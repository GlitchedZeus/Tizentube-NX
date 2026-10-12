"""Contract gate for native Switch libmpv->software RGB->NanoVG handoff.

The host live renderer test provides dynamic SW-video evidence; this
protects the exact production wiring from regressing to audio-only draws.
"""
from pathlib import Path
import unittest

SOURCE=(Path(__file__).resolve().parents[1]/
        "switch/source/m3_player_activity.cpp").read_text()

class M3NativeRendererContract(unittest.TestCase):
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
