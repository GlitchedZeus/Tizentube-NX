#include <borealis.hpp>
#include <switch.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <future>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "app_storage.hpp"
#include "libnx_http_client.hpp"
#ifdef TTNX_M3_INTEGRATED
#include "m3_player_activity.hpp"
#include "m3_diagnostics.hpp"
#include "media_file_sink.hpp"
#include "tizentube_nx/playback/guest_direct_extractor.hpp"
#include "tizentube_nx/playback/progressive_prepare.hpp"
#include "hls_cache_sink.hpp"
#include "tizentube_nx/playback/hls_probe.hpp"
#include "tizentube_nx/playback/hls_fallback.hpp"
#include "tizentube_nx/playback/hls_segment.hpp"
#include "tizentube_nx/playback/hls_cache.hpp"
#include "tizentube_nx/playback/adaptive_media_budget.hpp"
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
#include "m4_mpv_stream_cb_probe.hpp"
#include "tizentube_nx/core/content_filter.hpp"
#endif
#endif
#include "tizentube_nx/core/home_model.hpp"
#include "tizentube_nx/core/channel_model.hpp"
#include "tizentube_nx/core/url.hpp"
#include "tizentube_nx/core/search_model.hpp"
#include "tizentube_nx/core/search_presentation.hpp"
#include "tizentube_nx/ui/navigation.hpp"
#include "tizentube_nx/ui/library_pager.hpp"
#include "tizentube_nx/library/local_library.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"
#include "tizentube_nx/youtube/guest_home.hpp"
#include "tizentube_nx/youtube/guest_channel.hpp"
#include "tizentube_nx/youtube/search_flow.hpp"
#include "tizentube_nx/youtube/search_worker_guard.hpp"
#include "tizentube_nx/youtube/session_bootstrap.hpp"

namespace {

constexpr const char* kGuestUserAgent =
    "Mozilla/5.0 (Nintendo Switch; TizenTube NX) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
constexpr std::size_t kMaxUiTitleChars = 180;
constexpr std::size_t kMaxUiMetadataChars = 280;
// Keep the temporary M2 text UI bounded on real hardware while the model may
// retain many continuation pages. Final media UI can replace this with a
// recycler/virtualized list without changing SearchModel semantics.
constexpr std::size_t kMaxRenderedSearchResults = 40;

std::string bounded_ui_text(std::string_view text, std::size_t max_chars) {
    if (text.size() <= max_chars) return std::string(text);
    if (max_chars <= 3) return std::string(text.substr(0, max_chars));
    return std::string(text.substr(0, max_chars - 3)) + "...";
}

brls::Label* label(brls::Box* box, const std::string& text, float size = 23) {
    auto* view = new brls::Label();
    view->setText(text);
    view->setFontSize(size);
    view->setMarginBottom(22);
    box->addView(view);
    return view;
}

brls::Button* button(brls::Box* box, const std::string& text) {
    auto* view = new brls::Button();
    view->setText(text);
    view->setHeight(64);
    view->setMarginBottom(20);
    box->addView(view);
    return view;
}

class ChannelActivity : public brls::Activity {
public:
    ChannelActivity(
        ttnx::core::ChannelModel& model,
        std::function<void()> on_close)
        : model_(model), on_close_(std::move(on_close)) {}

    brls::View* createContentView() override {
        auto* scroll = new brls::ScrollingFrame();
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setPadding(32, 36, 32, 36);
        scroll->setContentView(content);

        title_label_ = label(content, "Channel", 34);
        status_label_ = label(content, "Loading Channel...", 20);
        metadata_label_ = label(content, "", 18);
        description_label_ = label(content, "", 18);

        back_button_ = button(content, "Back to Search");
        auto close = [this](brls::View*) {
            if (on_close_) on_close_();
            brls::Application::popActivity();
            return true;
        };
        back_button_->registerClickAction(close);
        content->registerAction("Back", brls::BUTTON_B, close);

        label(content,
              "Channel first-page guest browsing. Thumbnails, pagination and playback remain disabled for this hardware gate.",
              18);
        results_box_ = new brls::Box(brls::Axis::COLUMN);
        results_box_->setMarginBottom(8);
        content->addView(results_box_);
        diagnostic_label_ = label(content, "", 17);
        refresh(true);
        return scroll;
    }

    void set_diagnostics(std::string diagnostics) {
        diagnostics_ = std::move(diagnostics);
    }

    void refresh(bool rebuild_results) {
        if (!title_label_) return;
        const auto& metadata = model_.metadata();
        const auto& identity = model_.identity();
        std::string title = metadata.title.empty() ? identity.title : metadata.title;
        if (title.empty()) title = "Channel";
        title_label_->setText(bounded_ui_text(title, kMaxUiTitleChars));

        std::string status;
        switch (model_.state()) {
            case ttnx::core::ChannelViewState::Idle:
                status = "Channel is idle.";
                break;
            case ttnx::core::ChannelViewState::Loading:
                status = "Loading Channel first page...";
                break;
            case ttnx::core::ChannelViewState::Ready:
                status = std::to_string(model_.results().size()) + " normal Channel result";
                if (model_.results().size() != 1) status += 's';
                status += ".";
                break;
            case ttnx::core::ChannelViewState::Empty:
                status = "This selected Channel tab has no supported normal content.";
                break;
            case ttnx::core::ChannelViewState::Error:
                status = model_.error().empty()
                    ? "Channel could not be loaded safely."
                    : model_.error();
                break;
        }
        status_label_->setText(status);

        std::string metadata_text;
        if (!metadata.handle.empty()) metadata_text += metadata.handle;
        if (!metadata.subscriber_text.empty()) {
            if (!metadata_text.empty()) metadata_text += "  •  ";
            metadata_text += metadata.subscriber_text;
        }
        if (!metadata.canonical_url.empty()) {
            if (!metadata_text.empty()) metadata_text += "\n";
            metadata_text += metadata.canonical_url;
        }
        metadata_label_->setText(bounded_ui_text(metadata_text, kMaxUiMetadataChars));
        description_label_->setText(bounded_ui_text(metadata.description, 700));

        if (diagnostic_label_) {
            diagnostic_label_->setText(
                model_.state() == ttnx::core::ChannelViewState::Error
                    ? diagnostics_
                    : std::string{});
        }
        if (rebuild_results) rebuild();
    }

private:
    struct DetailRow {
        std::string identity;
        brls::Button* button{nullptr};
        brls::Label* detail{nullptr};
    };

    ttnx::core::ChannelModel& model_;
    std::function<void()> on_close_;
    std::string diagnostics_;
    brls::Label* title_label_{nullptr};
    brls::Label* status_label_{nullptr};
    brls::Label* metadata_label_{nullptr};
    brls::Label* description_label_{nullptr};
    brls::Label* diagnostic_label_{nullptr};
    brls::Button* back_button_{nullptr};
    brls::Box* results_box_{nullptr};
    std::vector<DetailRow> detail_rows_;

    void refresh_inline_details() {
        const auto& selected = model_.selected_identity();
        for (auto& row : detail_rows_) {
            if (!row.detail) continue;
            row.detail->setVisibility(
                !selected.empty() && row.identity == selected
                    ? brls::Visibility::VISIBLE
                    : brls::Visibility::GONE);
        }
    }

    void rebuild() {
        if (!results_box_) return;
        detail_rows_.clear();
        while (!results_box_->getChildren().empty()) {
            results_box_->removeView(results_box_->getChildren().back());
        }

        std::string last_section;
        for (const auto& section : model_.sections()) {
            if (!section.title.empty()) {
                label(results_box_, bounded_ui_text(section.title, 100), 24)->setMarginBottom(12);
            }
            for (const auto& result : section.results) append_result(result);
        }
        if (model_.sections().empty()) {
            for (const auto& result : model_.results()) append_result(result);
        }
    }

    void append_result(const ttnx::core::BrowseResult& result) {
        const auto identity = ttnx::core::browse_result_identity(result);
        if (identity.empty()) return;
        std::string title = "[" + ttnx::core::search_result_kind_label(result.kind) + "] ";
        title += result.title.empty() ? "Untitled result" : result.title;
        auto* select = button(results_box_, bounded_ui_text(title, kMaxUiTitleChars));
        select->setHeight(72);
        select->registerClickAction([this, identity](brls::View*) {
            if (model_.selected_identity() == identity) model_.clear_selection();
            else if (!model_.select(identity)) return true;
            refresh_inline_details();
            return true;
        });
        auto* detail = label(
            results_box_,
            ttnx::core::search_result_selection_display(result),
            18);
        detail->setMarginBottom(14);
        detail->setVisibility(
            identity == model_.selected_identity()
                ? brls::Visibility::VISIBLE
                : brls::Visibility::GONE);
        detail_rows_.push_back({identity, select, detail});
        const auto metadata = ttnx::core::search_result_metadata_display(result);
        if (!metadata.empty()) {
            auto* info = label(results_box_, bounded_ui_text(metadata, kMaxUiMetadataChars), 18);
            info->setMarginBottom(14);
        }
    }
};

class ShellActivity : public brls::Activity {
public:
    ShellActivity(
        ttnx::core::Settings settings,
        bool storage_ready,
        ttnx::switch_app::LibnxHttpClient& network_client)
        : settings_(settings),
          storage_ready_(storage_ready),
          network_client_(network_client) {}

    ~ShellActivity() override {
        shutdown_network();
    }

    brls::View* createContentView() override {
        ttnx::record_boot_event("boot-07-create-content");
        auto* frame = new brls::TabFrame();
        ttnx::record_boot_event("boot-08-tabframe");
        frame->setTitle("TizenTube NX");

        // This pinned framework has a placeholder footer. Supply real hints
        // and our own status/FPS label rather than its unimplemented FPS API.
        auto* footer = dynamic_cast<brls::Box*>(frame->getChildren().back());
        while (!footer->getChildren().empty()) {
            footer->removeView(footer->getChildren().back());
        }
        label(footer, "A Open    B Sections    + Exit", 20)->setMarginBottom(0);
        footer_ = label(footer, "M2 guest | Offline", 20);
        footer_->setMarginBottom(0);

        frame->getView("brls/tab_frame/sidebar")->setWidth(300);
        for (const auto& item : ttnx::ui::kRootNavigation) {
            frame->addTab(std::string(item.label), [this, section = item.section] {
                return create_page(section);
            });
        }
        ttnx::record_boot_event("boot-09-tabs-added");
        refresh_footer();
        return frame;
    }

    void tick() {
        // tick() runs after Borealis rendered/layouted the frame. A continuation
        // completed during the previous tick can therefore safely re-center the
        // still-focused Search control here using fresh Yoga coordinates.
        apply_pending_search_scroll_reflow();
        pump_network_result();
        pump_home_result();
        pump_search_result();
        pump_channel_result();
#ifdef TTNX_M3_INTEGRATED
        pump_m3_video();
#endif
        update_frame_rate();
    }

    void shutdown_network() {
        if (network_worker_.joinable()) network_worker_.join();
        if (home_worker_.joinable()) home_worker_.join();
        if (search_worker_.joinable()) search_worker_.join();
        if (channel_worker_.joinable()) channel_worker_.join();
#ifdef TTNX_M3_INTEGRATED
        media_cancel_requested_.store(true);
        if (media_worker_.joinable()) media_worker_.join();
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
        // Shutdown may bypass pump_m3_video(). Never leave an unlaunched
        // producer holding an app-owned temporary SD file or SSL client.
        std::shared_ptr<ttnx::playback::RollingStreamEpoch> abandoned_ring;
        {
            std::lock_guard<std::mutex> lock(media_mutex_);
            abandoned_ring=std::move(pending_m4_epoch_);
        }
        if(abandoned_ring) {
            abandoned_ring->stop_and_join();
            (void)abandoned_ring->cleanup();
        }
#endif
        // A PLUS-button/app exit from Search does not run the usual UI
        // completion pump. Sweep the just-cancelled temporary media here,
        // after the worker and both parallel transfer tasks have joined.
        // Active libmpv explicitly blocks deletion via its playback lease;
        // in that case the next boot performs the bounded recovery sweep.
        const bool mp4_discarded=
            ttnx::switch_app::SwitchMediaCacheSink::discard_session_media();
        const bool hls_discarded=
            ttnx::switch_app::SwitchHlsCacheSink::discard_session_media();
        ttnx::switch_app::M3Diagnostics::record(
            ttnx::switch_app::M3Stage::Cache,
            mp4_discarded && hls_discarded
                ?ttnx::switch_app::M3Event::Pass:ttnx::switch_app::M3Event::Fail,
            7,mp4_discarded ? 1 : 0,hls_discarded ? 1 : 0);
        media_busy_.store(false);
        media_done_.store(false);
#endif
        network_busy_.store(false);
        home_busy_.store(false);
        home_worker_done_.store(false);
        search_busy_.store(false);
        search_worker_done_.store(false);
        channel_busy_.store(false);
        channel_worker_done_.store(false);
    }


private:
    brls::Label* footer_ = nullptr;
    std::chrono::steady_clock::time_point sampled_ = std::chrono::steady_clock::now();
    unsigned frames_ = 0;
    int last_fps_ = 0;

    ttnx::core::Settings settings_;
    bool storage_ready_;
    ttnx::library::LocalLibrary library_{};
    bool history_clear_armed_{false};
    std::string query_;

    [[nodiscard]] std::string library_path()const {
        return std::string(ttnx::kAppDirectory)+"/library.txt";
    }
    void refresh_library_metadata() {
        ttnx::library::LocalLibrary loaded;
        if(storage_ready_ && loaded.load(library_path()))
            library_=std::move(loaded);
    }
    [[nodiscard]] bool persist_library_metadata() {
        return storage_ready_ && library_.save(library_path());
    }

    // main() owns this app-lifetime client. Workers only borrow it, so repeated
    // guest-test/Search presses never cycle socket/SSL service initialization.
    ttnx::switch_app::LibnxHttpClient& network_client_;

    std::atomic_bool network_busy_{false};
    std::thread network_worker_;
    std::mutex network_mutex_;
    bool pending_network_result_{false};
    std::string pending_network_summary_;
    std::string pending_network_detail_;
    std::optional<ttnx::youtube::GuestSession> pending_guest_session_;

    std::string network_summary_{"Offline"};
    std::string network_detail_{
        "No YouTube connection has been attempted. Networking starts only when you choose the test action."};
    std::optional<ttnx::youtube::GuestSession> guest_session_;
#ifdef TTNX_M3_INTEGRATED
    // Worker-only media service; never let a signed URL enter UI labels or logs.
    std::thread media_worker_;
    std::mutex media_mutex_;
    std::atomic_bool media_busy_{false};
    std::atomic_bool media_done_{false};
    std::atomic_bool media_cancel_requested_{false};
    bool pending_media_success_{false};
    bool pending_media_hls_{false};
    bool pending_media_adaptive_{false};
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
    std::shared_ptr<ttnx::playback::RollingStreamEpoch> pending_m4_epoch_{};
#endif
    std::string pending_media_feedback_;
    std::string media_feedback_{"Choose a normal video, then Play video (M3)."};
    std::string active_m3_video_id_;
    std::uint32_t active_m3_resume_seconds_{0};
    std::atomic<int> media_step_{0}; // 0 resolver, 1 media URL accepted, 2 downloading
    std::atomic<std::uint64_t> media_progress_bytes_{0};
    std::atomic<std::uint64_t> media_progress_total_{0};
    std::atomic_bool media_parallel_adaptive_{false};
    std::atomic<std::uint64_t> media_audio_bytes_{0},media_audio_total_{0};
    std::atomic<std::uint64_t> media_video_bytes_{0},media_video_total_{0};
    std::atomic<unsigned> media_segments_done_{0};
    std::atomic<unsigned> media_segments_total_{0};
    std::atomic_bool media_external_aac_phase_{false};
    std::atomic_bool media_buffer_ready_{false};
    std::atomic<std::uint64_t> media_buffer_ms_{0};
    std::chrono::steady_clock::time_point media_last_ui_update_ =
        std::chrono::steady_clock::now();
    brls::Label* m3_status_label_{nullptr};
#endif

    // HomeModel is UI-thread-only. The explicit Home worker publishes normalized
    // HomePage data plus the captured generation and never touches Borealis Views.
    ttnx::core::HomeModel home_model_;
    std::string home_diagnostics_;
    std::atomic_bool home_busy_{false};
    std::atomic_bool home_worker_done_{false};
    std::thread home_worker_;
    std::mutex home_mutex_;
    bool pending_home_result_{false};
    std::uint64_t pending_home_generation_{0};
    ttnx::youtube::GuestHomeResult pending_home_;

    // SearchModel is UI-thread-only. GuestSearchFlow is worker-thread-only and
    // there is at most one Search worker at a time. The worker publishes only a
    // normalized result/error plus the captured model generation.
    ttnx::core::SearchModel search_model_;
    ttnx::youtube::GuestSearchFlow search_flow_;
    std::atomic_bool search_busy_{false};
    std::atomic_bool search_worker_done_{false};
    std::thread search_worker_;
    std::mutex search_mutex_;
    bool pending_search_result_{false};
    std::uint64_t pending_search_generation_{0};
    bool pending_search_continuation_{false};
    ttnx::youtube::GuestSearchResult pending_search_;
    std::atomic_bool pending_search_emergency_{false};
    std::atomic<std::uint64_t> pending_search_emergency_generation_{0};
    std::atomic_bool pending_search_emergency_continuation_{false};
    std::atomic<ttnx::core::SearchErrorCode> pending_search_emergency_code_{
        ttnx::core::SearchErrorCode::None};
    std::atomic<ttnx::youtube::SearchWorkerStage> search_worker_stage_{
        ttnx::youtube::SearchWorkerStage::Idle};

    // Channel first-page state lives above the accepted Search Activity. The
    // network worker owns no Views; the plain ChannelActivity is updated only
    // by ShellActivity::tick() on the UI thread.
    ttnx::core::ChannelModel channel_model_;
    std::atomic_bool channel_busy_{false};
    std::atomic_bool channel_worker_done_{false};
    std::thread channel_worker_;
    std::mutex channel_mutex_;
    bool pending_channel_result_{false};
    std::uint64_t pending_channel_generation_{0};
    ttnx::youtube::GuestChannelResult pending_channel_;
    std::string channel_diagnostics_;
    ChannelActivity* active_channel_activity_{nullptr};

    // These pointers are UI-thread-only conveniences for the currently visible
    // plain ScrollingFrame. create_page() clears every page pointer before the
    // replacement page is built. Workers never capture or dereference them.
    brls::Button* home_probe_button_{nullptr};
    brls::Label* home_status_label_{nullptr};
    brls::Label* home_detail_label_{nullptr};
    brls::Button* home_load_button_{nullptr};
    brls::Label* home_feed_status_label_{nullptr};
    brls::Box* home_results_box_{nullptr};
    struct HomeDetailRow {
        std::string identity;
        brls::Button* button{nullptr};
        brls::Label* detail{nullptr};
    };
    std::vector<HomeDetailRow> home_detail_rows_;

    brls::Label* search_query_label_{nullptr};
    brls::Label* search_status_label_{nullptr};
    brls::Button* search_action_button_{nullptr};
    brls::Button* search_load_more_button_{nullptr};
    brls::Box* search_results_box_{nullptr};
    brls::ScrollingFrame* search_scroll_{nullptr};
    bool search_scroll_reflow_pending_{false};
    std::string search_post_load_focus_identity_;

    struct SearchDetailRow {
        std::string identity;
        brls::Button* button{nullptr};
        brls::Label* detail{nullptr};
        brls::Button* open_channel{nullptr};
        brls::Button* watch_later{nullptr};
        brls::Button* favorite_video{nullptr};
        brls::Button* follow_channel{nullptr};
#ifdef TTNX_M3_INTEGRATED
        brls::Button* play_video{nullptr};
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
        brls::Button* m4_test_video{nullptr};
#endif
        brls::Label* play_status{nullptr};
        brls::Button* cancel_video{nullptr};
#endif
    };
    std::vector<SearchDetailRow> search_detail_rows_;

    void apply_pending_search_scroll_reflow() {
        // Continuation completion mutates the detached Search tree after the
        // current frame has already been laid out. Defer the focus handoff until
        // the next post-layout tick, then focus the first newly-rendered result.
        // The local pixel-offset ScrollingFrame backport now makes this safe: a
        // programmatic focus change can re-center against stable pixel state.
        if (!search_post_load_focus_identity_.empty()) {
            const std::string identity = std::move(search_post_load_focus_identity_);
            search_post_load_focus_identity_.clear();
            for (const auto& row : search_detail_rows_) {
                if (row.identity == identity && row.button) {
                    brls::Application::giveFocus(row.button);
                    // Re-run one more centering pass on the following frame.
                    // This keeps the focus highlight and viewport aligned even
                    // if Borealis starts an animated focus scroll here.
                    if (search_scroll_) search_scroll_reflow_pending_ = true;
                    return;
                }
            }
        }

        if (!search_scroll_reflow_pending_) return;
        search_scroll_reflow_pending_ = false;
        if (!search_scroll_ || !search_results_box_) return;

        auto* focused = brls::Application::getCurrentFocus();
        if (!focused) return;

        bool search_result_focus = focused == search_load_more_button_;
        if (!search_result_focus) {
            for (const auto& row : search_detail_rows_) {
                if (row.button == focused) {
                    search_result_focus = true;
                    break;
                }
            }
        }
        // A continuation may complete after B moved focus into the sidebar.
        // Never use a sidebar coordinate to reposition the Search viewport.
        if (!search_result_focus) return;

        // Pinned ScrollingFrame has no public refresh method. Its public child
        // focus callback is the supported path into the same centering logic.
        // Our local pixel-offset backport makes this stable across content-size
        // changes instead of reinterpreting a stale percentage against a new
        // content height.
        search_scroll_->onChildFocusGained(search_results_box_, focused);
    }

    void refresh_footer() {
        if (!footer_) return;
        std::string text = "M2 guest | ";
        if (channel_busy_.load()) {
            text += "Loading Channel...";
        } else if (search_busy_.load()) {
            text += "Searching...";
        } else if (home_busy_.load()) {
            text += "Loading Home...";
        } else if (network_busy_.load()) {
            text += "Connecting...";
        } else {
            text += network_summary_;
        }
#ifdef TTNX_M3_INTEGRATED
        // The footer stays visible even when the selected Play button is
        // hundreds of rows below the global status label.
        if (!active_m3_video_id_.empty()) {
            text += " | M3: " + bounded_ui_text(media_feedback_, 100);
        }
#endif
        if (settings_.show_fps && last_fps_ > 0) {
            text += " | " + std::to_string(last_fps_) + " FPS";
        }
        footer_->setText(text);
    }

    void update_frame_rate() {
        ++frames_;
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - sampled_).count();
        if (elapsed < 1.0) return;
        last_fps_ = static_cast<int>(frames_ / elapsed + 0.5);
        sampled_ = now;
        frames_ = 0;
        refresh_footer();
    }

    std::string home_feed_status_text() const {
        if (network_busy_.load()) {
            return "Guest connection diagnostics are running. Home can load when they finish.";
        }
        if (home_busy_.load()) {
            return home_model_.results().empty()
                ? "Loading YouTube Home..."
                : "Refreshing YouTube Home... Existing results remain available.";
        }
        if (!guest_session_ || !guest_session_->usable()) {
            return "Guest session unavailable. Run Test YouTube guest connection first.";
        }

        switch (home_model_.state()) {
            case ttnx::core::HomeViewState::Idle:
                return "Guest ready. Press Load Home to fetch the first Home page.";
            case ttnx::core::HomeViewState::Loading:
                return "Loading YouTube Home...";
            case ttnx::core::HomeViewState::Ready: {
                std::string text = std::to_string(home_model_.results().size()) + " Home result";
                if (home_model_.results().size() != 1) text += 's';
                text += ". First-page Home hardware gate; continuation is deferred.";
                return text;
            }
            case ttnx::core::HomeViewState::Empty: {
                if (home_model_.empty_reason() == ttnx::core::HomeEmptyReason::FeedNudge) {
                    return "YouTube isn't providing guest Home recommendations for this session. Use Search to get started.";
                }
                std::string text = "YouTube Home returned no supported normal results.";
                if (!home_diagnostics_.empty()) text += "\n" + home_diagnostics_;
                return text;
            }
            case ttnx::core::HomeViewState::Error: {
                std::string text = home_model_.error().empty()
                    ? "Home could not be loaded safely."
                    : home_model_.error();
                if (!home_diagnostics_.empty()) text += "\n" + home_diagnostics_;
                return text;
            }
        }
        return "Home state unavailable.";
    }

    void refresh_home_inline_details() {
        const auto& selected_identity = home_model_.selected_identity();
        for (auto& row : home_detail_rows_) {
            if (!row.detail) continue;
            const bool selected = !selected_identity.empty() && row.identity == selected_identity;
            row.detail->setVisibility(
                selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }
    }

    void append_home_result_row(const ttnx::core::BrowseResult& result) {
        if (!home_results_box_) return;
        const auto identity = ttnx::core::browse_result_identity(result);
        if (identity.empty()) return;

        std::string title = "[" + ttnx::core::search_result_kind_label(result.kind) + "] ";
        title += result.title.empty() ? "Untitled result" : result.title;
        auto* select = button(home_results_box_, bounded_ui_text(title, kMaxUiTitleChars));
        select->setHeight(72);
        select->registerClickAction([this, identity](brls::View*) {
            if (home_model_.selected_identity() == identity) {
                home_model_.clear_selection();
            } else if (!home_model_.select(identity)) {
                return true;
            }
            refresh_home_inline_details();
            return true;
        });

        auto* detail_label = label(
            home_results_box_,
            ttnx::core::search_result_selection_display(result),
            18);
        detail_label->setMarginBottom(14);
        detail_label->setVisibility(
            identity == home_model_.selected_identity()
                ? brls::Visibility::VISIBLE
                : brls::Visibility::GONE);
        home_detail_rows_.push_back({identity, select, detail_label});

        const auto metadata = ttnx::core::search_result_metadata_display(result);
        if (!metadata.empty()) {
            auto* metadata_label = label(
                home_results_box_,
                bounded_ui_text(metadata, kMaxUiMetadataChars),
                18);
            metadata_label->setMarginBottom(14);
        }
    }

    void rebuild_home_results() {
        if (!home_results_box_) return;
        home_detail_rows_.clear();
        while (!home_results_box_->getChildren().empty()) {
            home_results_box_->removeView(home_results_box_->getChildren().back());
        }
        for (const auto& result : home_model_.results()) {
            append_home_result_row(result);
        }
    }

    void refresh_home_page(bool rebuild_results = false) {
        if (home_probe_button_) {
            home_probe_button_->setText(network_busy_.load()
                ? "YouTube guest connection: Connecting..."
                : "Test YouTube guest connection");
        }
        if (home_status_label_) {
            home_status_label_->setText("Connection status: " + network_summary_);
        }
        if (home_detail_label_) home_detail_label_->setText(network_detail_);
        if (home_feed_status_label_) home_feed_status_label_->setText(home_feed_status_text());

        if (home_load_button_) {
            const bool guest_ready = guest_session_ && guest_session_->usable();
            home_load_button_->setVisibility(guest_ready ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            home_load_button_->setText(home_busy_.load()
                ? "Home: Working..."
                : (home_model_.results().empty() ? "Load Home" : "Refresh Home"));
        }
        if (rebuild_results) rebuild_home_results();
    }

    void publish_home_result(
        std::uint64_t generation,
        ttnx::youtube::GuestHomeResult result) noexcept {
        try {
            std::lock_guard<std::mutex> lock(home_mutex_);
            pending_home_generation_ = generation;
            pending_home_ = std::move(result);
            pending_home_result_ = true;
        } catch (...) {
            pending_home_generation_ = generation;
            pending_home_result_ = false;
        }
        home_worker_done_.store(true);
    }

    void pump_home_result() {
        if (!home_worker_done_.load()) return;
        if (home_worker_.joinable()) home_worker_.join();

        ttnx::youtube::GuestHomeResult result;
        std::uint64_t generation = pending_home_generation_;
        bool have_result = false;
        {
            std::lock_guard<std::mutex> lock(home_mutex_);
            if (pending_home_result_) {
                generation = pending_home_generation_;
                result = std::move(pending_home_);
                pending_home_result_ = false;
                have_result = true;
            }
        }

        home_worker_done_.store(false);
        home_busy_.store(false);

        if (have_result) {
            home_diagnostics_ = std::move(result.diagnostics);
        } else {
            home_diagnostics_.clear();
        }

        bool applied = false;
        if (have_result && result.page) {
            applied = home_model_.apply_page(generation, std::move(*result.page));
        } else if (have_result) {
            applied = home_model_.fail(
                generation,
                result.error.empty()
                    ? "Home could not be loaded safely."
                    : std::move(result.error));
        } else {
            applied = home_model_.fail(generation, "Home failed safely because of an internal error.");
        }

        refresh_footer();
        refresh_home_page(applied);
    }

    void start_home_load() {
        if (home_busy_.load() || search_busy_.load() || network_busy_.load()) {
            refresh_home_page(false);
            return;
        }
        if (!guest_session_ || !guest_session_->usable()) {
            refresh_home_page(false);
            return;
        }
        if (home_worker_.joinable()) home_worker_.join();

        const auto generation = home_model_.begin_load();
        home_diagnostics_.clear();
        home_busy_.store(true);
        home_worker_done_.store(false);
        refresh_footer();
        refresh_home_page(false);

        try {
            const auto session = *guest_session_;
            home_worker_ = std::thread([this, generation, session] {
                try {
                    ttnx::core::GuestRequest request;
                    request.surface = ttnx::core::BrowseSurface::Home;
                    publish_home_result(
                        generation,
                        ttnx::youtube::execute_guest_home(network_client_, session, request));
                } catch (const std::bad_alloc&) {
                    ttnx::youtube::GuestHomeResult failure;
                    failure.error = "Home failed safely because there was not enough available memory.";
                    publish_home_result(generation, std::move(failure));
                } catch (const std::exception&) {
                    ttnx::youtube::GuestHomeResult failure;
                    failure.error = "Home failed safely because of an internal error.";
                    publish_home_result(generation, std::move(failure));
                } catch (...) {
                    ttnx::youtube::GuestHomeResult failure;
                    failure.error = "Home failed safely because of an internal error.";
                    publish_home_result(generation, std::move(failure));
                }
            });
        } catch (...) {
            home_busy_.store(false);
            home_worker_done_.store(false);
            const bool applied = home_model_.fail(
                generation,
                "Could not start the Home worker thread.");
            (void)applied;
            refresh_footer();
            refresh_home_page(false);
        }
    }

    std::string search_status_text() const {
        if (network_busy_.load()) {
            return "Guest connection diagnostics are running. Search can start when they finish.";
        }
        if (search_busy_.load()) {
            if (search_model_.state() == ttnx::core::SearchViewState::LoadingMore) {
                return "Loading more results...";
            }
            if (search_model_.state() == ttnx::core::SearchViewState::Idle) {
                return "A previous Search request is finishing. Its stale result will be ignored.";
            }
            return "Searching YouTube...";
        }
        if (!guest_session_ || !guest_session_->usable()) {
            return "Guest session unavailable. On Home, run Test YouTube guest connection first.";
        }

        switch (search_model_.state()) {
            case ttnx::core::SearchViewState::Idle:
                return query_.empty()
                    ? "Enter a query, then press Search."
                    : "Ready. Press Search to query YouTube.";
            case ttnx::core::SearchViewState::Loading:
                return "Searching YouTube...";
            case ttnx::core::SearchViewState::Ready: {
                std::string text = std::to_string(search_model_.results().size()) + " result";
                if (search_model_.results().size() != 1) text += 's';
                if (search_model_.end_of_results()) {
                    text += ". End of results.";
                } else if (search_model_.can_load_more()) {
                    text += ". More results are available.";
                } else {
                    text += '.';
                }
                if (search_model_.results().size() > kMaxRenderedSearchResults) {
                    text += " Hardware view shows newest " +
                            std::to_string(kMaxRenderedSearchResults) +
                            " while all loaded results remain in the model.";
                }
                return text;
            }
            case ttnx::core::SearchViewState::Empty:
                return search_model_.can_load_more()
                    ? "No results on this page yet. More results are available."
                    : "No results found.";
            case ttnx::core::SearchViewState::Error:
                return search_model_.error().empty()
                    ? "Search could not be completed."
                    : search_model_.error();
            case ttnx::core::SearchViewState::LoadingMore:
                return "Loading more results...";
        }
        return "Search state unavailable.";
    }

    void refresh_search_inline_details() {
        const auto& selected_identity = search_model_.selected_identity();
        for (auto& row : search_detail_rows_) {
            const bool selected = !selected_identity.empty() && row.identity == selected_identity;
            if (row.detail) {
                row.detail->setVisibility(
                    selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            }
            if (row.open_channel) {
                row.open_channel->setVisibility(
                    selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            }
            if (row.watch_later) {
                row.watch_later->setVisibility(
                    selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            }
            if (row.favorite_video)
                row.favorite_video->setVisibility(selected
                    ?brls::Visibility::VISIBLE:brls::Visibility::GONE);
            if (row.follow_channel)
                row.follow_channel->setVisibility(selected
                    ?brls::Visibility::VISIBLE:brls::Visibility::GONE);
#ifdef TTNX_M3_INTEGRATED
            if (row.play_video) {
                row.play_video->setVisibility(
                    selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            }
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
            if (row.m4_test_video) {
                row.m4_test_video->setVisibility(
                    selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            }
#endif
            if (row.play_status) {
                row.play_status->setVisibility(
                    selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            }
            if (row.cancel_video) {
                const bool playing_this = media_busy_.load() &&
                    row.identity == "video:" + active_m3_video_id_;
                row.cancel_video->setVisibility(
                    selected && playing_this
                    ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            }
#endif
        }
    }

    void refresh_search_load_more_navigation() {
        if (!search_load_more_button_) return;

        // Pinned Borealis traverses into a sibling Box via getDefaultFocus().
        // Because search_results_box_ is the nested sibling before Load more,
        // Up otherwise resolves to result 1. Route that edge to the actual
        // last result. With no results, Search is the stable predecessor.
        brls::View* up_target = search_action_button_;
        if (!search_detail_rows_.empty() && search_detail_rows_.back().button) {
            up_target = search_detail_rows_.back().button;
        }
        if (up_target) {
            search_load_more_button_->setCustomNavigationRoute(
                brls::FocusDirection::UP, up_target);
        }
    }

    brls::Button* append_search_result_row(const ttnx::core::BrowseResult& result) {
        if (!search_results_box_) return nullptr;
        const auto identity = ttnx::core::browse_result_identity(result);
        if (identity.empty()) return nullptr;

        std::string title = "[" + ttnx::core::search_result_kind_label(result.kind) + "] ";
        title += result.title.empty() ? "Untitled result" : result.title;
        auto* select = button(
            search_results_box_,
            bounded_ui_text(title, kMaxUiTitleChars));
        select->setHeight(72);
        select->registerClickAction([this, identity](brls::View*) {
            if (search_model_.selected_identity() == identity) {
                search_model_.clear_selection();
            } else if (!search_model_.select(identity)) {
                return true;
            }
            // A toggles only the existing inline detail. The focused row
            // stays alive and is never replaced or force-refocused.
            refresh_search_inline_details();
            return true;
        });

        auto selection_text = ttnx::core::search_result_selection_display(result);
#ifdef TTNX_M3_INTEGRATED
        if (result.kind == ttnx::core::BrowseResultKind::Video) {
            const auto stale = selection_text.find("\nPlayback is not enabled in this milestone.");
            if (stale != std::string::npos) selection_text.erase(stale);
        }
#endif
        auto* detail_label = label(
            search_results_box_, selection_text, 18);
        detail_label->setMarginBottom(14);
        detail_label->setVisibility(
            identity == search_model_.selected_identity()
                ? brls::Visibility::VISIBLE
                : brls::Visibility::GONE);
        brls::Button* open_channel = nullptr;
        if (result.kind == ttnx::core::BrowseResultKind::Channel &&
            ttnx::core::is_valid_channel_id(result.id)) {
            open_channel = button(search_results_box_, "Open channel");
            open_channel->setVisibility(brls::Visibility::GONE);
            const auto channel_id = result.id;
            const auto channel_title = result.title;
            open_channel->registerClickAction([this, channel_id, channel_title](brls::View*) {
                open_channel_page(channel_id, channel_title);
                return true;
            });
        }
        brls::Button* follow_channel = nullptr;
        if(result.kind == ttnx::core::BrowseResultKind::Channel &&
           ttnx::library::LocalLibrary::valid_channel_id(result.id)) {
            const auto channel_id=result.id;
            follow_channel=button(search_results_box_,"Follow on this Switch");
            follow_channel->setVisibility(brls::Visibility::GONE);
            follow_channel->registerClickAction([this,channel_id,follow_channel](brls::View*) {
                refresh_library_metadata();
                if(library_.follow_channel(channel_id) &&
                   persist_library_metadata())
                    follow_channel->setText("Following locally");
                else follow_channel->setText("Cannot save local follow");
                return true;
            });
        }
        brls::Button* favorite_video = nullptr;
        brls::Button* watch_later = nullptr;
        if (result.kind == ttnx::core::BrowseResultKind::Video &&
            ttnx::library::LocalLibrary::valid_id(result.id)) {
            const auto video_id = result.id;
            watch_later = button(search_results_box_, "Add to Watch Later (local)");
            watch_later->setVisibility(brls::Visibility::GONE);
            watch_later->registerClickAction([this, video_id, watch_later](brls::View*) {
                refresh_library_metadata();
                if(library_.add_watch_later(video_id) &&
                   persist_library_metadata())
                    watch_later->setText("Saved to Watch Later");
                else watch_later->setText("Could not save: SD/library limit");
                return true;
            });
            favorite_video=button(search_results_box_,"Add to Favorites (local)");
            favorite_video->setVisibility(brls::Visibility::GONE);
            favorite_video->registerClickAction([this,video_id,favorite_video](brls::View*) {
                refresh_library_metadata();
                if(library_.add_favorite(video_id) &&
                   persist_library_metadata())
                    favorite_video->setText("Saved to Favorites");
                else favorite_video->setText("Could not save local favorite");
                return true;
            });
        }
#ifdef TTNX_M3_INTEGRATED
        brls::Button* play_video = nullptr;
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
        brls::Button* m4_test_video = nullptr;
#endif
        if (result.kind == ttnx::core::BrowseResultKind::Video &&
            ttnx::core::is_valid_video_id(result.id)) {
            // Keep the physically accepted M3 source/player available in
            // both standard and M4 experimental hardware-test builds.
            play_video = button(search_results_box_, "Play video (M3 experimental)");
            play_video->setVisibility(brls::Visibility::GONE);
            const auto video_id = result.id;
            play_video->registerClickAction([this, video_id](brls::View*) {
                start_m3_video(video_id);
                return true;
            });
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
            if(!result.live && !result.upcoming) {
                // M4 requires a current normalized, non-Shorts Search VOD.
                // A library ID alone is never authority to open CDN media.
                const ttnx::core::ContentItem verified{
                    ttnx::core::ContentKind::Video,video_id,result.title,false,false};
                m4_test_video=button(search_results_box_, "Play streaming (M4 hardware test)");
                m4_test_video->setVisibility(brls::Visibility::GONE);
                m4_test_video->registerClickAction([this,verified](brls::View*) {
                    start_m4_native_video(verified);
                    return true;
                });
            }
#endif
        }
        brls::Label* play_status = nullptr;
        if (play_video) {
            play_status = label(search_results_box_, media_feedback_, 18);
            play_status->setVisibility(brls::Visibility::GONE);
        }
        brls::Button* cancel_video = nullptr;
        if (play_video) {
            cancel_video = button(search_results_box_, "Cancel video download");
            cancel_video->setVisibility(brls::Visibility::GONE);
            cancel_video->registerClickAction([this](brls::View*) {
                if (media_busy_.load()) {
                    media_cancel_requested_.store(true);
                    media_feedback_ = "M3: cancelling the current media request...";
                    publish_m3_feedback_on_ui(active_m3_video_id_);
                }
                return true;
            });
        }
        search_detail_rows_.push_back({
            identity, select, detail_label, open_channel,
            watch_later, favorite_video, follow_channel,
            play_video,
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
            m4_test_video,
#endif
            play_status, cancel_video});
#else
        search_detail_rows_.push_back({
            identity, select, detail_label, open_channel,
            watch_later, favorite_video, follow_channel});
#endif

        const auto metadata = ttnx::core::search_result_metadata_display(result);
        if (!metadata.empty()) {
            auto* metadata_label = label(
                search_results_box_,
                bounded_ui_text(metadata, kMaxUiMetadataChars),
                18);
            metadata_label->setMarginBottom(14);
        }
        return select;
    }

    std::string append_search_results_from(std::size_t start_index) {
        if (!search_results_box_) return {};
        const auto& results = search_model_.results();
        if (start_index > results.size()) return {};

        std::string first_added_identity;
        for (std::size_t i = start_index; i < results.size(); ++i) {
            const auto identity = ttnx::core::browse_result_identity(results[i]);
            auto* row = append_search_result_row(results[i]);
            if (row && first_added_identity.empty()) first_added_identity = identity;
        }
        refresh_search_load_more_navigation();
        return first_added_identity;
    }

    void rebuild_search_results() {
        if (!search_results_box_) return;

        // A genuine full rebuild can delete old rows. Point Load more at
        // a page-stable control first so its custom route cannot retain a
        // result pointer that is about to be destroyed.
        if (search_load_more_button_ && search_action_button_) {
            search_load_more_button_->setCustomNavigationRoute(
                brls::FocusDirection::UP, search_action_button_);
        }
        // Keep the live Borealis view tree bounded during continuation stress.
        // SearchModel remains append-only/deduped; only the temporary text UI is
        // windowed to the newest results.
        search_detail_rows_.clear();
        while (!search_results_box_->getChildren().empty()) {
            search_results_box_->removeView(search_results_box_->getChildren().back());
        }
        const auto result_count = search_model_.results().size();
        const std::size_t start = result_count > kMaxRenderedSearchResults
            ? result_count - kMaxRenderedSearchResults
            : 0;
        (void)append_search_results_from(start);
    }

    void refresh_search_load_more_control(bool focus_fallback_if_hiding = false) {
        if (!search_load_more_button_) return;

        refresh_search_load_more_navigation();
        const auto state = search_model_.state();
        const bool retry_continuation =
            state == ttnx::core::SearchViewState::Error &&
            search_model_.retryable_failure() &&
            search_model_.error_code() == ttnx::core::SearchErrorCode::ContinuationFailure;
        const bool loadable =
            (state == ttnx::core::SearchViewState::Ready ||
             state == ttnx::core::SearchViewState::Empty) &&
            search_model_.can_load_more();
        const bool loading_more = state == ttnx::core::SearchViewState::LoadingMore;
        const bool guest_ready = guest_session_ && guest_session_->usable();
        const bool show = guest_ready && !network_busy_.load() &&
                          (loadable || loading_more || retry_continuation);

        if (!show) {
            if (focus_fallback_if_hiding &&
                brls::Application::getCurrentFocus() == search_load_more_button_) {
                if (!search_detail_rows_.empty() && search_detail_rows_.back().button) {
                    brls::Application::giveFocus(search_detail_rows_.back().button);
                } else if (search_action_button_) {
                    brls::Application::giveFocus(search_action_button_);
                }
            }
            search_load_more_button_->setVisibility(brls::Visibility::GONE);
            return;
        }

        search_load_more_button_->setVisibility(brls::Visibility::VISIBLE);
        if (loading_more) {
            search_load_more_button_->setText("Loading more...");
        } else if (retry_continuation) {
            search_load_more_button_->setText("Retry Load more");
        } else {
            search_load_more_button_->setText("Load more");
        }
    }

    void refresh_search_page(
        bool rebuild_results,
        bool focus_fallback_if_hiding = false) {
        if (search_query_label_) {
            search_query_label_->setText(query_.empty()
                ? "Query: none"
                : "Query: " + bounded_ui_text(query_, 120));
        }
        if (search_status_label_) search_status_label_->setText(search_status_text());

        if (search_action_button_) {
            const bool retry_first_page =
                search_model_.state() == ttnx::core::SearchViewState::Error &&
                search_model_.retryable_failure() &&
                search_model_.error_code() != ttnx::core::SearchErrorCode::ContinuationFailure;
            search_action_button_->setText(search_busy_.load()
                ? "Search: Working..."
                : retry_first_page ? "Retry Search" : "Search");
        }

        if (rebuild_results) rebuild_search_results();
        refresh_search_load_more_control(focus_fallback_if_hiding);
    }

    void publish_network_result(
        std::string summary,
        std::string detail,
        std::optional<ttnx::youtube::GuestSession> session = std::nullopt) {
        {
            std::lock_guard<std::mutex> lock(network_mutex_);
            pending_network_summary_ = std::move(summary);
            pending_network_detail_ = std::move(detail);
            pending_guest_session_ = std::move(session);
            pending_network_result_ = true;
        }
        network_busy_.store(false);
    }

    void pump_network_result() {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(network_mutex_);
            if (pending_network_result_) {
                network_summary_ = std::move(pending_network_summary_);
                network_detail_ = std::move(pending_network_detail_);
                guest_session_ = std::move(pending_guest_session_);
                pending_network_result_ = false;
                changed = true;
            }
        }

        // Once the worker has published its result, join it from the UI thread.
        // It has no UI references and should already be at its return boundary.
        if (!network_busy_.load() && network_worker_.joinable()) {
            network_worker_.join();
        }
        if (changed) {
            refresh_footer();
            refresh_home_page();
            refresh_search_page(false);
        }
    }

    void start_guest_connection_test() {
        if (search_busy_.load() || home_busy_.load() || channel_busy_.load()) {
            network_detail_ = "A live YouTube request is in progress. Try the guest diagnostic again after it finishes.";
            refresh_home_page();
            return;
        }

        bool expected = false;
        if (!network_busy_.compare_exchange_strong(expected, true)) {
            network_summary_ = "Connecting...";
            refresh_footer();
            refresh_home_page();
            return;
        }

        if (network_worker_.joinable()) network_worker_.join();
        guest_session_.reset();
        home_model_.reset();
        network_summary_ = "Connecting...";
        network_detail_ =
            "Connecting only to the allowlisted YouTube host. Nintendo destinations remain blocked before DNS.";
        refresh_footer();
        refresh_home_page(true);
        refresh_search_page(false);

        try {
            network_worker_ = std::thread([this] {
                auto& client = network_client_;
                if (!client.ready()) {
                    std::string summary = "Network init failed";
                    if (client.initialization_failure() == ttnx::switch_app::NativeInitFailure::Socket) {
                        summary = "Socket init failed";
                    } else if (client.initialization_failure() ==
                               ttnx::switch_app::NativeInitFailure::Ssl) {
                        summary = "SSL init failed";
                    }
                    publish_network_result(
                        std::move(summary),
                        client.service_status() + " | " +
                            (client.initialization_error().empty()
                                ? "The native Switch network services could not be initialized."
                                : client.initialization_error()));
                    return;
                }

                auto request = ttnx::youtube::make_session_bootstrap_request(
                    "en-US", "UTC", kGuestUserAgent, {});
                request.timeout_ms = 12000;

                const auto http = client.perform(request);
                if (!http.ok) {
                    std::string summary = "HTTP failed";
                    switch (http.failure_stage) {
                        case ttnx::net::HttpFailureStage::Policy:
                            summary = "Network policy blocked";
                            break;
                        case ttnx::net::HttpFailureStage::Tcp:
                            summary = "TCP failed";
                            break;
                        case ttnx::net::HttpFailureStage::Tls:
                            summary = "TLS failed";
                            break;
                        case ttnx::net::HttpFailureStage::Http:
                            summary = "HTTP failed";
                            break;
                        case ttnx::net::HttpFailureStage::None:
                            summary = "HTTPS failed";
                            break;
                    }
                    publish_network_result(
                        std::move(summary),
                        client.service_status() + " | " +
                            (http.error.empty()
                                ? "The verified YouTube HTTPS request failed."
                                : http.error));
                    return;
                }

                ttnx::youtube::SessionBootstrapOptions options;
                options.language = "en-US";
                options.timezone = "UTC";
                options.user_agent = kGuestUserAgent;
                auto parsed = ttnx::youtube::parse_session_bootstrap(http.response.body, options);
                if (!parsed || !parsed.session || !parsed.session->usable()) {
                    publish_network_result(
                        "Bootstrap rejected",
                        client.service_status() + " | " +
                            (parsed.error.empty()
                                ? "YouTube replied, but the guest bootstrap shape was not accepted."
                                : parsed.error));
                    return;
                }

                // Visitor/session fields stay in memory only. Never log them or
                // include them in status strings.
                publish_network_result(
                    "Guest ready",
                    client.service_status() +
                        " | Verified HTTPS and the bounded YouTube guest bootstrap both succeeded.",
                    std::move(parsed.session));
            });
        } catch (...) {
            network_busy_.store(false);
            network_summary_ = "Worker failed";
            network_detail_ = "Could not start the guest-network worker thread.";
            refresh_footer();
            refresh_home_page();
            refresh_search_page(false);
        }
    }

    void publish_search_result(
        std::uint64_t generation,
        bool continuation,
        ttnx::youtube::GuestSearchResult result) noexcept {
        try {
            std::lock_guard<std::mutex> lock(search_mutex_);
            pending_search_generation_ = generation;
            pending_search_continuation_ = continuation;
            pending_search_ = std::move(result);
            pending_search_result_ = true;
        } catch (const std::bad_alloc&) {
            pending_search_emergency_generation_.store(generation);
            pending_search_emergency_continuation_.store(continuation);
            pending_search_emergency_code_.store(ttnx::core::SearchErrorCode::MemoryPressure);
            pending_search_emergency_.store(true);
        } catch (...) {
            pending_search_emergency_generation_.store(generation);
            pending_search_emergency_continuation_.store(continuation);
            pending_search_emergency_code_.store(ttnx::core::SearchErrorCode::InternalFailure);
            pending_search_emergency_.store(true);
        }
        // Keep search_busy_ true until the UI thread joins and applies the
        // completion. This closes the tiny window where a second button press
        // could overwrite an unconsumed pending completion.
        search_worker_done_.store(true);
    }

    void pump_search_result() {
        if (!search_worker_done_.load()) return;
        if (search_worker_.joinable()) search_worker_.join();

        ttnx::youtube::GuestSearchResult result;
        std::uint64_t generation = 0;
        bool continuation = false;
        bool have_result = false;
        {
            std::lock_guard<std::mutex> lock(search_mutex_);
            if (pending_search_result_) {
                generation = pending_search_generation_;
                continuation = pending_search_continuation_;
                result = std::move(pending_search_);
                pending_search_result_ = false;
                have_result = true;
            }
        }

        const bool have_emergency = pending_search_emergency_.exchange(false);
        const auto emergency_generation = pending_search_emergency_generation_.load();
        const bool emergency_continuation =
            pending_search_emergency_continuation_.load();
        const auto emergency_code = pending_search_emergency_code_.load();

        search_worker_done_.store(false);
        search_busy_.store(false);

        const std::size_t previous_result_count = search_model_.results().size();
        bool applied = false;
        bool applied_continuation = false;
        if (have_result) {
            applied_continuation = continuation;
            if (result.page) {
                applied = continuation
                    ? search_model_.apply_continuation(generation, *result.page)
                    : search_model_.apply_first_page(generation, *result.page);
            } else {
                auto code = result.error_code;
                if (code == ttnx::core::SearchErrorCode::None) {
                    code = ttnx::core::SearchErrorCode::UnsupportedResponse;
                }
                applied = search_model_.fail(generation, code);
            }
        } else if (have_emergency) {
            applied_continuation = emergency_continuation;
            applied = search_model_.fail(emergency_generation, emergency_code);
        }

        refresh_footer();
        if (!applied) {
            refresh_search_page(false);
            return;
        }

        if (applied_continuation) {
            // SearchModel already deduped by stable identity. Keep at most a
            // bounded number of Borealis result Views alive on Switch. Small
            // result sets still append in place; once the hardware UI budget is
            // exceeded, rebuild only the newest window while the model retains
            // every loaded result.
            const std::size_t new_total = search_model_.results().size();
            const std::size_t added = new_total >= previous_result_count
                ? new_total - previous_result_count
                : 0;

            // Choose the first newly-added result that will actually exist in
            // the bounded hardware view. With normal YouTube page sizes this is
            // exactly results[previous_result_count]. The max() also stays safe
            // if a future response adds more than the 40-row render window.
            std::string first_new_visible_identity;
            if (added > 0) {
                const std::size_t rendered_start = new_total > kMaxRenderedSearchResults
                    ? new_total - kMaxRenderedSearchResults
                    : 0;
                const std::size_t focus_index =
                    std::max(previous_result_count, rendered_start);
                if (focus_index < new_total) {
                    first_new_visible_identity = ttnx::core::browse_result_identity(
                        search_model_.results()[focus_index]);
                }
            }

            if (search_detail_rows_.size() + added > kMaxRenderedSearchResults) {
                rebuild_search_results();
            } else {
                (void)append_search_results_from(previous_result_count);
            }
            refresh_search_inline_details();
            refresh_search_page(false, search_model_.end_of_results());

            // Real-hardware testing at 57eac3c proved the pixel-offset
            // ScrollingFrame fix keeps pagination stable through 156 loaded
            // results, but leaving focus on Load more forces the user to scroll
            // upward manually to discover what was appended. Hand focus to the
            // first newly-rendered result on the next post-layout tick instead.
            if (!first_new_visible_identity.empty()) {
                search_post_load_focus_identity_ =
                    std::move(first_new_visible_identity);
                search_scroll_reflow_pending_ = false;
            } else if (search_scroll_) {
                search_scroll_reflow_pending_ = true;
            }
        } else {
            refresh_search_page(true);
        }
    }

    void launch_search_worker(
        std::uint64_t generation,
        std::string query,
        bool continuation = false) {
        if (!guest_session_ || !guest_session_->usable()) return;
        if (search_worker_.joinable()) search_worker_.join();

        search_busy_.store(true);
        search_worker_done_.store(false);
        refresh_footer();
        refresh_search_page(false);

        try {
            const auto session = *guest_session_;
            search_worker_ = std::thread(
                [this, generation, query = std::move(query), session, continuation] {
                    ttnx::youtube::SearchWorkerStage stage =
                        ttnx::youtube::SearchWorkerStage::BuildingRequest;
                    search_worker_stage_.store(stage);
                    auto protected_result = ttnx::youtube::protect_search_worker_operation(
                        stage,
                        [&] {
                            stage = ttnx::youtube::SearchWorkerStage::SendingRequest;
                            search_worker_stage_.store(stage);
                            auto result = continuation
                                ? search_flow_.next(network_client_, session)
                                : search_flow_.begin(network_client_, session, query);
                            stage = ttnx::youtube::SearchWorkerStage::Normalizing;
                            search_worker_stage_.store(stage);
                            return result;
                        },
                        [this, continuation] {
                            // A first-page exception must not leave a half-active
                            // flow. A continuation exception keeps the current token
                            // so the explicit Retry Load more action remains valid.
                            if (!continuation) search_flow_.reset();
                        });
                    search_worker_stage_.store(protected_result.failure_stage);
                    publish_search_result(
                        generation,
                        continuation,
                        std::move(protected_result.result));
                    if (!protected_result.caught_exception) {
                        search_worker_stage_.store(ttnx::youtube::SearchWorkerStage::Complete);
                    }
                });
        } catch (const std::bad_alloc&) {
            search_busy_.store(false);
            search_worker_done_.store(false);
            const bool applied = search_model_.fail(
                generation, ttnx::core::SearchErrorCode::MemoryPressure);
            refresh_footer();
            refresh_search_page(false);
            (void)applied;
        } catch (...) {
            search_busy_.store(false);
            search_worker_done_.store(false);
            const bool applied = search_model_.fail(
                generation, ttnx::core::SearchErrorCode::InternalFailure);
            refresh_footer();
            refresh_search_page(false);
            (void)applied;
        }
    }


#ifdef TTNX_M3_INTEGRATED
    // Do not silently retain a completed or partial prior video. The
    // on-disk media folder is always throwaway, never watch history.
    static bool discard_m3_session_media() noexcept {
        const bool mp4=ttnx::switch_app::SwitchMediaCacheSink::discard_session_media();
        const bool hls=ttnx::switch_app::SwitchHlsCacheSink::discard_session_media();
        return mp4 && hls;
    }
    void publish_m3_feedback_on_ui(const std::string& video_id) {
        // Global status is above Search results and offscreen when Play is
        // focused. Put a diagnostic next to the actual selected video.
        if (m3_status_label_) m3_status_label_->setText(media_feedback_);
        for (auto& row : search_detail_rows_) {
            if (row.play_status && row.identity == "video:" + video_id) {
                row.play_status->setText(media_feedback_);
            }
        }
        refresh_search_inline_details();
        refresh_footer();
    }

#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
    // Explicit hardware-lab build only. Normal M3 Search and Library playback
    // never calls this function. Worker owns HTTPS setup and validates the
    // HLS plan; UI receives only the shared, verified local SD epoch.
    void start_m4_native_video(ttnx::core::ContentItem verified) {
        if(media_busy_.load() || !guest_session_ || !guest_session_->usable() ||
           verified.kind!=ttnx::core::ContentKind::Video ||
           !ttnx::core::is_valid_video_id(verified.id) ||
           ttnx::core::should_hide(verified)) {
            media_feedback_="M4: verified normal guest video and idle network required.";
            publish_m3_feedback_on_ui(verified.id);
            return;
        }
        // A separate Search result, not a random 11-character video ID, is
        // required to start the restricted native media/CDN experiment.
        bool in_search=false;
        for(const auto& result:search_model_.results()) {
            if(result.kind==ttnx::core::BrowseResultKind::Video &&
               result.id==verified.id && !result.live && !result.upcoming) {
                in_search=true;break;
            }
        }
        if(!in_search) {
            media_feedback_="M4: only reviewed normal Search results may stream.";
            publish_m3_feedback_on_ui(verified.id);
            return;
        }
        if(media_worker_.joinable())media_worker_.join();
        if(!discard_m3_session_media()) {
            media_feedback_="M4: previous disposable media could not be cleared.";
            publish_m3_feedback_on_ui(verified.id);
            return;
        }
        active_m3_video_id_=verified.id;
        active_m3_resume_seconds_=0; // never seek an unprimed live ring
        (void)ttnx::switch_app::M3Diagnostics::begin_session();
        media_cancel_requested_.store(false);
        media_done_.store(false);
        media_busy_.store(true);
        media_step_.store(6);
        media_feedback_="M4: locating verified H264/AAC VOD playlist...";
        publish_m3_feedback_on_ui(verified.id);
        const auto guest=*guest_session_;
        try {
            media_worker_=std::thread([this,verified=std::move(verified),guest] {
                std::shared_ptr<ttnx::playback::RollingStreamEpoch> epoch;
                std::string feedback="M4: approved muxed H264/AAC HLS unavailable.";
                bool ready=false;
                try {
                    ttnx::switch_app::LibnxHlsPlaylistTransport manifests;
                    if(manifests.ready() && !media_cancel_requested_.load()) {
                        ttnx::playback::HlsSourceProbe probe(network_client_,manifests,guest);
                        const auto plan=probe.inspect(verified.id,720,60);
                        // Unlike M3's separate MP4/AAC fallback, this first
                        // M4 transport accepts ONLY a muxed H264/AAC MPEG-TS
                        // playlist. YouTube can legitimately lack that VOD
                        // representation even when separate MP4 plays.
                        ttnx::switch_app::M3Diagnostics::record(
                            ttnx::switch_app::M3Stage::Hls,
                            ttnx::switch_app::M3Event::Status,
                            6,plan?1:0,static_cast<int>(probe.last_error()));
                        if(!plan) {
                            feedback=probe.last_error()==
                                ttnx::playback::HlsProbeError::VariantUnavailable
                                ?"M4: no muxed H264/AAC HLS; separate video/audio streaming is not implemented."
                                :std::string("M4 source: ")+
                                     ttnx::playback::describe_hls_probe_failure(
                                         probe.last_error());
                        }
                        if(plan && !media_cancel_requested_.load()) {
                            epoch=ttnx::switch_app::create_native_ring_epoch(*plan,verified);
                            if(epoch && epoch->start()) {
                                const auto deadline=std::chrono::steady_clock::now()+
                                    std::chrono::seconds(35);
                                while(!media_cancel_requested_.load() &&
                                      std::chrono::steady_clock::now()<deadline) {
                                    const auto state=epoch->snapshot();
                                    if(epoch->ring_for_decoder()) {
                                        ready=true;break;
                                    }
                                    if(state.phase==ttnx::playback::RollingStreamEpoch::Phase::Failed ||
                                       state.phase==ttnx::playback::RollingStreamEpoch::Phase::StopRequested ||
                                       state.phase==ttnx::playback::RollingStreamEpoch::Phase::Joined)
                                        break;
                                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                                }
                                feedback=ready?"M4: verified H264/AAC prefix ready for native decoder.":
                                    "M4: no verified playable prefix within bounded startup window.";
                            } else feedback="M4: verified native stream could not start.";
                        }
                    }
                } catch(...) {
                    feedback="M4: native HLS source preparation failed.";
                }
                if(media_cancel_requested_.load()) {
                    ready=false;
                    feedback="M4: cancelled before the player opened.";
                }
                if(!ready && epoch) {
                    epoch->stop_and_join();
                    (void)epoch->cleanup();
                    epoch.reset();
                }
                {
                    std::lock_guard<std::mutex> lock(media_mutex_);
                    pending_media_success_=ready;
                    pending_media_hls_=false;
                    pending_media_adaptive_=false;
                    pending_media_feedback_=std::move(feedback);
                    pending_m4_epoch_=std::move(epoch);
                }
                media_done_.store(true);
            });
        } catch(...) {
            media_busy_.store(false);
            media_feedback_="M4: unable to create native media preparation worker.";
            publish_m3_feedback_on_ui(active_m3_video_id_);
        }
    }
#endif

    void start_m3_video(std::string normal_video_id,
                        std::uint32_t resume_seconds=0) {
        if (media_busy_.load() || !guest_session_ || !guest_session_->usable()) {
            media_feedback_ = "Connect to guest YouTube first, or wait for current download.";
            publish_m3_feedback_on_ui(normal_video_id);
            return;
        }
        if (!ttnx::core::is_valid_video_id(normal_video_id)) {
            media_feedback_ = "Invalid normal video ID (Shorts are forbidden).";
            publish_m3_feedback_on_ui(normal_video_id);
            return;
        }
        if (media_worker_.joinable()) media_worker_.join();
        if(!discard_m3_session_media()) {
            media_feedback_="M3: cannot remove prior temporary media from SD. "
                            "Free the app cache before playing.";
            publish_m3_feedback_on_ui(normal_video_id);
            return;
        }
        active_m3_video_id_ = normal_video_id;
        active_m3_resume_seconds_ = resume_seconds;
        const bool diagnostic_recording=ttnx::switch_app::M3Diagnostics::begin_session();
        (void)diagnostic_recording;
        const auto guest = *guest_session_;
        media_cancel_requested_.store(false);
        media_done_.store(false);
        media_busy_.store(true);
        media_step_.store(0);
        media_progress_bytes_.store(0);
        media_progress_total_.store(0);
        media_parallel_adaptive_.store(false);
        media_audio_bytes_.store(0);
        media_audio_total_.store(0);
        media_video_bytes_.store(0);
        media_video_total_.store(0);
        media_segments_done_.store(0);
        media_segments_total_.store(0);
        media_external_aac_phase_.store(false);
        media_buffer_ready_.store(false);
        media_buffer_ms_.store(0);
        media_feedback_ = "M3: locating approved guest HLS video/audio streams...";
        publish_m3_feedback_on_ui(normal_video_id);

        try {
            media_worker_ = std::thread([this, normal_video_id, guest] {
                std::string feedback="M3 HLS playback source unavailable.";
                bool completed=false;
                bool completed_hls=false;
                bool completed_adaptive=false;
                ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Session,ttnx::switch_app::M3Event::Ready,0,0,0);
                try {
                    ttnx::switch_app::LibnxHlsPlaylistTransport manifests;
                    ttnx::switch_app::LibnxHlsSegmentTransport segments;
                    ttnx::switch_app::LibnxMediaChunkTransport audio_transport;
                    ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Source,ttnx::switch_app::M3Event::Begin,manifests.ready() ? 1 : 0,segments.ready() ? 1 : 0,audio_transport.ready() ? 1 : 0);
                    if (!manifests.ready() || !segments.ready() ||
                        !audio_transport.ready()) {
                        feedback="M3: native HLS TLS service unavailable.";
                    } else if(media_cancel_requested_.load()) {
                        feedback="M3: video request cancelled.";
                    } else {
                        // M2 Search retains its accepted anonymous WEB guest
                        // session. A separate reviewed player identity reuses
                        // that visitor only for HLS source discovery.
                        ttnx::playback::HlsSourceProbe probe(network_client_,manifests,guest);
                        const auto source=probe.inspect(normal_video_id,720,30);
                        ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Hls,ttnx::switch_app::M3Event::Status,source ? 1 : 0,static_cast<int>(probe.last_error()),0);
                        if (!source) {
                            feedback=ttnx::playback::describe_hls_probe_failure(probe.last_error());
                            // Independent safe MP4 fallback: never bypass
                            // YouTube guest playability, URL policy, Shorts,
                            // DRM, H.264/AAC requirements or cache limits.
                            if(ttnx::playback::permits_muxed_mp4_fallback(
                                   probe.last_error()) &&
                               !media_cancel_requested_.load()) {
                                const std::string hls_reason=
                                    ttnx::playback::describe_hls_probe_failure(
                                        probe.last_error());
                                ttnx::playback::GuestDirectExtractor extractor(
                                    network_client_,guest,true);
                                const auto resolved=extractor.resolve(normal_video_id);
                                ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Direct,ttnx::switch_app::M3Event::Status,resolved ? 1 : 0,static_cast<int>(extractor.last_failure()),0);
                                if(!resolved) {
                                    feedback=hls_reason+" | Direct: "+
                                        ttnx::playback::describe_direct_player_failure(
                                            extractor.last_failure());
                                } else {
                                    ttnx::playback::QualityPolicy policy;
                                    policy.max_height=720;policy.max_fps=30;
                                    policy.permit_vp9=false;policy.prefer_adaptive=false;
                                    auto muxed=*resolved;
                                    muxed.audio.clear();
                                    for(auto& fmt:muxed.video)
                                        if(fmt.muxed_audio!=ttnx::playback::AudioCodec::Aac)
                                            fmt.codec=ttnx::playback::VideoCodec::Unknown;
                                    const auto single=ttnx::playback::select_streams(
                                        muxed,policy);
                                    policy.prefer_adaptive=true;
                                    const auto paired=ttnx::playback::select_streams(
                                        *resolved,policy);
                                    const bool have_muxed=single.streams &&
                                        !single.streams->adaptive &&
                                        single.streams->video_codec==
                                            ttnx::playback::VideoCodec::H264 &&
                                        single.streams->audio_codec==
                                            ttnx::playback::AudioCodec::Aac;
                                    const bool have_pair=paired.streams &&
                                        paired.streams->adaptive &&
                                        paired.streams->video_codec==
                                            ttnx::playback::VideoCodec::H264 &&
                                        paired.streams->audio_codec==
                                            ttnx::playback::AudioCodec::Aac;
                                    ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Direct,ttnx::switch_app::M3Event::Selected,have_muxed ? 1 : 0,have_pair ? 1 : 0,static_cast<std::int64_t>(resolved->video.size())*1000+static_cast<std::int64_t>(resolved->audio.size()));
                                    using Cache=ttnx::switch_app::SwitchMediaCacheSink;
                                    ttnx::playback::AdaptiveMediaBudget pair_budget;
                                    std::atomic_bool pair_too_large{false};
                                    ttnx::playback::MediaChunkReader reader(audio_transport);
                                    auto download=[&](const std::string& url,Cache& sink,
                                                      int phase,
                                                      ttnx::playback::MediaChunkReader& chunks,
                                                      std::atomic_bool* sibling_failed=nullptr)->bool {
                                        ttnx::playback::MediaDownloadJob job(chunks,sink);
                                        ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Transfer,ttnx::switch_app::M3Event::Start,phase,0,0);
                                        if(!job.start(url)){
                                            if(sibling_failed)sibling_failed->store(true);
                                            ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Cache,ttnx::switch_app::M3Event::Fail,phase,static_cast<int>(sink.last_storage_error()),sink.last_storage_errno());
                                            ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Transfer,ttnx::switch_app::M3Event::Fail,phase,static_cast<int>(job.error()),0);
                                            return false;
                                        }
                                        // start() does not know Content-Range
                                        // total yet. It becomes known after
                                        // the first bounded pump()/HTTP 206.
                                        media_step_.store(phase);
                                        bool budget_registered=false;
                                        std::uint64_t last_reported_mib=0;
                                        while(job.state()==
                                              ttnx::playback::MediaDownloadState::Fetching &&
                                              !media_cancel_requested_.load() &&
                                              !(sibling_failed && sibling_failed->load())) {
                                            (void)job.pump();
                                            if((phase==4 || phase==5) &&
                                               !budget_registered &&
                                               job.total_bytes()!=0) {
                                                budget_registered=true;
                                                if(!pair_budget.admit(job.total_bytes())) {
                                                    pair_too_large.store(true);
                                                    if(sibling_failed)sibling_failed->store(true);
                                                    ttnx::switch_app::M3Diagnostics::record(
                                                        ttnx::switch_app::M3Stage::Cache,
                                                        ttnx::switch_app::M3Event::Refused,
                                                        phase,
                                                        static_cast<std::int64_t>(job.total_bytes()),
                                                        static_cast<std::int64_t>(pair_budget.reserved()));
                                                    job.cancel();
                                                    return false;
                                                }
                                            }
                                            if(job.state()==ttnx::playback::MediaDownloadState::Error &&
                                               sibling_failed)sibling_failed->store(true);
                                            if(job.retry_delay_ms()) {
                                                ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Transfer,ttnx::switch_app::M3Event::Retry,phase,job.last_http_status(),static_cast<int>(job.last_chunk_failure()));
                                            }
                                            const auto mib=job.bytes_received()/(4ULL*1024*1024);
                                            if(mib>last_reported_mib) {
                                                last_reported_mib=mib;
                                                ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Transfer,ttnx::switch_app::M3Event::Progress,phase,static_cast<std::int64_t>(job.bytes_received()),static_cast<std::int64_t>(job.total_bytes()));
                                            }
                                            if(job.retry_delay_ms() &&
                                               !media_cancel_requested_.load())
                                                std::this_thread::sleep_for(
                                                    std::chrono::milliseconds(
                                                        job.retry_delay_ms()));
                                            if(phase==4) {
                                                media_audio_bytes_.store(job.bytes_received());
                                                media_audio_total_.store(job.total_bytes());
                                            } else if(phase==5) {
                                                media_video_bytes_.store(job.bytes_received());
                                                media_video_total_.store(job.total_bytes());
                                            }
                                            if(!media_parallel_adaptive_.load()) {
                                                media_progress_bytes_.store(job.bytes_received());
                                                media_progress_total_.store(job.total_bytes());
                                            }
                                        }
                                        if(media_cancel_requested_.load() ||
                                           (sibling_failed && sibling_failed->load() &&
                                            job.state()==ttnx::playback::MediaDownloadState::Fetching)) {
                                            job.cancel();
                                            ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Transfer,ttnx::switch_app::M3Event::Cancel,phase,static_cast<std::int64_t>(job.bytes_received()),0);
                                            return false;
                                        }
                                        if(phase==4) {
                                            media_audio_bytes_.store(job.bytes_received());
                                            media_audio_total_.store(job.total_bytes());
                                        } else if(phase==5) {
                                            media_video_bytes_.store(job.bytes_received());
                                            media_video_total_.store(job.total_bytes());
                                        }
                                        ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Cache,job.state()==ttnx::playback::MediaDownloadState::Complete?ttnx::switch_app::M3Event::Pass:ttnx::switch_app::M3Event::Fail,phase,static_cast<int>(sink.last_storage_error()),sink.last_storage_errno());
                                        ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Transfer,ttnx::switch_app::M3Event::End,phase,static_cast<int>(job.error())*1000+job.last_http_status(),static_cast<std::int64_t>(job.bytes_received()));
                                        ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Transfer,ttnx::switch_app::M3Event::Status,phase,static_cast<int>(job.last_chunk_failure()),static_cast<std::int64_t>(job.total_bytes()));
                                        ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Transfer,ttnx::switch_app::M3Event::Redirect,phase,job.redirect_hops_total(),0);
                                        const bool good=job.state()==
                                            ttnx::playback::MediaDownloadState::Complete;
                                        if(!good && sibling_failed)sibling_failed->store(true);
                                        return good;
                                    };
                                    if(have_muxed){
                                        Cache movie;
                                        completed=download(single.streams->video_url,
                                                           movie,3,reader);
                                        feedback=media_cancel_requested_.load()
                                            ?"M3 download cancelled."
                                            :completed?"M3: verified muxed MP4 ready."
                                            :"M3 muxed MP4 signed range or SD error.";
                                    } else if(have_pair){
                                        Cache::invalidate_adaptive_pair();
                                        Cache audio(Cache::Slot::AdaptiveAudio);
                                        Cache video(Cache::Slot::AdaptiveVideo);
                                        bool audio_ok=false,video_ok=false;
                                        std::atomic_bool sibling_failed{false};
                                        // Both media URLs came from the same approved
                                        // guest response. Keep independent strict
                                        // HTTPS transports/byte-range readers and
                                        // distinct SD sinks, then join before commit.
                                        // Two simultaneous downloads reduce startup
                                        // on links where latency is the limiting factor.
                                        ttnx::switch_app::LibnxMediaChunkTransport video_transport;
                                        if(video_transport.ready()) {
                                            ttnx::playback::MediaChunkReader video_reader(video_transport);
                                            std::future<bool> video_result;
                                            try {
                                                media_parallel_adaptive_.store(true);
                                                video_result=std::async(std::launch::async,[&] {
                                                    return download(paired.streams->video_url,
                                                                    video,5,video_reader,
                                                                    &sibling_failed);
                                                });
                                            } catch (...) {
                                                media_parallel_adaptive_.store(false);
                                            }
                                            if(video_result.valid()) {
                                                audio_ok=download(paired.streams->audio_url,
                                                                  audio,4,reader,
                                                                  &sibling_failed);
                                                video_ok=video_result.get();
                                                media_parallel_adaptive_.store(false);
                                            } else {
                                                audio_ok=download(paired.streams->audio_url,
                                                                  audio,4,reader);
                                                if(audio_ok)video_ok=download(
                                                    paired.streams->video_url,video,5,reader);
                                            }
                                        } else {
                                            audio_ok=download(paired.streams->audio_url,
                                                              audio,4,reader);
                                            if(audio_ok)video_ok=download(
                                                paired.streams->video_url,video,5,reader);
                                        }
                                        if(audio_ok && video_ok &&
                                           Cache::publish_adaptive_pair()) {
                                            ttnx::switch_app::M3Diagnostics::record(
                                                ttnx::switch_app::M3Stage::Cache,
                                                ttnx::switch_app::M3Event::Pass,1,0,0);
                                            completed=true;
                                            completed_adaptive=true;
                                            feedback="M3: video MP4 + separate AAC verified.";
                                        } else {
                                            ttnx::switch_app::M3Diagnostics::record(
                                                ttnx::switch_app::M3Stage::Cache,
                                                ttnx::switch_app::M3Event::Fail,1,
                                                audio_ok?1:0,video_ok?1:0);
                                            feedback=media_cancel_requested_.load()
                                                ?"M3 adaptive download cancelled."
                                                :pair_too_large.load()?"M3: full download over 60MB SD limit; requires streaming."
                                                :!audio_ok?"M3 adaptive AAC range download failed."
                                                :"M3 adaptive video transfer/MP4 validation failed.";
                                        }
                                    } else feedback=hls_reason+
                                        " | no safe muxed/separate H.264+AAC; "+
                                        std::to_string(resolved->video.size())+
                                        " video, "+
                                        std::to_string(resolved->audio.size())+
                                        " audio candidates.";
                                }
                            }
                        } else if(media_cancel_requested_.load()) {
                            feedback="M3: video request cancelled.";
                        } else {
                            ttnx::playback::HlsSegmentReader reader(segments);
                            ttnx::playback::MediaChunkReader audio_reader(audio_transport);
                            ttnx::switch_app::SwitchHlsCacheSink cache;
                            ttnx::playback::HlsCacheJob job(reader,cache,&audio_reader);
                            if(!job.start(*source)) {
                                ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Cache,ttnx::switch_app::M3Event::Fail,static_cast<int>(job.error()),0,0);
                                feedback=ttnx::playback::describe_hls_cache_failure(job.error());
                            } else {
                                media_step_.store(2);
                                media_segments_total_.store(
                                    static_cast<unsigned>(job.total_segments()));
                                while(job.state()==ttnx::playback::HlsCacheState::Fetching &&
                                      !media_cancel_requested_.load()) {
                                    // Exactly one bounded HTTPS segment per
                                    // loop: UI remains on the other thread.
                                    (void)job.pump();
                                    // Retry policy runs only on the media
                                    // worker, never on the Borealis UI thread.
                                    if(job.retry_delay_ms() &&
                                       !media_cancel_requested_.load())
                                        std::this_thread::sleep_for(
                                            std::chrono::milliseconds(
                                                job.retry_delay_ms()));
                                    media_progress_bytes_.store(job.downloaded_bytes());
                                    media_segments_done_.store(
                                        static_cast<unsigned>(job.segments_completed()));
                                    media_external_aac_phase_.store(
                                        job.downloading_external_aac());
                                    media_buffer_ready_.store(
                                        job.startup_event_staged());
                                    media_buffer_ms_.store(
                                        job.startup_buffer_ms());
                                }
                                if(media_cancel_requested_.load()) {
                                    job.cancel();
                                    feedback="M3 HLS transfer cancelled.";
                                } else if(job.state()==ttnx::playback::HlsCacheState::Ready) {
                                    ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Cache,ttnx::switch_app::M3Event::Pass,2,static_cast<std::int64_t>(job.downloaded_bytes()),static_cast<std::int64_t>(job.segments_completed()));
                                    completed=true;
                                    completed_hls=true;
                                    feedback="M3: complete HLS video/AAC locally verified.";
                                } else {
                                    ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Cache,ttnx::switch_app::M3Event::Fail,static_cast<int>(job.error()),static_cast<std::int64_t>(job.downloaded_bytes()),0);
                                    feedback=ttnx::playback::describe_hls_cache_failure(job.error());
                                }
                            }
                        }
                    }
                } catch (...) {
                    ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Session,ttnx::switch_app::M3Event::Error,9,0,0);
                    feedback="M3 HLS preparation failed (no ad web-player fallback).";
                }
                ttnx::switch_app::M3Diagnostics::record(ttnx::switch_app::M3Stage::Session,ttnx::switch_app::M3Event::End,completed ? 1 : 0,completed_hls ? 1 : (completed_adaptive ? 3 : 2),media_step_.load());
                {
                    std::lock_guard<std::mutex> guard(media_mutex_);
                    pending_media_success_=completed;
                    pending_media_hls_=completed && completed_hls;
                    pending_media_adaptive_=completed && completed_adaptive;
                    pending_media_feedback_=std::move(feedback);
                }
                media_done_.store(true);
            });
        } catch (...) {
            media_busy_.store(false);
            media_feedback_ = "M3: unable to start video preparation thread.";
            publish_m3_feedback_on_ui(normal_video_id);
        }
    }

    void pump_m3_video() {
        if (!media_done_.load()) {
            if (!media_busy_.load()) return;
            const auto now = std::chrono::steady_clock::now();
            if (now - media_last_ui_update_ < std::chrono::milliseconds(700)) return;
            media_last_ui_update_ = now;
            if(media_parallel_adaptive_.load()) {
                media_feedback_="M3 downloading AAC "+
                    std::to_string(media_audio_bytes_.load()/1024)+" KiB";
                if(media_audio_total_.load())media_feedback_+=" / "+
                    std::to_string(media_audio_total_.load()/1024)+" KiB";
                media_feedback_+=" + H.264 "+
                    std::to_string(media_video_bytes_.load()/1024)+" KiB";
                if(media_video_total_.load())media_feedback_+=" / "+
                    std::to_string(media_video_total_.load()/1024)+" KiB";
                publish_m3_feedback_on_ui(active_m3_video_id_);
            } else if(media_step_.load()==3 || media_step_.load()==4 ||
               media_step_.load()==5) {
                const auto phase=media_step_.load();
                const auto current=media_progress_bytes_.load();
                const auto total=media_progress_total_.load();
                media_feedback_=phase==3?"M3 muxed MP4: ":
                                phase==4?"M3 separate AAC: ":
                                         "M3 adaptive H.264 video: ";
                media_feedback_+=
                    std::to_string(current/1024)+" KiB cached";
                if(total)media_feedback_+=" / "+std::to_string(total/1024)+" KiB";
                publish_m3_feedback_on_ui(active_m3_video_id_);
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
            } else if(media_step_.load()==6) {
                media_feedback_="M4: preparing first verified SD segment (no full-video cache)";
                publish_m3_feedback_on_ui(active_m3_video_id_);
#endif
            } else if (media_step_.load() == 2) {
                const auto current = media_progress_bytes_.load();
                const auto total = media_progress_total_.load();
                const auto done=media_segments_done_.load();
                const auto count=media_segments_total_.load();
                media_feedback_ = media_external_aac_phase_.load()
                    ? "M3 caching verified external AAC"
                    : "M3 HLS media segments: " + std::to_string(done) +
                      "/" + std::to_string(count);
                if(media_buffer_ready_.load()) {
                    media_feedback_ += " | " +
                        std::to_string(media_buffer_ms_.load()/1000) +
                        "s video buffered (finishing cache)";
                }
                media_feedback_ += " | " + std::to_string(current / 1024) +
                                   " KiB downloaded";
                if (total) media_feedback_ += " / " + std::to_string(total / 1024) + " KiB";
                publish_m3_feedback_on_ui(active_m3_video_id_);
            }
            return;
        }
        if (media_worker_.joinable()) media_worker_.join();
        bool success = false;
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
        std::shared_ptr<ttnx::playback::RollingStreamEpoch> pending_ring;
#endif
        {
            std::lock_guard<std::mutex> guard(media_mutex_);
            success = pending_media_success_;
            media_feedback_ = std::move(pending_media_feedback_);
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
            pending_ring=std::move(pending_m4_epoch_);
#endif
        }
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(pending_ring && media_cancel_requested_.load()) {
            pending_ring->stop_and_join();
            (void)pending_ring->cleanup();
            pending_ring.reset();success=false;
            media_feedback_="M4: cancelled before decoder handoff.";
        }
#endif
        media_busy_.store(false);
        media_done_.store(false);
        if(!success) {
            // Failed/cancelled transfers never leave downloaded AAC or an
            // incomplete video lingering until a future launch.
            if(!discard_m3_session_media())
                media_feedback_+=" [SD temporary-media cleanup failed]";
        }
        if(ttnx::switch_app::M3Diagnostics::available()) {
            media_feedback_+=" [SD log: m3-playback.log]";
        }
        publish_m3_feedback_on_ui(active_m3_video_id_);
#if defined(TTNX_M4_NATIVE_TEST_UI) && defined(TTNX_M4_RING_STREAM_CB_PROBE)
        if(success && pending_ring) {
            ttnx::switch_app::open_m4_native_ring_candidate(std::move(pending_ring));
            return;
        }
#endif
        if(success && pending_media_hls_)
            ttnx::switch_app::open_m3_completed_hls(
                active_m3_video_id_,active_m3_resume_seconds_);
        else if(success && pending_media_adaptive_)
            ttnx::switch_app::open_m3_completed_adaptive(
                active_m3_video_id_,active_m3_resume_seconds_);
        else if(success)
            ttnx::switch_app::open_m3_completed_media(
                active_m3_video_id_,active_m3_resume_seconds_);
    }
#endif

    void start_search() {
        if (search_busy_.load()) return;
        if (home_busy_.load() || channel_busy_.load() || network_busy_.load()) {
            refresh_search_page(false);
            return;
        }
        if (query_.empty() || !guest_session_ || !guest_session_->usable()) {
            refresh_search_page(false);
            return;
        }

        const auto generation = search_model_.begin_query(query_);
        refresh_search_page(true);
        launch_search_worker(generation, query_);
    }


    void retry_search_request() {
        if (search_busy_.load() || home_busy_.load() || channel_busy_.load() || network_busy_.load()) return;
        if (!guest_session_ || !guest_session_->usable()) {
            refresh_search_page(false);
            return;
        }

        const auto generation = search_model_.generation();
        if (!search_model_.begin_retry(generation)) return;
        refresh_search_page(false);
        launch_search_worker(generation, search_model_.query());
    }

    void start_load_more() {
        if (search_busy_.load() || home_busy_.load() || channel_busy_.load() || network_busy_.load()) return;
        if (!guest_session_ || !guest_session_->usable()) {
            refresh_search_page(false);
            return;
        }

        const auto generation = search_model_.generation();
        const bool retry_continuation =
            search_model_.state() == ttnx::core::SearchViewState::Error &&
            search_model_.retryable_failure() &&
            search_model_.error_code() == ttnx::core::SearchErrorCode::ContinuationFailure;

        if (retry_continuation) {
            if (!search_model_.begin_retry(generation)) return;
        } else if (!search_model_.begin_load_more(generation)) {
            return;
        }

        // Keep all existing result Views alive while the continuation runs.
        refresh_search_page(false);
        launch_search_worker(generation, {}, true);
    }

    void publish_channel_result(
        std::uint64_t generation,
        ttnx::youtube::GuestChannelResult result) noexcept {
        try {
            std::lock_guard<std::mutex> lock(channel_mutex_);
            pending_channel_generation_ = generation;
            pending_channel_ = std::move(result);
            pending_channel_result_ = true;
        } catch (...) {
            pending_channel_result_ = false;
        }
        channel_worker_done_.store(true);
    }

    void pump_channel_result() {
        if (!channel_worker_done_.load()) return;
        if (channel_worker_.joinable()) channel_worker_.join();

        ttnx::youtube::GuestChannelResult result;
        std::uint64_t generation = 0;
        bool have_result = false;
        {
            std::lock_guard<std::mutex> lock(channel_mutex_);
            if (pending_channel_result_) {
                generation = pending_channel_generation_;
                result = std::move(pending_channel_);
                pending_channel_result_ = false;
                have_result = true;
            }
        }
        channel_worker_done_.store(false);
        channel_busy_.store(false);

        bool applied = false;
        if (have_result) {
            channel_diagnostics_ = std::move(result.diagnostics);
            if (result.page) {
                applied = channel_model_.apply_page(generation, std::move(*result.page));
            } else {
                applied = channel_model_.fail(
                    generation,
                    result.error.empty()
                        ? "Channel could not be loaded safely."
                        : std::move(result.error));
            }
        } else {
            channel_diagnostics_.clear();
            applied = channel_model_.fail(
                generation,
                "Channel failed safely because of an internal error.");
        }

        refresh_footer();
        if (applied && active_channel_activity_) {
            active_channel_activity_->set_diagnostics(channel_diagnostics_);
            active_channel_activity_->refresh(true);
        }
    }

    void launch_channel_worker(std::uint64_t generation, std::string channel_id) {
        if (!guest_session_ || !guest_session_->usable()) return;
        if (channel_worker_.joinable()) channel_worker_.join();
        channel_busy_.store(true);
        channel_worker_done_.store(false);
        refresh_footer();

        try {
            const auto session = *guest_session_;
            channel_worker_ = std::thread([this, generation, channel_id = std::move(channel_id), session] {
                try {
                    ttnx::core::GuestRequest request;
                    request.surface = ttnx::core::BrowseSurface::Channel;
                    request.value = channel_id;
                    publish_channel_result(
                        generation,
                        ttnx::youtube::execute_guest_channel(
                            network_client_, session, request));
                } catch (const std::bad_alloc&) {
                    ttnx::youtube::GuestChannelResult failure;
                    failure.error = "Channel failed safely because there was not enough available memory.";
                    publish_channel_result(generation, std::move(failure));
                } catch (const std::exception&) {
                    ttnx::youtube::GuestChannelResult failure;
                    failure.error = "Channel failed safely because of an internal error.";
                    publish_channel_result(generation, std::move(failure));
                } catch (...) {
                    ttnx::youtube::GuestChannelResult failure;
                    failure.error = "Channel failed safely because of an internal error.";
                    publish_channel_result(generation, std::move(failure));
                }
            });
        } catch (const std::bad_alloc&) {
            channel_busy_.store(false);
            channel_worker_done_.store(false);
            (void)channel_model_.fail(
                generation,
                "Could not start Channel because there was not enough available memory.");
            if (active_channel_activity_) active_channel_activity_->refresh(false);
            refresh_footer();
        } catch (...) {
            channel_busy_.store(false);
            channel_worker_done_.store(false);
            (void)channel_model_.fail(generation, "Could not start the Channel worker thread.");
            if (active_channel_activity_) active_channel_activity_->refresh(false);
            refresh_footer();
        }
    }

    void open_channel_page(const std::string& channel_id, const std::string& channel_title) {
        if (channel_busy_.load() || active_channel_activity_ ||
            search_busy_.load() || home_busy_.load() || network_busy_.load()) return;
        if (!guest_session_ || !guest_session_->usable() ||
            !ttnx::core::is_valid_channel_id(channel_id)) return;

        ttnx::core::ChannelIdentity identity;
        identity.browse_id = channel_id;
        identity.title = channel_title;
        const auto generation = channel_model_.begin_load(std::move(identity));
        channel_diagnostics_.clear();

        auto* activity = new ChannelActivity(
            channel_model_,
            [this] {
                active_channel_activity_ = nullptr;
                // Invalidates an in-flight completion if the user backs out early.
                channel_model_.reset();
                channel_diagnostics_.clear();
            });
        active_channel_activity_ = activity;
        brls::Application::pushActivity(activity);
        launch_channel_worker(generation, channel_id);
    }

    void open_search_keyboard() {
        SwkbdConfig config{};
        if (R_FAILED(swkbdCreate(&config, 0))) return;
        swkbdConfigMakePresetDefault(&config);
        swkbdConfigSetHeaderText(&config, "Search YouTube");
        swkbdConfigSetGuideText(&config, "Enter a video, topic or channel");
        swkbdConfigSetStringLenMax(&config, 120);
        swkbdConfigSetInitialText(&config, query_.c_str());
        char text[481]{};
        const Result result = swkbdShow(&config, text, sizeof(text));
        swkbdClose(&config);
        if (R_FAILED(result)) return;

        const std::string next_query = text;
        if (next_query != query_) {
            query_ = next_query;
            // Invalidates any older completion immediately. The old worker may
            // finish normally, but its generation can no longer mutate the UI.
            search_model_.reset();
        }
        refresh_search_page(true);
    }

    brls::View* sidebar_item_for_section(ttnx::ui::RootSection section) {
        auto* sidebar = dynamic_cast<brls::Box*>(getView("brls/tab_frame/sidebar"));
        if (!sidebar || sidebar->getChildren().empty()) return nullptr;

        auto* item_box = dynamic_cast<brls::Box*>(sidebar->getChildren().front());
        if (!item_box) return nullptr;

        std::size_t wanted = ttnx::ui::kRootNavigation.size();
        for (std::size_t i = 0; i < ttnx::ui::kRootNavigation.size(); ++i) {
            if (ttnx::ui::kRootNavigation[i].section == section) {
                wanted = i;
                break;
            }
        }

        auto& items = item_box->getChildren();
        if (wanted >= items.size()) return nullptr;
        return items[wanted];
    }

    void focus_sidebar_section(ttnx::ui::RootSection section) {
        if (auto* item = sidebar_item_for_section(section)) {
            brls::Application::giveFocus(item);
            return;
        }

        // Conservative fallback for an unexpected pinned-framework layout mismatch.
        if (auto* sidebar = getView("brls/tab_frame/sidebar")) {
            brls::Application::giveFocus(sidebar);
        }
    }

    enum class LocalListKind { WatchLater, Favorite, History, Following };
    struct LocalPageEntry { std::string id; std::uint32_t resume{0}; };

    // Native Borealis page recycling: at most 20 row buttons per section,
    // independently of a thousand-entry on-disk Library. No Google API,
    // thumbnail cache, video file or second renderer is involved.
    void add_local_list(brls::Box* parent,LocalListKind kind,
                        const std::string& heading,brls::Label* feedback) {
        label(parent,heading,24);
        auto entries=std::make_shared<std::vector<LocalPageEntry>>();
        switch(kind) {
        case LocalListKind::WatchLater:
            for(const auto& id:library_.watch_later())
                entries->push_back({id,0});
            break;
        case LocalListKind::Favorite:
            for(const auto& id:library_.favorites())
                entries->push_back({id,0});
            break;
        case LocalListKind::History:
            for(const auto& e:library_.history())
                entries->push_back({e.video_id,e.resume_seconds});
            break;
        case LocalListKind::Following:
            for(const auto& id:library_.followed_channels())
                entries->push_back({id,0});
            break;
        }
        if(entries->empty()) {
            label(parent,kind==LocalListKind::Following
                ?"Follow a channel from Search to see it here."
                :"Nothing saved yet. Select an eligible video in Search.",17);
            return;
        }
        auto pager=std::make_shared<ttnx::ui::LibraryPager>(entries->size());
        auto* rows=new brls::Box(brls::Axis::COLUMN);
        parent->addView(rows);
        auto* pager_actions=new brls::Box(brls::Axis::ROW);
        pager_actions->setHeight(56);
        parent->addView(pager_actions);
        auto* previous=button(pager_actions,"First page");
        auto* next=button(pager_actions,"Next 20");
        previous->setGrow(1.0f);
        next->setGrow(1.0f);
        auto* paging_status=label(parent,"",17);
        auto render=[this,entries,pager,kind,feedback,rows,
                     previous,next,paging_status]() {
            while(!rows->getChildren().empty())
                rows->removeView(rows->getChildren().back());
            const auto [first,last]=pager->range();
            for(std::size_t index=first;index<last;++index) {
                const auto item=(*entries)[index];
                const auto text=kind==LocalListKind::Following
                    ?"Channel: "+item.id
                    :kind==LocalListKind::History
                    ?item.id+" | "+std::to_string(item.resume)+"s"
                    :"Video: "+item.id;
                auto* row=button(rows,text);
                auto removed=std::make_shared<bool>(false);
                row->registerClickAction(
                    [this,item,kind,feedback,removed](brls::View*) {
                    if(*removed) return true;
                    if(kind==LocalListKind::Following) {
                        open_channel_page(item.id,"Followed channel");
                        return true;
                    }
#ifdef TTNX_M3_INTEGRATED
                    if(!guest_session_ || !guest_session_->usable())
                        feedback->setText("Connect to guest YouTube first.");
                    else start_m3_video(item.id,
                        kind==LocalListKind::History?item.resume:0);
#else
                    feedback->setText("Playback requires integrated player.");
#endif
                    return true;
                });
                row->registerAction("Remove locally",brls::BUTTON_Y,
                    [this,item,kind,feedback,row,removed](brls::View*) {
                    if(*removed)return true;
                    refresh_library_metadata();
                    auto original=library_;
                    bool changed=false;
                    switch(kind) {
                    case LocalListKind::WatchLater:
                        changed=library_.remove_watch_later(item.id);break;
                    case LocalListKind::Favorite:
                        changed=library_.remove_favorite(item.id);break;
                    case LocalListKind::History:
                        changed=library_.erase_history(item.id);break;
                    case LocalListKind::Following:
                        changed=library_.unfollow_channel(item.id);break;
                    }
                    if(changed && persist_library_metadata()) {
                        *removed=true;
                        row->setText("Removed locally: "+item.id);
                        feedback->setText("Removed from the Switch Library.");
                    } else {
                        library_=std::move(original);
                        feedback->setText("Local removal was not saved.");
                    }
                    return true;
                });
            }
            previous->setText(pager->has_previous()
                ?"Previous 20":"First page");
            next->setText(pager->has_next()?"Next 20":"Last page");
            paging_status->setText("Page "+std::to_string(pager->page()+1)+
                " / "+std::to_string(pager->pages())+
                "  |  Y Remove locally");
        };
        previous->registerClickAction([pager,render](brls::View*) {
            if(pager->previous())render();
            return true;
        });
        next->registerClickAction([pager,render](brls::View*) {
            if(pager->next())render();
            return true;
        });
        render();
    }

    brls::View* create_page(ttnx::ui::RootSection section) {
        using ttnx::ui::RootSection;
        // Startup recovery build: use the same plain ScrollingFrame ownership
        // model as the physically accepted checkpoint. Tab switches are synchronous
        // on the UI thread, so stale UI-only pointers are cleared before creating
        // the replacement page.
        home_probe_button_ = nullptr;
        home_status_label_ = nullptr;
        home_detail_label_ = nullptr;
        home_load_button_ = nullptr;
        home_feed_status_label_ = nullptr;
        home_results_box_ = nullptr;
        home_detail_rows_.clear();
        search_query_label_ = nullptr;
        search_status_label_ = nullptr;
#ifdef TTNX_M3_INTEGRATED
        m3_status_label_ = nullptr;
#endif
        search_action_button_ = nullptr;
        search_load_more_button_ = nullptr;
        search_results_box_ = nullptr;
        search_scroll_ = nullptr;
        search_scroll_reflow_pending_ = false;
        search_post_load_focus_identity_.clear();
        search_detail_rows_.clear();
        auto* scroll = new brls::ScrollingFrame();
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setPadding(32, 36, 32, 36);
        scroll->setContentView(content);

        // Pages are destroyed on tab changes. Worker threads never capture page
        // views; all async data returns through ShellActivity state first.
        content->registerAction("Sections", brls::BUTTON_B, [this, section](brls::View*) {
            focus_sidebar_section(section);
            return true;
        });

        switch (section) {
        case RootSection::Home: {
            label(content, "Home", 34);
            label(content, "Your videos. Less clutter.", 26);
            label(content,
                  "Home is explicit for this hardware gate: TizenTube NX still makes no hidden YouTube request at startup.",
                  20);
            home_probe_button_ = button(content, network_busy_.load()
                ? "YouTube guest connection: Connecting..."
                : "Test YouTube guest connection");
            home_probe_button_->registerClickAction([this](brls::View*) {
                start_guest_connection_test();
                return true;
            });
            home_status_label_ = label(content, "Connection status: " + network_summary_, 20);
            home_detail_label_ = label(content, network_detail_, 18);

            home_load_button_ = button(content, "Load Home");
            home_load_button_->registerClickAction([this](brls::View*) {
                start_home_load();
                return true;
            });
            home_feed_status_label_ = label(content, "", 20);
            label(content,
                  "Text-only first-page Home gate. Thumbnail candidates are normalized but no remote image host is contacted.",
                  18);
            home_results_box_ = new brls::Box(brls::Axis::COLUMN);
            home_results_box_->setMarginBottom(8);
            content->addView(home_results_box_);
            label(content,
                  "No Shorts, ads, promoted or shopping renderers may enter the Home model. Home continuation is deferred until this first page passes hardware.",
                  18);
            refresh_home_page(true);
            ttnx::record_boot_event("boot-10-home-created");
            break;
        }
        case RootSection::Search: {
            search_scroll_ = scroll;
            label(content, "Search", 34);
            label(content,
                  "Live guest Search is enabled with explicit Load more. Results stay text-only while thumbnail hosts remain blocked.",
                  20);
            label(content,
                  "M2 stability note: the Switch view tree keeps only the newest 40 loaded results visible while SearchModel retains the full loaded set.",
                  18);

            auto* enter = button(content, "Enter search");
            enter->registerClickAction([this](brls::View*) {
                open_search_keyboard();
                return true;
            });

            search_query_label_ = label(content, "Query: none", 20);
            search_action_button_ = button(content, "Search");
            search_action_button_->registerClickAction([this](brls::View*) {
                const bool retry_first_page =
                    search_model_.state() == ttnx::core::SearchViewState::Error &&
                    search_model_.retryable_failure() &&
                    search_model_.error_code() != ttnx::core::SearchErrorCode::ContinuationFailure;
                if (retry_first_page) retry_search_request();
                else start_search();
                return true;
            });

            search_status_label_ = label(content, "", 20);
#ifdef TTNX_M3_INTEGRATED
            m3_status_label_ = label(content, media_feedback_, 18);
#endif
            label(content,
                  "Press A on any result to toggle its normalized ID and clean canonical URL. Channel results also reveal an explicit Open channel action.",
                  18);
            search_results_box_ = new brls::Box(brls::Axis::COLUMN);
            search_results_box_->setMarginBottom(8);
            content->addView(search_results_box_);

            search_load_more_button_ = button(content, "Load more");
            search_load_more_button_->registerClickAction([this](brls::View*) {
                start_load_more();
                return true;
            });
            label(content,
                  "Load more is explicit for this hardware gate. No Shorts, ads, promoted or shopping renderers may reach any page.",
                  18);
            refresh_search_page(true);
            ttnx::record_boot_event("boot-11-search-created");
            break;
        }
        case RootSection::Subscriptions: {
            refresh_library_metadata();
            label(content,"Following",34);
            label(content,"Locally followed channels; no Google login.",20);
            auto* status=label(content,storage_ready_
                ?"A Open channel   Y Unfollow"
                :"SD unavailable: followed channels read-only",17);
            add_local_list(content,LocalListKind::Following,
                           "Followed channels",status);
            break;
        }
        case RootSection::Library: {
            refresh_library_metadata();
            label(content,"Library",34);
            label(content,"History, Watch Later and Favorites stay on this Switch.",20);
            auto* status=label(content,storage_ready_
                ?"A Play   Y Remove locally"
                :"SD unavailable: Library is read-only",17);
            add_local_list(content,LocalListKind::History,"Recent playback",status);
            add_local_list(content,LocalListKind::WatchLater,"Watch Later",status);
            add_local_list(content,LocalListKind::Favorite,"Favorites",status);
            break;
        }
        case RootSection::Settings: {
            refresh_library_metadata();
            history_clear_armed_=false;
            label(content, "Settings", 34);
            label(content, "History & data", 23);
            auto* history_toggle=button(content,
                library_.history_enabled()?"Pause watch history":"Resume watch history");
            history_toggle->registerClickAction([this,history_toggle](brls::View*) {
                library_.set_history_enabled(!library_.history_enabled());
                if(persist_library_metadata())
                    history_toggle->setText(library_.history_enabled()
                        ?"Pause watch history":"Resume watch history");
                else history_toggle->setText("Unable to save watch history setting");
                return true;
            });
            auto* clear_history=button(content,"Clear watch history (two presses)");
            clear_history->registerClickAction([this,clear_history](brls::View*) {
                if(!history_clear_armed_) {
                    history_clear_armed_=true;
                    clear_history->setText("Confirm: clear local watch history");
                    return true;
                }
                history_clear_armed_=false;
                auto copy=library_;
                library_.clear_history();
                if(!persist_library_metadata()) {
                    library_=std::move(copy);
                    clear_history->setText("Unable to clear history on SD");
                } else clear_history->setText("Watch history cleared");
                return true;
            });
            label(content, "Appearance follows your Switch system theme.");
            auto* fps = button(content,
                settings_.show_fps ? "Show frame rate: On" : "Show frame rate: Off");
            auto* saved = label(content,
                storage_ready_ ? "Display preferences are saved automatically."
                               : "SD storage unavailable. Changes last for this session.",
                20);
            fps->registerClickAction([this, fps, saved](brls::View*) {
                settings_.show_fps = !settings_.show_fps;
                fps->setText(settings_.show_fps
                    ? "Show frame rate: On"
                    : "Show frame rate: Off");
                saved->setText(ttnx::save_settings(settings_)
                    ? "Display preference saved."
                    : "Could not save. This change lasts for this session.");
                refresh_footer();
                return true;
            });
            label(content, "No Shorts. Clean share links. No promoted content.", 20);
            label(content, "Nintendo network destinations are blocked by the app before DNS.", 20);
            label(content, "SponsorBlock and DeArrow are planned for a later build.", 20);
            label(content, "TizenTube NX 0.1.0  /  M2 guest Home + Search", 20);
            break;
        }
        }
        return scroll;
    }
};


// Hit-test only visible portions of this shell, with parent clipping. Resolve
// afresh on release so a tab change cannot leave a stale pointer behind.
brls::View* tap_target(brls::View* view, float x, float y) {
    if (!view || view->isHidden() || x < view->getX() || y < view->getY()
        || x >= view->getX() + view->getWidth() || y >= view->getY() + view->getHeight()) {
        return nullptr;
    }
    if (auto* box = dynamic_cast<brls::Box*>(view)) {
        auto& children = box->getChildren();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (auto* target = tap_target(*it, x, y)) return target;
        }
    }
    return view->isFocusable() ? view : nullptr;
}

void handle_touch(ShellActivity* shell) {
    static bool pressed = false;
    static bool moved = false;
    static float start_x = 0;
    static float start_y = 0;
    HidTouchScreenState state{};
    if (hidGetTouchScreenStates(&state, 1) != 1) return;
    if (state.count > 0) {
        const float x = state.touches[0].x;
        const float y = state.touches[0].y;
        if (!pressed) {
            start_x = x;
            start_y = y;
            moved = state.count != 1;
        }
        moved = moved || state.count != 1 ||
                std::abs(x - start_x) > 16 || std::abs(y - start_y) > 16;
        pressed = true;
    } else if (pressed) {
        pressed = false;
        if (moved || shell->getContentView()->getAlpha() < 1.0f) return;
        // Touch coordinates always use the handheld 1280x720 coordinate space.
        auto* target = tap_target(
            shell->getContentView(),
            start_x * brls::Application::contentWidth / 1280.0f,
            start_y * brls::Application::contentHeight / 720.0f);
        if (!target) return;
        const bool is_button = dynamic_cast<brls::Button*>(target) != nullptr;
        brls::Application::giveFocus(target);
        if (is_button) {
            brls::Application::onControllerButtonPressed(brls::BUTTON_A, false);
        }
    }
}

}  // namespace

int main(int, char**) {
    ttnx::record_boot_event("boot-00-main");
    const bool storage_ready = ttnx::initialize_storage();
    ttnx::record_boot_event("boot-01-storage");
#ifdef TTNX_M3_INTEGRATED
    // An app crash or Switch power loss may bypass player destructors.
    // Next successful launch removes *only* known media files, never user
    // settings, numeric diagnostics, downloads from other applications.
    if(storage_ready) {
        (void)ttnx::switch_app::SwitchMediaCacheSink::discard_session_media();
        (void)ttnx::switch_app::SwitchHlsCacheSink::discard_session_media();
    }
#endif
    const auto settings = ttnx::load_settings();

    brls::Logger::setLogLevel(brls::LogLevel::ERROR);
    ttnx::record_boot_event("boot-02-brls-init-begin");
    if (!brls::Application::init()) {
        ttnx::record_boot_event("boot-02-brls-init-failed");
        return EXIT_FAILURE;
    }
    ttnx::record_boot_event("boot-02-brls-init-done");

    ttnx::record_boot_event("boot-03-window-begin");
    brls::Application::createWindow("TizenTube NX");
    ttnx::record_boot_event("boot-03-window-done");
    brls::Application::setGlobalQuit(true);

    // Borealis userAppInit() has already initialized the process socket runtime.
    // This app-lifetime client borrows sockets and acquires one ref-counted SSL
    // initialization. Its destructor runs when main() unwinds, before Borealis
    // userAppExit() releases the framework-owned socket environment.
    ttnx::record_boot_event("boot-04-network-client-begin");
    ttnx::switch_app::LibnxHttpClient network_client;
    ttnx::record_boot_event("boot-04-network-client-done");
    auto* shell = new ShellActivity(settings, storage_ready, network_client);
    ttnx::record_boot_event("boot-05-shell-ctor");
    ttnx::record_boot_event("boot-06-push-activity-begin");
    brls::Application::pushActivity(shell);
    ttnx::record_boot_event("boot-12-push-activity-done");
    hidInitializeTouchScreen();
    ttnx::record_boot_event("boot-13-touch");
    ttnx::record_boot_event("boot-14-interface-ready");

    bool first_loop = true;
    ttnx::record_boot_event("boot-15-first-loop");
    while (brls::Application::mainLoop()) {
        if (first_loop) {
            first_loop = false;
            ttnx::record_boot_event("boot-16-first-frame-done");
        }
        handle_touch(shell);
        shell->tick();
#ifdef TTNX_M3_INTEGRATED
        ttnx::switch_app::tick_m3_player();
#endif
    }

    // Do not let a request worker survive application shutdown. The client is
    // still alive here; it is destroyed only after this function leaves scope.
    shell->shutdown_network();
    ttnx::record_boot_event("clean-exit");
    return EXIT_SUCCESS;
}
