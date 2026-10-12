#include "tizentube_nx/playback/hls_fallback.hpp"
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
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
