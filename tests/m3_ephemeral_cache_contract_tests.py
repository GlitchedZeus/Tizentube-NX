"""Prevent M3 temporary-cache accumulation and unsafe fast-start changes.

This complements the actual C++ SD-cache deletion tests and compiled Switch
build; static checks alone are not proof of faster load times on hardware.
"""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
MAIN=(ROOT/"switch/source/main.cpp").read_text()
PLAYER=(ROOT/"switch/source/m3_player_activity.cpp").read_text()
MEDIA=(ROOT/"switch/source/media_file_sink.cpp").read_text()
HLS=(ROOT/"switch/source/hls_cache_sink.cpp").read_text()

class EphemeralPlaybackContract(unittest.TestCase):
    def test_abort_cleanup_and_restart_recovery(self):
        self.assertIn('if(!discard_m3_session_media()) {', MAIN)
        self.assertIn('if(!success) {',MAIN)
        self.assertIn('const bool storage_ready = ttnx::initialize_storage();',MAIN)
        self.assertIn('SwitchMediaCacheSink::discard_session_media();', MAIN)
        self.assertIn('SwitchHlsCacheSink::discard_session_media();', MAIN)
        self.assertIn('if(playback::HlsPlaybackLease::is_active())return false;', MEDIA)
        self.assertIn('if(playback::HlsPlaybackLease::is_active())return false;', HLS)

    def test_player_releases_mpv_before_erasing_cache(self):
        scope=PLAYER.split('~CachedMpv() {',1)[1].split('    bool init()',1)[0]
        self.assertLess(scope.index('close();'),scope.index('discard_session_media()'))
        self.assertIn('ready_lease_.release();',PLAYER)

    def test_no_generic_recursive_deletion_or_untrusted_file_names(self):
        media=MEDIA.split('bool SwitchMediaCacheSink::discard_session_media()',1)[1].split('void SwitchMediaCacheSink::invalidate_adaptive_pair',1)[0]
        hls=HLS.split('bool SwitchHlsCacheSink::discard_session_media()',1)[1].split('bool SwitchHlsCacheSink::begin',1)[0]
        self.assertIn('lstat(kFolder,&root)',media)
        self.assertIn('S_ISREG(entry.st_mode)',media)
        self.assertIn('lstat(kRoot,&root)',hls)
        self.assertIn('discard_known_directory(kReady)',hls)
        for forbidden in ('remove_all(', 'system(', 'std::filesystem::remove_all'):
            self.assertNotIn(forbidden,media)
            self.assertNotIn(forbidden,hls)

    def test_failed_adaptive_stream_cancels_sibling_before_publish(self):
        block=MAIN.split('auto download=[&]',1)[1].split('} else feedback=hls_reason+',1)[0]
        self.assertIn('std::atomic_bool sibling_failed{false}',block)
        self.assertIn('if(!good && sibling_failed)sibling_failed->store(true)',block)
        self.assertIn('sibling_failed && sibling_failed->load()',block)
        self.assertIn('&sibling_failed',block)
        self.assertLess(block.index('video_result.get()'),
                        block.index('Cache::publish_adaptive_pair()'))
    def test_parallel_adaptive_reads_require_two_independent_clients_and_both_successes(self):
        block=MAIN.split('} else if(have_pair){',1)[1].split('} else feedback=hls_reason+',1)[0]
        for needle in ('LibnxMediaChunkTransport video_transport',
                       'MediaChunkReader video_reader(video_transport)',
                       'std::async(std::launch::async',
                       'video_result.get()',
                       'audio_ok && video_ok &&',
                       'Cache::publish_adaptive_pair()'):
            self.assertIn(needle,block)
        self.assertLess(block.index('video_result.get()'),
                        block.index('Cache::publish_adaptive_pair()'))
        self.assertIn('if(audio_ok)video_ok=download(',block,
                      "must retain sequential fallback when second TLS unavailable")
        self.assertIn('media_parallel_adaptive_.load()',MAIN,
                      "concurrent status must not show one stream's progress for both")

if __name__=="__main__":
    unittest.main()
