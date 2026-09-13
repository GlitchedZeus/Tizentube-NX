#include <borealis.hpp>
#include <switch.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "app_storage.hpp"
#include "libnx_http_client.hpp"
#include "tizentube_nx/core/search_model.hpp"
#include "tizentube_nx/core/search_presentation.hpp"
#include "tizentube_nx/ui/navigation.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"
#include "tizentube_nx/youtube/search_flow.hpp"
#include "tizentube_nx/youtube/search_worker_guard.hpp"
#include "tizentube_nx/youtube/session_bootstrap.hpp"

namespace {

constexpr const char* kGuestUserAgent =
    "Mozilla/5.0 (Nintendo Switch; TizenTube NX) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
constexpr std::size_t kMaxUiTitleChars = 180;
constexpr std::size_t kMaxUiMetadataChars = 280;

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
        // Apply a continuation-focus request one frame after rows are
        // attached so Yoga has current coordinates before ScrollingFrame
        // recenters the selected result.
        apply_pending_search_focus();
        pump_network_result();
        pump_search_result();
        update_frame_rate();
    }

    void shutdown_network() {
        if (network_worker_.joinable()) network_worker_.join();
        if (search_worker_.joinable()) search_worker_.join();
        network_busy_.store(false);
        search_busy_.store(false);
        search_worker_done_.store(false);
    }


private:
    brls::Label* footer_ = nullptr;
    std::chrono::steady_clock::time_point sampled_ = std::chrono::steady_clock::now();
    unsigned frames_ = 0;
    int last_fps_ = 0;

    ttnx::core::Settings settings_;
    bool storage_ready_;
    std::string query_;

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

    // These pointers are UI-thread-only conveniences for the currently visible
    // plain ScrollingFrame. create_page() clears every page pointer before the
    // replacement page is built. Workers never capture or dereference them.
    brls::Button* home_probe_button_{nullptr};
    brls::Label* home_status_label_{nullptr};
    brls::Label* home_detail_label_{nullptr};
    brls::Label* search_query_label_{nullptr};
    brls::Label* search_status_label_{nullptr};
    brls::Button* search_action_button_{nullptr};
    brls::Button* search_load_more_button_{nullptr};
    brls::Box* search_results_box_{nullptr};

    struct SearchDetailRow {
        std::string identity;
        brls::Button* button{nullptr};
        brls::Label* detail{nullptr};
    };
    std::vector<SearchDetailRow> search_detail_rows_;
    std::string pending_search_focus_identity_;

    void refresh_footer() {
        if (!footer_) return;
        std::string text = "M2 guest | ";
        if (search_busy_.load()) {
            text += "Searching...";
        } else if (network_busy_.load()) {
            text += "Connecting...";
        } else {
            text += network_summary_;
        }
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

    void refresh_home_page() {
        if (home_probe_button_) {
            home_probe_button_->setText(network_busy_.load()
                ? "YouTube guest connection: Connecting..."
                : "Test YouTube guest connection");
        }
        if (home_status_label_) {
            home_status_label_->setText("Connection status: " + network_summary_);
        }
        if (home_detail_label_) home_detail_label_->setText(network_detail_);
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
            if (!row.detail) continue;
            const bool selected = !selected_identity.empty() && row.identity == selected_identity;
            row.detail->setVisibility(
                selected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
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

    void apply_pending_search_focus() {
        if (pending_search_focus_identity_.empty()) return;

        // A continuation may finish after the user leaves Search. Never
        // steal focus back from another tab or retain page-owned pointers.
        if (!search_results_box_) {
            pending_search_focus_identity_.clear();
            return;
        }

        const std::string wanted = pending_search_focus_identity_;
        pending_search_focus_identity_.clear();
        for (const auto& row : search_detail_rows_) {
            if (row.identity == wanted && row.button) {
                brls::Application::giveFocus(row.button);
                return;
            }
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

        auto* detail_label = label(
            search_results_box_,
            ttnx::core::search_result_selection_display(result),
            18);
        detail_label->setMarginBottom(14);
        detail_label->setVisibility(
            identity == search_model_.selected_identity()
                ? brls::Visibility::VISIBLE
                : brls::Visibility::GONE);
        search_detail_rows_.push_back({identity, select, detail_label});

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
        pending_search_focus_identity_.clear();

        // Full rebuilding is reserved for a genuinely new/reconstructed result
        // page. Selection and continuation appends never delete existing rows.
        search_detail_rows_.clear();
        while (!search_results_box_->getChildren().empty()) {
            search_results_box_->removeView(search_results_box_->getChildren().back());
        }
        (void)append_search_results_from(0);
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
        if (search_busy_.load()) {
            network_detail_ = "A live Search request is in progress. Try the guest diagnostic again after it finishes.";
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
        network_summary_ = "Connecting...";
        network_detail_ =
            "Connecting only to the allowlisted YouTube host. Nintendo destinations remain blocked before DNS.";
        refresh_footer();
        refresh_home_page();
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
            // SearchModel already deduped by stable identity. Append only rows
            // that were not present before this continuation; never delete or
            // replace the currently focused first-page result tree.
            const auto first_new_identity =
                append_search_results_from(previous_result_count);
            refresh_search_inline_details();
            refresh_search_page(false, search_model_.end_of_results());

            // Appending rows above an already-focused Load more button moves
            // its layout position without another focus-gained event. Defer
            // focus to the first newly-added row until the next frame so the
            // selector remains visible and the next page begins naturally.
            if (!first_new_identity.empty()) {
                pending_search_focus_identity_ = first_new_identity;
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

    void start_search() {
        if (search_busy_.load()) return;
        if (network_busy_.load()) {
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
        if (search_busy_.load() || network_busy_.load()) return;
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
        if (search_busy_.load() || network_busy_.load()) return;
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

    brls::View* create_page(ttnx::ui::RootSection section) {
        using ttnx::ui::RootSection;
        // Startup recovery build: use the same plain ScrollingFrame ownership
        // model as the physically accepted checkpoint. Tab switches are synchronous
        // on the UI thread, so stale UI-only pointers are cleared before creating
        // the replacement page.
        home_probe_button_ = nullptr;
        home_status_label_ = nullptr;
        home_detail_label_ = nullptr;
        search_query_label_ = nullptr;
        search_status_label_ = nullptr;
        search_action_button_ = nullptr;
        search_load_more_button_ = nullptr;
        search_results_box_ = nullptr;
        search_detail_rows_.clear();
        pending_search_focus_identity_.clear();
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
            label(content, "Welcome to TizenTube NX", 34);
            label(content, "Your videos. Less clutter.", 26);
            label(content, "Guest networking verified on real Switch hardware.");
            label(content,
                  "Networking remains manual: the app does not make a hidden YouTube request at startup.",
                  20);
            home_probe_button_ = button(content, network_busy_.load()
                ? "YouTube guest connection: Connecting..."
                : "Test YouTube guest connection");
            home_probe_button_->registerClickAction([this](brls::View*) {
                start_guest_connection_test();
                return true;
            });
            home_status_label_ = label(content, "Connection status: " + network_summary_, 20);
            home_detail_label_ = label(content, network_detail_, 20);
            label(content,
                  "Network policy: exact www.youtube.com:443 only for M2; Nintendo endpoints are blocked before DNS.",
                  18);
            button(content, "Explore the sections")->registerClickAction([this](brls::View*) {
                focus_sidebar_section(ttnx::ui::RootSection::Home);
                return true;
            });
            ttnx::record_boot_event("boot-10-home-created");
            break;
        }
        case RootSection::Search: {
            label(content, "Search", 34);
            label(content,
                  "Live guest Search is enabled with explicit Load more. Results stay text-only while thumbnail hosts remain blocked.",
                  20);

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
            label(content,
                  "Press A on any result to show its normalized ID and clean canonical URL directly below that result.",
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
        case RootSection::Subscriptions:
            label(content, "Subscriptions", 34);
            label(content, "Keep up with the channels you follow.", 26);
            label(content, "YouTube sign-in is not available in this preview.");
            label(content, "Your subscriptions will appear here once local profile support is ready.");
            break;
        case RootSection::Library:
            label(content, "Library", 34);
            label(content, "A place for your playlists and watched videos.", 26);
            label(content, "YouTube sign-in is not available in this preview.");
            label(content, "There is no watch history or saved video data yet.");
            break;
        case RootSection::Settings: {
            label(content, "Settings", 34);
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
            label(content, "TizenTube NX 0.1.0  /  M2 live guest Search", 20);
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
    }

    // Do not let a request worker survive application shutdown. The client is
    // still alive here; it is destroyed only after this function leaves scope.
    shell->shutdown_network();
    ttnx::record_boot_event("clean-exit");
    return EXIT_SUCCESS;
}
