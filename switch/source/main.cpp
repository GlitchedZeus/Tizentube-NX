#include <borealis.hpp>
#include <switch.h>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <string>
#include "app_storage.hpp"
#include "tizentube_nx/ui/navigation.hpp"

namespace {
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
    brls::View* createContentView() override {
        auto* frame = new brls::TabFrame();
        frame->setTitle("TizenTube NX");
        // This pinned framework has a placeholder footer. Supply real hints
        // and our own FPS label rather than its unimplemented FPS API.
        auto* footer = dynamic_cast<brls::Box*>(frame->getChildren().back());
        while (!footer->getChildren().empty()) footer->removeView(footer->getChildren().back());
        label(footer, "A Open    B Sections    + Exit", 20)->setMarginBottom(0);
        footer_ = label(footer, "M1 preview | Guest", 20);
        footer_->setMarginBottom(0);
        frame->getView("brls/tab_frame/sidebar")->setWidth(300);
        for (const auto& item : ttnx::ui::kRootNavigation) {
            frame->addTab(std::string(item.label), [this, section = item.section] {
                return create_page(section);
            });
        }
        return frame;
    }
    void update_frame_rate() {
        ++frames_;
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - sampled_).count();
        if (elapsed < 1.0) return;
        footer_->setText(settings_.show_fps
            ? "M1 preview | " + std::to_string(static_cast<int>(frames_ / elapsed + 0.5)) + " FPS"
            : "M1 preview | Guest");
        sampled_ = now;
        frames_ = 0;
    }
private:
    brls::Label* footer_ = nullptr;
    std::chrono::steady_clock::time_point sampled_ = std::chrono::steady_clock::now();
    unsigned frames_ = 0;
    ttnx::core::Settings settings_;
    bool storage_ready_;
    std::string query_;
    brls::View* create_page(ttnx::ui::RootSection section) {
        using ttnx::ui::RootSection;
        auto* scroll = new brls::ScrollingFrame();
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setPadding(32, 36, 32, 36);
        scroll->setContentView(content);
        // Page callbacks are destroyed on tab changes; no asynchronous view captures.
        content->registerAction("Sections", brls::BUTTON_B, [this](brls::View*) {
            brls::Application::giveFocus(getView("brls/tab_frame/sidebar"));
            return true;
        });
        switch (section) {
        case RootSection::Home:
            label(content, "Welcome to TizenTube NX", 34);
            label(content, "Your videos. Less clutter.", 26);
            label(content, "This preview lets you try the new Switch interface.");
            label(content, "YouTube browsing and playback are coming next.");
            label(content, "Use the left menu to explore. Press A to enter a section.");
            button(content, "Explore the sections")->registerClickAction([this](brls::View*) {
                brls::Application::giveFocus(getView("brls/tab_frame/sidebar"));
                return true;
            });
            break;
        case RootSection::Search: {
            label(content, "Search", 34);
            label(content, "Try entering a search with the Switch keyboard.");
            auto* enter = button(content, "Enter search");
            auto* query = label(content, query_.empty() ? "No search entered." : query_);
            auto* status = label(content, "Video results are not available in this preview.");
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
                    status->setText("Video results are not available in this preview.");
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
            auto* fps = button(content, settings_.show_fps ? "Show frame rate: On" : "Show frame rate: Off");
            auto* saved = label(content, storage_ready_ ? "Display preferences are saved automatically."
                : "SD storage unavailable. Changes last for this session.", 20);
            fps->registerClickAction([this, fps, saved](brls::View*) {
                settings_.show_fps = !settings_.show_fps;

                fps->setText(settings_.show_fps ? "Show frame rate: On" : "Show frame rate: Off");
                saved->setText(ttnx::save_settings(settings_) ? "Display preference saved."
                    : "Could not save. This change lasts for this session.");
                return true;
            });
            label(content, "No Shorts. Clean share links. No promoted content.", 20);
            label(content, "SponsorBlock and DeArrow are planned for a later build.", 20);
            label(content, "TizenTube NX 0.1.0  /  Interface preview", 20);
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
        || x >= view->getX() + view->getWidth() || y >= view->getY() + view->getHeight()) return nullptr;
    if (auto* box = dynamic_cast<brls::Box*>(view)) {
        auto& children = box->getChildren();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if (auto* target = tap_target(*it, x, y)) return target;
    }
    return view->isFocusable() ? view : nullptr;
}
void handle_touch(ShellActivity* shell) {
    static bool pressed = false;
    static bool moved = false;
    static float start_x = 0, start_y = 0;
    HidTouchScreenState state{};
    if (hidGetTouchScreenStates(&state, 1) != 1) return;
    if (state.count > 0) {
        const float x = state.touches[0].x;
        const float y = state.touches[0].y;
        if (!pressed) { start_x = x; start_y = y; moved = state.count != 1; }
        moved = moved || state.count != 1 || std::abs(x - start_x) > 16 || std::abs(y - start_y) > 16;
        pressed = true;
    } else if (pressed) {
        pressed = false;
        if (moved || shell->getContentView()->getAlpha() < 1.0f) return;
        // Touch coordinates always use the handheld 1280x720 coordinate space.
        auto* target = tap_target(shell->getContentView(),
            start_x * brls::Application::contentWidth / 1280.0f,
            start_y * brls::Application::contentHeight / 720.0f);
        if (!target) return;
        const bool is_button = dynamic_cast<brls::Button*>(target) != nullptr;
        brls::Application::giveFocus(target);
        if (is_button) brls::Application::onControllerButtonPressed(brls::BUTTON_A, false);
    }
}
} // namespace
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
    while (brls::Application::mainLoop()) { handle_touch(shell); shell->update_frame_rate(); }
    ttnx::record_boot_event("clean-exit");
    return EXIT_SUCCESS;
}
