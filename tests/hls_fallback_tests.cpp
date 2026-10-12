#include "tizentube_nx/playback/hls_fallback.hpp"
#include "tizentube_nx/playback/hls_playback_lease.hpp"
#include <cstdlib>
#include <iostream>
int main(){
    using ttnx::playback::HlsProbeError;
    using ttnx::playback::permits_muxed_mp4_fallback;
    int failures=0;
    auto ck=[&](bool ok,const char* label){
        if(!ok){++failures;std::cerr<<"FAIL "<<label<<"\n";}
    };
    for(auto reason:{
        HlsProbeError::ManifestRejected,HlsProbeError::ManifestMimeRejected,
        HlsProbeError::ManifestLengthRejected,HlsProbeError::ManifestBodyRejected,
        HlsProbeError::MasterRejected,HlsProbeError::VariantUnavailable,
        HlsProbeError::VideoSegmentsRejected,HlsProbeError::AudioSegmentsRejected
    })ck(permits_muxed_mp4_fallback(reason),
         "safe independently reviewed fallback after HLS-only failure");
    for(auto reason:{
        HlsProbeError::None,HlsProbeError::SourceUnavailable,
        HlsProbeError::GuestTransport,HlsProbeError::GuestHttpStatus,
        HlsProbeError::GuestPlayabilityRejected,HlsProbeError::ManifestStatus,
        HlsProbeError::ManifestTransport,HlsProbeError::InvalidDuration
    })ck(!permits_muxed_mp4_fallback(reason),
         "no fallback after guest refusal or unresolved identity/network error");
    {
        ttnx::playback::HlsPlaybackLease hls;
        ttnx::playback::HlsPlaybackLease direct_mp4;
        ck(hls.acquire(),"first native media decoder can hold cache lease");
        ck(!direct_mp4.acquire(),
           "direct MP4 cannot replace already playing HLS cache");
        hls.release();
        ck(direct_mp4.acquire(),
           "safe direct MP4 player acquires lease after HLS fully closes");
        ck(!hls.acquire(),
           "HLS cannot replace active completed MP4 movie");
        direct_mp4.release();
        ck(!ttnx::playback::HlsPlaybackLease::is_active(),
           "media cache lease eventually releases on player destruction");
    }
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
