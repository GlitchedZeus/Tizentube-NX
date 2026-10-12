import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
from m3_diagnostics_triage import assess,parse

def events(*items):
    return "\n".join(f"{i} {kind} {status} {a} {b} {c}"
                    for i,(kind,status,a,b,c) in enumerate(items,1))+"\n"

class M3TriageTests(unittest.TestCase):
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
