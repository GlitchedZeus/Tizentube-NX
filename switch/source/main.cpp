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

#include "app_storage.hpp"
#include "libnx_http_client.hpp"
#include "tizentube_nx/core/search_model.hpp"
#include "tizentube_nx/core/search_presentation.hpp"
#include "tizentube_nx/ui/navigation.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"
#include "tizentube_nx/youtube/search_flow.hpp"
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

class ShellActivity;

// TabFrame keeps only one tab alive and deletes it when changing sections. This
// wrapper lets ShellActivity clear UI-only pointers before the child views are
// freed. Network workers never receive this pointer or any child view pointer.
class TrackedSectionPage final : public brls::ScrollingFrame {
public:
    TrackedSectionPage(ShellActivity* owner, ttnx::ui::RootSection section)
        : owner_(owner), section_(section) {}
    ~TrackedSectionPage() override;

    void detach_owner() noexcept { owner_ = nullptr; }
    [[nodiscard]] ttnx::ui::RootSection section() const noexcept { return section_; }

private:
    ShellActivity* owner_{nullptr};
    ttnx::ui::RootSection section_;
};

enum class SearchWorkerKind {
    FirstPage,
    Continuation,
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
        if (active_page_) active_page_->detach_owner();
    }

    brls::View* createContentView() override {
        auto* frame = new brls::TabFrame();
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
        refresh_footer();
        return frame;
    }

    void tick() {
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

    void on_section_page_destroyed(
        TrackedSectionPage* page,
        ttnx::ui::RootSection section) noexcept {
        if (active_page_ == page) active_page_ = nullptr;
        if (section == ttnx::ui::RootSection::Home) {
            home_probe_button_ = nullptr;
            home_status_label_ = nullptr;
            home_detail_label_ = nullptr;
        }
        if (section == ttnx::ui::RootSection::Search) {
            search_query_label_ = nullptr;
            search_status_label_ = nullptr;
            search_action_button_ = nullptr;
            search_results_box_ = nullptr;
            search_load_more_button_ = nullptr;
            search_selection_label_ = nullptr;
        }
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
    SearchWorkerKind pending_search_kind_{SearchWorkerKind::FirstPage};
    ttnx::youtube::GuestSearchResult pending_search_;

    // Only the active TabFrame page exists. TrackedSectionPage clears these
    // pointers synchronously on tab destruction, so UI refresh never touches a
    // view that Borealis has already freed.
    TrackedSectionPage* active_page_{nullptr};
    brls::Button* home_probe_button_{nullptr};
    brls::Label* home_status_label_{nullptr};
    brls::Label* home_detail_label_{nullptr};
    brls::Label* search_query_label_{nullptr};
    brls::Label* search_status_label_{nullptr};
    brls::Button* search_action_button_{nullptr};
    brls::Box* search_results_box_{nullptr};
    brls::Button* search_load_more_button_{nullptr};
    brls::Label* search_selection_label_{nullptr};

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
                    ? "No results on this page. More results are available."
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

    const ttnx::core::BrowseResult* selected_search_result() const {
        const auto& identity = search_model_.selected_identity();
        if (identity.empty()) return nullptr;
        for (const auto& result : search_model_.results()) {
            if (ttnx::core::browse_result_identity(result) == identity) return &result;
        }
        return nullptr;
    }

    void rebuild_search_results() {
        if (!search_results_box_) return;
        while (!search_results_box_->getChildren().empty()) {
            search_results_box_->removeView(search_results_box_->getChildren().back());
        }

        for (const auto& result : search_model_.results()) {
            const auto identity = ttnx::core::browse_result_identity(result);
            if (identity.empty()) continue;

            std::string title = "[" + ttnx::core::search_result_kind_label(result.kind) + "] ";
            title += result.title.empty() ? "Untitled result" : result.title;
            auto* select = button(
                search_results_box_,
                bounded_ui_text(title, kMaxUiTitleChars));
            select->setHeight(72);
            select->registerClickAction([this, identity](brls::View*) {
                if (!search_model_.select(identity)) return true;
                refresh_search_selection();
                return true;
            });

            const auto metadata = ttnx::core::search_result_metadata_display(result);
            if (!metadata.empty()) {
                auto* metadata_label = label(
                    search_results_box_,
                    bounded_ui_text(metadata, kMaxUiMetadataChars),
                    18);
                metadata_label->setMarginBottom(14);
            }
        }
    }

    void refresh_search_selection() {
        if (!search_selection_label_) return;
        if (const auto* result = selected_search_result()) {
            search_selection_label_->setText(
                ttnx::core::search_result_selection_display(*result));
        } else {
            search_selection_label_->setText(
                "Select a result to inspect its normalized identity. Playback is not enabled yet.");
        }
    }

    void refresh_search_page(bool rebuild_results) {
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

        if (search_load_more_button_) {
            const bool continuation_error =
                search_model_.state() == ttnx::core::SearchViewState::Error &&
                search_model_.error_code() == ttnx::core::SearchErrorCode::ContinuationFailure &&
                search_model_.retryable_failure();
            const bool show = continuation_error ||
                              search_model_.state() == ttnx::core::SearchViewState::LoadingMore ||
                              !search_model_.results().empty() || search_model_.can_load_more();
            search_load_more_button_->setVisibility(
                show ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            if (continuation_error) {
                search_load_more_button_->setText("Retry load more");
            } else if (search_model_.state() == ttnx::core::SearchViewState::LoadingMore) {
                search_load_more_button_->setText("Loading more...");
            } else if (search_model_.can_load_more()) {
                search_load_more_button_->setText("Load more");
            } else {
                search_load_more_button_->setText("End of results");
            }
        }

        if (rebuild_results) rebuild_search_results();
        refresh_search_selection();
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
        SearchWorkerKind kind,
        ttnx::youtube::GuestSearchResult result) {
        {
            std::lock_guard<std::mutex> lock(search_mutex_);
            pending_search_generation_ = generation;
            pending_search_kind_ = kind;
            pending_search_ = std::move(result);
            pending_search_result_ = true;
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
        SearchWorkerKind kind = SearchWorkerKind::FirstPage;
        bool have_result = false;
        {
            std::lock_guard<std::mutex> lock(search_mutex_);
            if (pending_search_result_) {
                generation = pending_search_generation_;
                kind = pending_search_kind_;
                result = std::move(pending_search_);
                pending_search_result_ = false;
                have_result = true;
            }
        }

        search_worker_done_.store(false);
        search_busy_.store(false);

        bool applied = false;
        if (have_result) {
            if (result.page) {
                applied = kind == SearchWorkerKind::Continuation
                    ? search_model_.apply_continuation(generation, *result.page)
                    : search_model_.apply_first_page(generation, *result.page);
            } else {
                auto code = result.error_code;
                if (code == ttnx::core::SearchErrorCode::None) {
                    code = ttnx::core::SearchErrorCode::UnsupportedResponse;
                }
                applied = search_model_.fail(generation, code);
            }
        }

        refresh_footer();
        if (applied) refresh_search_page(true);
        else refresh_search_page(false);
    }

    void launch_search_worker(
        std::uint64_t generation,
        SearchWorkerKind kind,
        std::string query) {
        if (!guest_session_ || !guest_session_->usable()) return;
        if (search_worker_.joinable()) search_worker_.join();

        search_busy_.store(true);
        search_worker_done_.store(false);
        refresh_footer();
        refresh_search_page(false);

        const auto session = *guest_session_;
        try {
            search_worker_ = std::thread(
                [this, generation, kind, query = std::move(query), session] {
                    ttnx::youtube::GuestSearchResult result;
                    if (kind == SearchWorkerKind::Continuation) {
                        result = search_flow_.next(network_client_, session);
                    } else {
                        result = search_flow_.begin(network_client_, session, query);
                    }
                    publish_search_result(generation, kind, std::move(result));
                });
        } catch (...) {
            search_busy_.store(false);
            search_worker_done_.store(false);
            search_model_.fail(generation, ttnx::core::SearchErrorCode::NetworkUnavailable);
            refresh_footer();
            refresh_search_page(false);
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
        launch_search_worker(generation, SearchWorkerKind::FirstPage, query_);
    }

    void start_load_more() {
        if (search_busy_.load() || network_busy_.load()) return;
        if (!guest_session_ || !guest_session_->usable()) {
            refresh_search_page(false);
            return;
        }

        const auto generation = search_model_.generation();
        if (!search_model_.begin_load_more(generation)) return;
        refresh_search_page(false);
        launch_search_worker(generation, SearchWorkerKind::Continuation, {});
    }

    void retry_search_request() {
        if (search_busy_.load() || network_busy_.load()) return;
        if (!guest_session_ || !guest_session_->usable()) {
            refresh_search_page(false);
            return;
        }

        const auto generation = search_model_.generation();
        if (!search_model_.begin_retry(generation)) return;
        const bool continuation =
            search_model_.state() == ttnx::core::SearchViewState::LoadingMore;
        refresh_search_page(false);
        launch_search_worker(
            generation,
            continuation ? SearchWorkerKind::Continuation : SearchWorkerKind::FirstPage,
            continuation ? std::string{} : search_model_.query());
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

    brls::View* create_page(ttnx::ui::RootSection section) {
        using ttnx::ui::RootSection;
        auto* scroll = new TrackedSectionPage(this, section);
        active_page_ = scroll;
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setPadding(32, 36, 32, 36);
        scroll->setContentView(content);

        // Pages are destroyed on tab changes. Worker threads never capture page
        // views; all async data returns through ShellActivity state first.
        content->registerAction("Sections", brls::BUTTON_B, [this](brls::View*) {
            brls::Application::giveFocus(getView("brls/tab_frame/sidebar"));
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
                brls::Application::giveFocus(getView("brls/tab_frame/sidebar"));
                return true;
            });
            refresh_home_page();
            break;
        }
        case RootSection::Search: {
            label(content, "Search", 34);
            label(content,
                  "Live guest Search is enabled. Results are text-only while thumbnail hosts remain blocked.",
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
            search_results_box_ = new brls::Box(brls::Axis::COLUMN);
            search_results_box_->setMarginBottom(8);
            content->addView(search_results_box_);

            search_load_more_button_ = button(content, "Load more");
            search_load_more_button_->registerClickAction([this](brls::View*) {
                const bool continuation_error =
                    search_model_.state() == ttnx::core::SearchViewState::Error &&
                    search_model_.error_code() == ttnx::core::SearchErrorCode::ContinuationFailure &&
                    search_model_.retryable_failure();
                if (continuation_error) retry_search_request();
                else start_load_more();
                return true;
            });

            search_selection_label_ = label(
                content,
                "Select a result to inspect its normalized identity. Playback is not enabled yet.",
                18);
            label(content,
                  "No Shorts, ads, promoted or shopping renderers are allowed to reach this list.",
                  18);
            refresh_search_page(true);
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

TrackedSectionPage::~TrackedSectionPage() {
    if (owner_) owner_->on_section_page_destroyed(this, section_);
}

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
    const bool storage_ready = ttnx::initialize_storage();
    const auto settings = ttnx::load_settings();
    ttnx::record_boot_event("starting-interface");

    brls::Logger::setLogLevel(brls::LogLevel::ERROR);
    if (!brls::Application::init()) {
        ttnx::record_boot_event("interface-initialization-failed");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("TizenTube NX");
    brls::Application::setGlobalQuit(true);

    // Borealis userAppInit() has already initialized the process socket runtime.
    // This app-lifetime client borrows sockets and acquires one ref-counted SSL
    // initialization. Its destructor runs when main() unwinds, before Borealis
    // userAppExit() releases the framework-owned socket environment.
    ttnx::switch_app::LibnxHttpClient network_client;
    auto* shell = new ShellActivity(settings, storage_ready, network_client);
    brls::Application::pushActivity(shell);
    hidInitializeTouchScreen();
    ttnx::record_boot_event("interface-ready");

    while (brls::Application::mainLoop()) {
        handle_touch(shell);
        shell->tick();
    }

    // Do not let a request worker survive application shutdown. The client is
    // still alive here; it is destroyed only after this function leaves scope.
    shell->shutdown_network();
    ttnx::record_boot_event("clean-exit");
    return EXIT_SUCCESS;
}
