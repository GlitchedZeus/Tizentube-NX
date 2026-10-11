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
#include <cstddef>
#include <cstdint>
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
        // A failure to apply a mandatory safety option is fatal, not an
        // invitation to run mpv with a permissive default configuration.
        auto require_option = [this](const char* name, const char* value) {
            if (mpv_set_option_string(handle_, name, value) >= 0) return true;
            message_ = std::string("Unsupported safety/player option: ") + name;
            return false;
        };
        if (!require_option("config", "no") ||
            !require_option("load-scripts", "no") ||
            !require_option("ytdl", "no") ||
            !require_option("input-default-bindings", "no") ||
            // FFmpeg must not follow nested manifests/network references.
            !require_option("demuxer-lavf-o", "protocol_whitelist=file,pipe") ||
            !require_option("vo", "libmpv") ||
            !require_option("hwdec", "auto") ||
            !require_option("audio-channels", "stereo")) {
            shutdown();
            return false;
        }
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
        // A deliberately small experimental media decoder must not accept
        // arbitrarily huge inputs. Check the SD fixture before handing it
        // to FFmpeg (the decoder does its own format validation afterwards).
        constexpr long kMaxFixtureBytes = 256L * 1024L * 1024L;
        const bool sized = std::fseek(file, 0, SEEK_END) == 0;
        const long bytes = sized ? std::ftell(file) : -1;
        std::fclose(file);
        if (bytes < 1024 || bytes > kMaxFixtureBytes) {
            message_ = "Offline MP4 fixture missing data or exceeds 256 MiB.";
            return false;
        }
        // Keep every action constrained to this known local file. The user
        // cannot supply an arbitrary URL, playlist or media host.
        const char* command[] = {"loadfile", kFixture, "replace", nullptr};
        have_frame_ = false;
        new_frame_ = false;
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
        have_frame_ = false;
        new_frame_ = false;
        message_ = "Stopped.";
    }


    void pump_events() {
        if (!handle_) return;
        // mpv has its own threads; no event callback ever touches Borealis.
        // Poll only on the main UI thread, bound work per frame.
        for (unsigned i = 0; i < 32; ++i) {
            const mpv_event* event = mpv_wait_event(handle_, 0);
            if (!event || event->event_id == MPV_EVENT_NONE) break;
            if (event->event_id == MPV_EVENT_FILE_LOADED) {
                message_ = "Local MP4 loaded. Inspect moving video/audio.";
            } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) {
                message_ = "Playback active. Decoder status shown below.";
            } else if (event->event_id == MPV_EVENT_END_FILE) {
                auto* end = static_cast<const mpv_event_end_file*>(event->data);
                message_ = (end && end->reason == MPV_END_FILE_REASON_ERROR)
                    ? "Local file decoding failed (see exact build debug symbols)."
                    : "Playback reached end/stopped.";
            }
        }
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
        const char* video = mpv_get_property_string(handle_, "video-codec");
        const char* audio = mpv_get_property_string(handle_, "audio-codec");
        const std::string video_name = video ? video : "--";
        const std::string audio_name = audio ? audio : "--";
        if (video) mpv_free(const_cast<char*>(video));
        if (audio) mpv_free(const_cast<char*>(audio));
        return "Decoder: " + decoder +
               "  |  Video: " + video_name +
               "  |  Audio: " + audio_name +
               "  |  time: " +
               (has_time ? std::to_string(static_cast<int>(seconds)) + "s" : "--") +
               "  |  rendered frames: " + std::to_string(frame_count_);
    }

    // The pinned Borealis video context has no public Deko3D framebuffer
    // accessors. This intentionally tests mpv's software render API first;
    // it does NOT prove hardware decoder usage or 1080p performance.
    const std::vector<unsigned char>& render() {
        if (!render_) return pixels_;
        new_frame_ = false;
        const auto flags = mpv_render_context_update(render_);
        if (flags & MPV_RENDER_UPDATE_FRAME) {
            int size[2]{kRenderWidth, kRenderHeight};
            std::size_t stride = static_cast<std::size_t>(kRenderWidth) * 4;
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_SW_SIZE, size},
                {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char*>("rgb0")},
                {MPV_RENDER_PARAM_SW_STRIDE, &stride},
                {MPV_RENDER_PARAM_SW_POINTER, pixels_.data()},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };
            if (mpv_render_context_render(render_, params) >= 0) {
                // libmpv's guaranteed "rgb0" format leaves byte 3 undefined.
                // NanoVG's RGBA upload requires fully opaque alpha.
                for (std::size_t i = 3; i < pixels_.size(); i += 4)
                    pixels_[i] = 255;
                have_frame_ = true;
                new_frame_ = true;
                ++frame_count_;
                mpv_render_context_report_swap(render_);
            }
        }
        return pixels_;
    }
    bool has_frame() const { return have_frame_; }
    bool new_frame() const { return new_frame_; }

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
    bool new_frame_{false};
    std::uint64_t frame_count_{0};
};

class ProbeVideo : public brls::View {
public:
    explicit ProbeVideo(OfflineMpv& player) : player_(player) {
        setHeight(400);
    }

    // The one-image allocation is released with this dedicated NRO process.
    // Never touch a global NanoVG context during uncertain app teardown.

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style, brls::FrameContext*) override {
        const auto& rgba = player_.render();
        if (image_ < 0) {
            image_ = nvgCreateImageRGBA(vg, kRenderWidth, kRenderHeight, 0,
                                        rgba.data());
        } else if (player_.new_frame()) {
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
        player_.pump_events();
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
