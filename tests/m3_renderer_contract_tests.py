"""Contract gate for native Switch libmpv->software RGB->NanoVG handoff.

The host live renderer test provides dynamic SW-video evidence; this
protects the exact production wiring from regressing to audio-only draws.
"""
from pathlib import Path
import unittest

SOURCE=(Path(__file__).resolve().parents[1]/
        "switch/source/m3_player_activity.cpp").read_text()

class M3NativeRendererContract(unittest.TestCase):
    def test_downloads_erased_only_after_decoder_release(self):
        self.assertIn("const bool files=SwitchMediaCacheSink::discard_session_media()",SOURCE)
        self.assertIn("const bool hls=SwitchHlsCacheSink::discard_session_media()",SOURCE)
        destructor=SOURCE.split("~CachedMpv() {",1)[1].split("    bool init()",1)[0]
        self.assertLess(destructor.index("close();"),
                        destructor.index("discard_session_media()"))
        self.assertIn("ready_lease_.release();",SOURCE)

    def test_pinned_borealis_opaque_activity_fade_is_primed(self):
        # The pinned upstream View::show() returns early for !hidden,
        # leaving pushActivity()'s initial alpha=0 untouched. This is
        # exactly what the Oct 12 physical Switch log/screenshot proved.
        activity=SOURCE.split("class M3Activity final",1)[1]
        create=activity.split("brls::View* createContentView()",1)[1].split("    void tick()",1)[0]
        self.assertIn("root->hide([]{},false,0.0f);",create)
        self.assertLess(create.index("root->hide([]{},false,0.0f);"),
                        create.index("return root;"))
        # Minimal state-model regression for pinned Borealis semantics.
        def simulate(hidden_before_push):
            hidden, alpha=hidden_before_push, 0.0  # new activity alpha reset by push
            if hidden:  # View::show's animated branch
                hidden, alpha=False, 1.0
            return alpha
        self.assertEqual(simulate(False),0.0)  # reproduced invisible activity
        self.assertEqual(simulate(True),1.0)   # repaired visible activity

    def test_hardware_accepted_video_has_no_calibration_overlay(self):
        view=SOURCE.split("class VideoFrame final",1)[1].split("brls::Label* label",1)[0]
        self.assertNotIn("const NVGcolor colors[]",view)
        self.assertIn("nvgFillPaint(vg,nvgImagePattern",view)
    def test_video_has_independent_presentation_watchdog(self):
        self.assertIn("player_.frames_rendered()>=90",SOURCE)
        self.assertIn("video_->draw_count()==0",SOURCE)
        self.assertIn("M3Diagnostics::record(M3Stage::Ui,M3Event::Fail,2,",SOURCE)
        self.assertIn("nvgDeleteImage(brls::Application::getNVGContext(),image_)",SOURCE)

    def test_mpv_callback_is_registered_and_unregistered(self):
        self.assertIn("mpv_render_context_set_update_callback(",SOURCE)
        self.assertIn("renderer_,&CachedMpv::notify_render_update,this",SOURCE)
        self.assertIn("mpv_render_context_set_update_callback(renderer_,nullptr,nullptr)",SOURCE)
        callback=SOURCE.split("static void notify_render_update(void* context)",1)[1].split("void close()",1)[0]
        self.assertIn("fetch_add(1,std::memory_order_release)",callback)
        for forbidden in ("mpv_command(", "mpv_get_property(", "nvg", "M3Diagnostics::record"):
            self.assertNotIn(forbidden,callback)
    def test_video_frame_pumped_from_ui_tick_not_only_widget_draw(self):
        self.assertIn("player_.pump();\n        // Software frame dequeue",SOURCE)
        self.assertIn("(void)player_.render();",SOURCE)
        render_view=SOURCE.split("class VideoFrame final",1)[1].split("brls::Label* label",1)[0]
        self.assertIn("player_.pixels()",render_view)
        self.assertNotIn("player_.render()",render_view)
    def test_software_renderer_disables_incompatible_hardware_surfaces(self):
        self.assertIn('!required("hwdec","no")',SOURCE)
        self.assertNotIn('!required("hwdec","auto")',SOURCE)

    def test_unuploaded_frame_latches_until_texture_upload(self):
        renderer=SOURCE.split("const std::vector<unsigned char>& render()",1)[1].split("    bool have_frame()const",1)[0]
        self.assertNotIn("new_frame_=false;",renderer)
        self.assertIn("new_frame_=true;",renderer)
        self.assertIn("player_.acknowledge_frame_upload();",SOURCE)
        self.assertIn("nvgUpdateImage(vg,image_,pixels.data())",SOURCE)
        self.assertIn("mpv_render_context_report_swap(renderer_)",SOURCE)
        self.assertIn("for(std::size_t i=3;i<pixels_.size();i+=4)pixels_[i]=255",SOURCE)

if __name__=="__main__":
    unittest.main()
