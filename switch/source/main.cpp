#include <borealis.hpp>
#include <switch.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "app_storage.hpp"
#include "libnx_http_client.hpp"
#include "tizentube_nx/ui/navigation.hpp"
#include "tizentube_nx/youtube/guest_api.hpp"
#include "tizentube_nx/youtube/session_bootstrap.hpp"

namespace {

constexpr const char* kGuestUserAgent =
    "Mozilla/5.0 (Nintendo Switch; TizenTube NX) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

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
    ShellActivity(ttnx::core::Settings settings, bool storage_ready)
        : settings_(settings), storage_ready_(storage_ready) {}

    ~ShellActivity() override {
        shutdown_network();
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
        update_frame_rate();
    }

    void shutdown_network() {
        if (network_worker_.joinable()) network_worker_.join();
    }

private:
    brls::Label* footer_ = nullptr;
    std::chrono::steady_clock::time_point sampled_ = std::chrono::steady_clock::now();
    unsigned frames_ = 0;
    int last_fps_ = 0;

    ttnx::core::Settings settings_;
    bool storage_ready_;
    std::string query_;

    // The socket environment is process-level and Borealis initializes it from
    // userAppInit before main(). Keep our SSL reference/client at activity
    // lifetime so button presses borrow one stable service environment instead
    // of repeatedly initializing and tearing services down in each worker.
    ttnx::switch_app::LibnxHttpClient network_client_;
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

    void refresh_footer() {
        if (!footer_) return;
        std::string text = "M2 guest | ";
        text += network_busy_.load() ? "Connecting..." : network_summary_;
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
        if (changed) refresh_footer();
    }

    void start_guest_connection_test() {
        bool expected = false;
        if (!network_busy_.compare_exchange_strong(expected, true)) {
            network_summary_ = "Connecting...";
            refresh_footer();
            return;
        }

        if (network_worker_.joinable()) network_worker_.join();
        guest_session_.reset();
        network_summary_ = "Connecting...";
        network_detail_ =
            "Connecting only to the allowlisted YouTube host. Nintendo destinations remain blocked before DNS.";
        refresh_footer();

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
        }
    }

    brls::View* create_page(ttnx::ui::RootSection section) {
        using ttnx::ui::RootSection;
        auto* scroll = new brls::ScrollingFrame();
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setPadding(32, 36, 32, 36);
        scroll->setContentView(content);

        // Page callbacks are destroyed on tab changes. Worker threads never
        // capture page views, so a tab change cannot leave a stale UI pointer.
        content->registerAction("Sections", brls::BUTTON_B, [this](brls::View*) {
            brls::Application::giveFocus(getView("brls/tab_frame/sidebar"));
            return true;
        });

        switch (section) {
        case RootSection::Home: {
            label(content, "Welcome to TizenTube NX", 34);
            label(content, "Your videos. Less clutter.", 26);
            label(content, "M2 guest browsing networking is ready for real-hardware verification.");
            label(content,
                  "The test below is manual: the app does not make a hidden YouTube request at startup.",
                  20);
            auto* probe = button(content, network_busy_.load()
                ? "YouTube guest connection: Connecting..."
                : "Test YouTube guest connection");
            probe->registerClickAction([this](brls::View*) {
                start_guest_connection_test();
                return true;
            });
            label(content, "Connection status: " + network_summary_, 20);
            label(content, network_detail_, 20);
            label(content,
                  "Network policy: exact www.youtube.com:443 only for M2; Nintendo endpoints are blocked before DNS.",
                  18);
            button(content, "Explore the sections")->registerClickAction([this](brls::View*) {
                brls::Application::giveFocus(getView("brls/tab_frame/sidebar"));
                return true;
            });
            break;
        }
        case RootSection::Search: {
            label(content, "Search", 34);
            label(content, "Try entering a search with the Switch keyboard.");
            auto* enter = button(content, "Enter search");
            auto* query = label(content, query_.empty() ? "No search entered." : query_);
            auto* status = label(content, guest_session_ && guest_session_->usable()
                ? "Guest session is ready. Live Search parsing is the next M2 slice."
                : "Run the guest connection test on Home before live Search is enabled.");
            enter->registerClickAction([this, query, status](brls::View*) {
                // Handle creation errors and cancellation without logging input.
                SwkbdConfig config{};
                if (R_FAILED(swkbdCreate(&config, 0))) {
                    status->setText("Could not open the keyboard. Please try again.");
                    return true;
                }
                swkbdConfigMakePresetDefault(&config);
                swkbdConfigSetHeaderText(&config, "Search YouTube");
                swkbdConfigSetGuideText(&config, "Enter a video, topic or channel");
                swkbdConfigSetStringLenMax(&config, 120);
                swkbdConfigSetInitialText(&config, query_.c_str());
                char text[481]{};
                const Result result = swkbdShow(&config, text, sizeof(text));
                swkbdClose(&config);
                if (R_SUCCEEDED(result)) {
                    query_ = text;
                    query->setText(query_.empty() ? "No search entered." : query_);
                    status->setText(guest_session_ && guest_session_->usable()
                        ? "Guest session is ready. Live Search parsing is the next M2 slice."
                        : "Run the guest connection test on Home before live Search is enabled.");
                }
                return true;
            });
            break;
        }
        case RootSection::Subscriptions:
            label(content, "Subscriptions", 34);
            label(content, "Keep up with the channels you follow.", 26);
            label(content, "YouTube sign-in is not available in this preview.");
            label(content, "Your subscriptions will appear here once account support is ready.");
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
            label(content, "TizenTube NX 0.1.0  /  M2 guest networking", 20);
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
    auto* shell = new ShellActivity(settings, storage_ready);
    brls::Application::pushActivity(shell);
    hidInitializeTouchScreen();
    ttnx::record_boot_event("interface-ready");

    while (brls::Application::mainLoop()) {
        handle_touch(shell);
        shell->tick();
    }

    // Do not let a network worker survive application shutdown. No session data
    // is persisted; joining guarantees the worker is done before ShellActivity's
    // app-lifetime SSL reference is released. Borealis retains ownership of its
    // pre-existing socket initialization through userAppExit().
    shell->shutdown_network();
    ttnx::record_boot_event("clean-exit");
    return EXIT_SUCCESS;
}