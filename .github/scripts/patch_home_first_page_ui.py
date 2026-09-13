from pathlib import Path

p = Path('switch/source/main.cpp')
s = p.read_text()


def repl(old, new, count=1):
    global s
    n = s.count(old)
    if n != count:
        raise SystemExit(f'expected {count} occurrences, found {n}: {old[:120]!r}')
    s = s.replace(old, new, count)


repl('#include "libnx_http_client.hpp"\n#include "tizentube_nx/core/search_model.hpp"',
     '#include "libnx_http_client.hpp"\n#include "tizentube_nx/core/home_model.hpp"\n#include "tizentube_nx/core/search_model.hpp"')
repl('#include "tizentube_nx/youtube/guest_api.hpp"\n#include "tizentube_nx/youtube/search_flow.hpp"',
     '#include "tizentube_nx/youtube/guest_api.hpp"\n#include "tizentube_nx/youtube/guest_home.hpp"\n#include "tizentube_nx/youtube/search_flow.hpp"')

repl('''        apply_pending_search_scroll_reflow();
        pump_network_result();
        pump_search_result();
        update_frame_rate();''',
     '''        apply_pending_search_scroll_reflow();
        pump_network_result();
        pump_home_result();
        pump_search_result();
        update_frame_rate();''')

repl('''        if (network_worker_.joinable()) network_worker_.join();
        if (search_worker_.joinable()) search_worker_.join();
        network_busy_.store(false);
        search_busy_.store(false);
        search_worker_done_.store(false);''',
     '''        if (network_worker_.joinable()) network_worker_.join();
        if (home_worker_.joinable()) home_worker_.join();
        if (search_worker_.joinable()) search_worker_.join();
        network_busy_.store(false);
        home_busy_.store(false);
        home_worker_done_.store(false);
        search_busy_.store(false);
        search_worker_done_.store(false);''')

repl('''    std::optional<ttnx::youtube::GuestSession> guest_session_;

    // SearchModel is UI-thread-only.''',
     '''    std::optional<ttnx::youtube::GuestSession> guest_session_;

    // HomeModel is UI-thread-only. The explicit Home worker publishes normalized
    // HomePage data plus the captured generation and never touches Borealis Views.
    ttnx::core::HomeModel home_model_;
    std::atomic_bool home_busy_{false};
    std::atomic_bool home_worker_done_{false};
    std::thread home_worker_;
    std::mutex home_mutex_;
    bool pending_home_result_{false};
    std::uint64_t pending_home_generation_{0};
    ttnx::youtube::GuestHomeResult pending_home_;

    // SearchModel is UI-thread-only.''')

repl('''    brls::Button* home_probe_button_{nullptr};
    brls::Label* home_status_label_{nullptr};
    brls::Label* home_detail_label_{nullptr};
    brls::Label* search_query_label_{nullptr};''',
     '''    brls::Button* home_probe_button_{nullptr};
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

    brls::Label* search_query_label_{nullptr};''')

repl('''        if (search_busy_.load()) {
            text += "Searching...";
        } else if (network_busy_.load()) {''',
     '''        if (search_busy_.load()) {
            text += "Searching...";
        } else if (home_busy_.load()) {
            text += "Loading Home...";
        } else if (network_busy_.load()) {''')

old_home_refresh = '''    void refresh_home_page() {
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
'''
new_home_refresh = '''    std::string home_feed_status_text() const {
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
            case ttnx::core::HomeViewState::Empty:
                return "YouTube Home returned no supported normal results.";
            case ttnx::core::HomeViewState::Error:
                return home_model_.error().empty()
                    ? "Home could not be loaded safely."
                    : home_model_.error();
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
'''
repl(old_home_refresh, new_home_refresh)

repl('''    void start_guest_connection_test() {
        if (search_busy_.load()) {
            network_detail_ = "A live Search request is in progress. Try the guest diagnostic again after it finishes.";''',
     '''    void start_guest_connection_test() {
        if (search_busy_.load() || home_busy_.load()) {
            network_detail_ = "A live YouTube request is in progress. Try the guest diagnostic again after it finishes.";''')

repl('''        guest_session_.reset();
        network_summary_ = "Connecting...";''',
     '''        guest_session_.reset();
        home_model_.reset();
        network_summary_ = "Connecting...";''')
repl('''        refresh_home_page();
        refresh_search_page(false);

        try {
            network_worker_''',
     '''        refresh_home_page(true);
        refresh_search_page(false);

        try {
            network_worker_''')

repl('''    void start_search() {
        if (search_busy_.load()) return;
        if (network_busy_.load()) {''',
     '''    void start_search() {
        if (search_busy_.load()) return;
        if (home_busy_.load() || network_busy_.load()) {''')
repl('''    void retry_search_request() {
        if (search_busy_.load() || network_busy_.load()) return;''',
     '''    void retry_search_request() {
        if (search_busy_.load() || home_busy_.load() || network_busy_.load()) return;''')
repl('''    void start_load_more() {
        if (search_busy_.load() || network_busy_.load()) return;''',
     '''    void start_load_more() {
        if (search_busy_.load() || home_busy_.load() || network_busy_.load()) return;''')

repl('''        home_probe_button_ = nullptr;
        home_status_label_ = nullptr;
        home_detail_label_ = nullptr;
        search_query_label_ = nullptr;''',
     '''        home_probe_button_ = nullptr;
        home_status_label_ = nullptr;
        home_detail_label_ = nullptr;
        home_load_button_ = nullptr;
        home_feed_status_label_ = nullptr;
        home_results_box_ = nullptr;
        home_detail_rows_.clear();
        search_query_label_ = nullptr;''')

old_home_page = '''        case RootSection::Home: {
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
        }'''
new_home_page = '''        case RootSection::Home: {
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
        }'''
repl(old_home_page, new_home_page)

p.write_text(s)
