import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
from m3_diagnostics_triage import assess,parse

def events(*items):
    return "\n".join(f"{i} {kind} {status} {a} {b} {c}"
                    for i,(kind,status,a,b,c) in enumerate(items,1))+"\n"

class M3TriageTests(unittest.TestCase):
    def test_visible_frame_after_stop_is_not_playback_success(self):
        # Physical Switch October 12 first real YouTube picture flash on B.
        log=events(("SESSION","START",1,1,0),
                   ("PLAYER","START",3,0,0),
                   ("DECODE","SELECTED",1,1,0),
                   ("RENDER","FRAME",1,13,1),
                   ("RENDER","FRAME",120,13,1),
                   ("PLAYER","STOP",0,0,0),
                   ("UI","READY",1,2,0),
                   ("UI","DRAW",1,0,1),
                   ("UI","PRESENTED",1,2,1),
                   ("UI","DRAW",2,1,1))
        code,summary=assess(log)
        self.assertEqual(code,"PLAYER_ACTIVITY_NOT_VISIBLE")
        self.assertIn("only during exit",summary)

    def test_ui_watchdog_reports_invisible_root_opacity(self):
        log=events(("SESSION","START",1,1,0),
                   ("RENDER","FRAME",120,13,1),
                   ("UI","FAIL",2,120,0))
        code,summary=assess(log)
        self.assertEqual(code,"PLAYER_ACTIVITY_NOT_VISIBLE")
        self.assertIn("alpha x1000=0",summary)

    def test_healthy_draw_before_stop_is_not_invisible(self):
        log=events(("SESSION","START",1,1,0),
                   ("RENDER","FRAME",1,13,1),
                   ("UI","DRAW",1,1,1),
                   ("UI","PRESENTED",1,3,1),
                   ("PLAYER","STOP",0,0,0),
                   ("UI","DRAW",2,0,1))
        self.assertEqual(assess(log)[0],"CHECK_TEXTURE_AND_DISPLAY")

    def test_audio_only(self):
        log=events(("SESSION","START",1,1,0),
                   ("DECODE","LOADED",0,0,0),
                   ("DECODE","SELECTED",0,1,0))
        self.assertEqual(assess(log)[0],"AUDIO_ONLY_TRACKS")
    def test_video_decode_not_rendered(self):
        log=events(("SESSION","START",1,1,0),
                   ("DECODE","SELECTED",1,1,0),
                   ("DECODE","LOADED",0,0,0))
        self.assertEqual(assess(log)[0],"VIDEO_RENDER_NOT_PRODUCING_FRAMES")
    def test_frames_black(self):
        log=events(("SESSION","START",1,1,0),
                   ("DECODE","SELECTED",1,1,0),
                   ("RENDER","FRAME",8,0,1),
                   ("UI","DRAW",3,1,1))
        self.assertEqual(assess(log)[0],"BLACK_RENDERED_FRAMES")
    def test_good_frames_no_ui(self):
        log=events(("SESSION","START",1,1,0),
                   ("RENDER","FRAME",9,2,1))
        self.assertEqual(assess(log)[0],"UI_VIEW_NOT_DRAWN")
    def test_texture_display(self):
        log=events(("SESSION","START",1,1,0),
                   ("RENDER","FRAME",9,2,1),
                   ("UI","DRAW",20,1,1),
                   ("UI","PRESENTED",2,13,1))
        self.assertEqual(assess(log)[0],"CHECK_TEXTURE_AND_DISPLAY")
    def test_missing_texture_upload(self):
        log=events(("SESSION","START",1,1,0),
                   ("RENDER","FRAME",9,2,1),
                   ("UI","DRAW",20,1,1))
        self.assertEqual(assess(log)[0],"TEXTURE_UPLOAD_NOT_OBSERVED")
    def test_post_download_rename_failure(self):
        log=events(("SESSION","START",1,1,0),
                   ("TRANSFER","PROGRESS",4,9814141,9814141),
                   ("CACHE","FAIL",4,8,17),
                   ("TRANSFER","END",4,2206,9814141))
        code,summary=assess(log)
        self.assertEqual(code,"AAC_SD_PROMOTE_FAILED")
        self.assertIn("local failure",summary)
    def test_post_download_fsync_failure(self):
        log=events(("SESSION","START",1,1,0),
                   ("CACHE","FAIL",4,4,5),
                   ("TRANSFER","END",4,2206,9814141))
        self.assertEqual(assess(log)[0],"AAC_SD_SYNC_FAILED")
    def test_october_12_video_mid_transfer_302(self):
        log=events(("SESSION","START",1,1,0),
                   ("CACHE","PASS",4,0,0),
                   ("TRANSFER","END",4,206,7849910),
                   ("TRANSFER","RETRY",5,302,4),
                   ("TRANSFER","RETRY",5,302,4),
                   ("CACHE","FAIL",5,0,0),
                   ("TRANSFER","END",5,3302,11534336),
                   ("TRANSFER","STATUS",5,4,21356101))
        code,summary=assess(log)
        self.assertEqual(code,"CDN_REDIRECT_UNHANDLED")
        self.assertIn("11534336",summary)
    def test_unsafe_redirect_and_limit(self):
        for failure,expected in ((5,"UNSAFE_MEDIA_REDIRECT"),
                                 (11,"MEDIA_REDIRECT_LIMIT")):
            log=events(("SESSION","START",1,1,0),
                       ("TRANSFER","END",4,3302,0),
                       ("TRANSFER","STATUS",4,failure,1234567))
            self.assertEqual(assess(log)[0],expected)
    def test_range_failure(self):
        log=events(("SESSION","START",1,1,0),
                   ("TRANSFER","END",4,1033,4096))
        self.assertEqual(assess(log)[0],"AAC_DOWNLOAD_FAILED")
    def test_schema_ignores_raw_sensitive_text(self):
        log=events(("SESSION","START",1,1,0)) + (
            "123 SOURCE url https://secret.googlevideo.com/test?sig=x\n")
        self.assertEqual(len(parse(log)),1)
    def test_last_session_only(self):
        log=events(("SESSION","START",1,1,0),
                   ("TRANSFER","END",4,1403,0),
                   ("SESSION","START",2,1,0),
                   ("RENDER","FRAME",3,9,1),
                   ("UI","DRAW",10,1,1),
                   ("UI","PRESENTED",3,17,1))
        self.assertEqual(assess(log)[0],"CHECK_TEXTURE_AND_DISPLAY")

if __name__=="__main__":
    unittest.main()
