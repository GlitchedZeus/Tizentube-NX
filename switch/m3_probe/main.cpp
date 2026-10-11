// TizenTube NX M3 *isolated* offline playback probe.
// NEVER link this unit into the accepted M2 application NRO.
// Only a fixed local MP4 is permitted; no HTTP client or media URL entry exists.
#include <borealis.hpp>
#include <switch.h>
#include <mpv/client.h>
#include <mpv/render.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr const char* kFixture = "sdmc:/switch/TizenTube-NX/m3-test.mp4";
constexpr int kRenderWidth = 640;
constexpr int kRenderHeight = 360;

class OfflineMpv {
public:
    OfflineMpv() : pixels_(kRenderWidth * kRenderHeight * 4, 0) {}
    ~OfflineMpv() { shutdown(); }

    bool init() {
        if (handle_) return true;
        handle_ = mpv_create();
        if (!handle_) { message_ = "mpv_create failed"; return false; }
        // A hard-coded SD fixture is the ONLY accepted source. No config,
        // scripts, ytdl hooks, IPC, playlists or arbitrary network streams.
        mpv_set_option_string(handle_, "config", "no");
        mpv_set_option_string(handle_, "load-scripts", "no");
        mpv_set_option_string(handle_, "ytdl", "no");
        // FFmpeg must not follow nested HLS/network/file references from a
        // malicious local file. This proof deliberately permits file/pipe only.
        mpv_set_option_string(handle_, "demuxer-lavf-o", "protocol_whitelist=file,pipe");
        mpv_set_option_string(handle_, "input-default-bindings", "no");
        mpv_set_option_string(handle_, "vo", "libmpv");
        mpv_set_option_string(handle_, "hwdec", "auto");
        mpv_set_option_string(handle_, "audio-channels", "stereo");
        if (mpv_initialize(handle_) < 0) {
            message_ = "mpv_initialize failed";
            shutdown();
            return false;
        }
        mpv_render_param api[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_create(&render_, handle_, api) < 0) {
            message_ = "Software render context unsupported by this libmpv";
            shutdown();
            return false;
        }
        message_ = "Ready. Copy an H.264/AAC MP4 to SD, then press A.";
        return true;
    }

    bool start() {
        if (!handle_ || !render_) { message_ = "Player not initialized"; return false; }
        FILE* file = std::fopen(kFixture, "rb");
        if (!file) {
            message_ = "No m3-test.mp4 in /switch/TizenTube-NX/";
            return false;
        }
        std::fclose(file);
        const char* command[] = {"loadfile", kFixture, "replace", nullptr};
        if (mpv_command(handle_, command) < 0) {
            message_ = "Local playback command failed";
            return false;
        }
        message_ = "Local file requested; check image and sound.";
        return true;
    }

    void toggle_pause() {
        if (!handle_) return;
        const char* command[] = {"cycle", "pause", nullptr};
        mpv_command(handle_, command);
    }

    void seek(int seconds) {
        if (!handle_) return;
        const auto offset = std::to_string(seconds);
        const char* command[] = {"seek", offset.c_str(), "relative", nullptr};
        mpv_command(handle_, command);
    }

    void stop() {
        if (!handle_) return;
        const char* command[] = {"stop", nullptr};
        mpv_command(handle_, command);
        message_ = "Stopped.";
    }

    const std::string& message() const { return message_; }

    std::string stats() const {
        if (!handle_) return "mpv unavailable";
        const char* hw = mpv_get_property_string(handle_, "hwdec-current");
        std::string decoder = hw ? hw : "unknown";
        if (hw) mpv_free(const_cast<char*>(hw));
        double seconds = 0.0;
        const bool has_time = mpv_get_property(handle_, "time-pos",
                                               MPV_FORMAT_DOUBLE, &seconds) >= 0;
        return "Decoder: " + decoder + "  |  time: " +
               (has_time ? std::to_string(static_cast<int>(seconds)) + "s" : "--");
    }

    // The pinned Borealis video context has no public Deko3D framebuffer
    // accessors. This intentionally tests mpv's software render API first;
    // it does NOT prove hardware decoder usage or 1080p performance.
    const std::vector<unsigned char>& render() {
        if (!render_) return pixels_;
        const auto flags = mpv_render_context_update(render_);
        if (flags & MPV_RENDER_UPDATE_FRAME) {
            int size[2]{kRenderWidth, kRenderHeight};
            int stride = kRenderWidth * 4;
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_SW_SIZE, size},
                {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char*>("rgba")},
                {MPV_RENDER_PARAM_SW_STRIDE, &stride},
                {MPV_RENDER_PARAM_SW_POINTER, pixels_.data()},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };
            if (mpv_render_context_render(render_, params) >= 0) {
                have_frame_ = true;
                mpv_render_context_report_swap(render_);
            }
        }
        return pixels_;
    }
    bool has_frame() const { return have_frame_; }

private:
    void shutdown() {
        if (render_) { mpv_render_context_free(render_); render_ = nullptr; }
        if (handle_) { mpv_terminate_destroy(handle_); handle_ = nullptr; }
    }
    mpv_handle* handle_{nullptr};
    mpv_render_context* render_{nullptr};
    std::vector<unsigned char> pixels_;
    std::string message_{"Initializing offline libmpv..."};
    bool have_frame_{false};
};

class ProbeVideo : public brls::View {
public:
    explicit ProbeVideo(OfflineMpv& player) : player_(player) {
        setHeight(400);
    }

    ~ProbeVideo() override {
        if (image_ >= 0) {
            nvgDeleteImage(brls::Application::getNVGContext(), image_);
            image_ = -1;
        }
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style, brls::FrameContext*) override {
        const auto& rgba = player_.render();
        if (image_ < 0) {
            image_ = nvgCreateImageRGBA(vg, kRenderWidth, kRenderHeight, 0,
                                        rgba.data());
        } else if (player_.has_frame()) {
            nvgUpdateImage(vg, image_, rgba.data());
        }
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 8);
        nvgFillColor(vg, nvgRGB(8, 14, 20));
        nvgFill(vg);
        if (image_ < 0 || !player_.has_frame()) return;
        const float scale = std::min(width / kRenderWidth, height / kRenderHeight);
        const float render_w = scale * kRenderWidth;
        const float render_h = scale * kRenderHeight;
        const float render_x = x + (width - render_w) * 0.5f;
        const float render_y = y + (height - render_h) * 0.5f;
        nvgBeginPath(vg);
        nvgRect(vg, render_x, render_y, render_w, render_h);
        nvgFillPaint(vg, nvgImagePattern(vg, render_x, render_y, render_w,
                                         render_h, 0, image_, 1.0f));
        nvgFill(vg);
    }
private:
    OfflineMpv& player_;
    int image_{-1};
};

brls::Label* add_label(brls::Box* root, const std::string& s, float font = 19) {
    auto* label = new brls::Label();
    label->setText(s);
    label->setFontSize(font);
    label->setMarginBottom(12);
    root->addView(label);
    return label;
}

brls::Button* add_button(brls::Box* root, const std::string& name,
                          std::function<bool(brls::View*)> action) {
    auto* button = new brls::Button();
    button->setText(name);
    button->setHeight(60);
    button->setMarginBottom(12);
    button->registerClickAction(std::move(action));
    root->addView(button);
    return button;
}

class ProbeActivity final : public brls::Activity {
public:
    explicit ProbeActivity(OfflineMpv& player) : player_(player) {}

    brls::View* createContentView() override {
        auto* scroll = new brls::ScrollingFrame();
        auto* root = new brls::Box(brls::Axis::COLUMN);
        root->setPadding(22, 26, 22, 26);
        scroll->setContentView(root);
        add_label(root, "TizenTube NX  |  M3 ISOLATED OFFLINE VIDEO PROBE", 26);
        add_label(root, "Not YouTube playback. No network or account access.", 18);
        root->addView(new ProbeVideo(player_));
        status_ = add_label(root, player_.message(), 18);
        decoder_ = add_label(root, "Decoder: not started", 18);
        add_button(root, "A - Load local test video", [this](brls::View*) {
            player_.start();
            update_labels();
            return true;
        });
        add_button(root, "Pause / Resume", [this](brls::View*) {
            player_.toggle_pause();
            return true;
        });
        add_button(root, "Seek -10 seconds", [this](brls::View*) {
            player_.seek(-10);
            return true;
        });
        add_button(root, "Seek +10 seconds", [this](brls::View*) {
            player_.seek(10);
            return true;
        });
        add_button(root, "Stop playback", [this](brls::View*) {
            player_.stop();
            update_labels();
            return true;
        });
        root->registerAction("Pause", brls::BUTTON_Y, [this](brls::View*) {
            player_.toggle_pause(); return true;
        });
        root->registerAction("Seek -10s", brls::BUTTON_LB, [this](brls::View*) {
            player_.seek(-10); return true;
        });
        root->registerAction("Seek +10s", brls::BUTTON_RB, [this](brls::View*) {
            player_.seek(10); return true;
        });
        root->registerAction("Stop", brls::BUTTON_X, [this](brls::View*) {
            player_.stop(); update_labels(); return true;
        });
        return scroll;
    }

    void tick() {
        if ((++frames_ % 45) == 0) update_labels();
    }
private:
    void update_labels() {
        if (status_) status_->setText(player_.message());
        if (decoder_) decoder_->setText(player_.stats());
    }
    OfflineMpv& player_;
    brls::Label* status_{nullptr};
    brls::Label* decoder_{nullptr};
    unsigned frames_{0};
};

} // namespace

int main(int, char**) {
    if (!brls::Application::init()) return EXIT_FAILURE;
    brls::Application::createWindow("TizenTube NX M3 Offline Probe");
    brls::Application::setGlobalQuit(true);
    OfflineMpv player;
    player.init();
    auto* app = new ProbeActivity(player);
    brls::Application::pushActivity(app);
    while (brls::Application::mainLoop()) app->tick();
    player.stop();
    return EXIT_SUCCESS;
}
