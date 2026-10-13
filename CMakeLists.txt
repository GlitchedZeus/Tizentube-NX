cmake_minimum_required(VERSION 3.20)
project(TizenTubeNX VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tizentube_nx_core
    src/core/url.cpp
    src/core/content_filter.cpp
    src/core/search_result.cpp
    src/core/search_presentation.cpp
    src/core/search_error.cpp
    src/core/search_model.cpp
    src/core/home_model.cpp
    src/core/channel_model.cpp
    src/core/guest_browse.cpp
    src/core/guest_api.cpp
    src/core/session_bootstrap.cpp
    src/core/browse_response.cpp
    src/core/search_response.cpp
    src/core/home_response.cpp
    src/core/guest_search.cpp
    src/core/guest_home.cpp
    src/core/guest_channel.cpp
    src/core/search_flow.cpp
    src/core/diagnostics.cpp
    src/core/outbound_policy.cpp
    src/core/http1.cpp
    src/core/settings.cpp
    src/core/playback.cpp
    src/core/media_chunks.cpp
    src/core/media_download.cpp
    src/core/guest_direct_extractor.cpp
    src/core/hls_manifest.cpp
    src/core/hls_probe.cpp
    src/core/hls_segment.cpp
    src/core/hls_cache.cpp
    src/core/ts_streams.cpp
    src/core/progressive_prepare.cpp
    src/core/source_routes.cpp
    src/core/ump_wire.cpp
    src/core/ump_safety.cpp
    src/core/ump_transfer.cpp
    src/core/extraction_wire.cpp
    src/core/sponsorblock.cpp
)
target_include_directories(tizentube_nx_core PUBLIC include)
target_compile_options(tizentube_nx_core PRIVATE -Wall -Wextra -Wpedantic)
# Only core parser implementation files may see the unscoped renderer-payload
# parser declaration. Application/network callers must use the scoped facade.
target_compile_definitions(tizentube_nx_core PRIVATE TTNX_INTERNAL_SEARCH_PARSER=1)

add_executable(tizentube_nx_host src/host/main.cpp)
target_link_libraries(tizentube_nx_host PRIVATE tizentube_nx_core)

option(TTNX_BUILD_TESTS "Build host tests" ON)
if(TTNX_BUILD_TESTS)
    enable_testing()
    add_executable(tizentube_nx_media_chunks_tests tests/media_chunks_tests.cpp)
    target_link_libraries(tizentube_nx_media_chunks_tests PRIVATE tizentube_nx_core)
    add_test(NAME media_chunks COMMAND tizentube_nx_media_chunks_tests)

    add_executable(tizentube_nx_ump_transfer_tests tests/ump_transfer_tests.cpp)
    target_link_libraries(tizentube_nx_ump_transfer_tests PRIVATE tizentube_nx_core)
    add_test(NAME ump_transfer COMMAND tizentube_nx_ump_transfer_tests)

    add_executable(tizentube_nx_ump_wire_tests tests/ump_wire_tests.cpp)
    target_link_libraries(tizentube_nx_ump_wire_tests PRIVATE tizentube_nx_core)
    add_test(NAME ump_wire COMMAND tizentube_nx_ump_wire_tests)

    add_executable(tizentube_nx_ts_streams_tests tests/ts_streams_tests.cpp)
    target_link_libraries(tizentube_nx_ts_streams_tests PRIVATE tizentube_nx_core)
    add_test(NAME ts_streams COMMAND tizentube_nx_ts_streams_tests)

    add_executable(tizentube_nx_audio_gate_tests tests/audio_gate_tests.cpp)
    target_link_libraries(tizentube_nx_audio_gate_tests PRIVATE tizentube_nx_core)
    add_test(NAME audio_gate COMMAND tizentube_nx_audio_gate_tests)

    add_executable(tizentube_nx_aac_mp4_structure_tests tests/aac_mp4_structure_tests.cpp)
    target_link_libraries(tizentube_nx_aac_mp4_structure_tests PRIVATE tizentube_nx_core)
    add_test(NAME aac_mp4_structure COMMAND tizentube_nx_aac_mp4_structure_tests)

    add_executable(tizentube_nx_hls_event_playlist_tests tests/hls_event_playlist_tests.cpp)
    target_link_libraries(tizentube_nx_hls_event_playlist_tests PRIVATE tizentube_nx_core)
    add_test(NAME hls_event_playlist COMMAND tizentube_nx_hls_event_playlist_tests)

    add_executable(tizentube_nx_hls_startup_buffer_tests tests/hls_startup_buffer_tests.cpp)
    target_link_libraries(tizentube_nx_hls_startup_buffer_tests PRIVATE tizentube_nx_core)
    add_test(NAME hls_startup_buffer COMMAND tizentube_nx_hls_startup_buffer_tests)

    add_executable(tizentube_nx_hls_fallback_tests tests/hls_fallback_tests.cpp)
    target_link_libraries(tizentube_nx_hls_fallback_tests PRIVATE tizentube_nx_core)
    add_test(NAME hls_fallback COMMAND tizentube_nx_hls_fallback_tests)

    add_executable(tizentube_nx_hls_cache_tests tests/hls_cache_tests.cpp)
    target_link_libraries(tizentube_nx_hls_cache_tests PRIVATE tizentube_nx_core)
    add_test(NAME hls_cache COMMAND tizentube_nx_hls_cache_tests)

    add_executable(tizentube_nx_hls_segment_tests tests/hls_segment_tests.cpp)
    target_link_libraries(tizentube_nx_hls_segment_tests PRIVATE tizentube_nx_core)
    add_test(NAME hls_segment COMMAND tizentube_nx_hls_segment_tests)

    add_executable(tizentube_nx_hls_probe_tests tests/hls_probe_tests.cpp)
    target_link_libraries(tizentube_nx_hls_probe_tests PRIVATE tizentube_nx_core)
    add_test(NAME hls_probe COMMAND tizentube_nx_hls_probe_tests)

    add_executable(tizentube_nx_hls_manifest_tests tests/hls_manifest_tests.cpp)
    target_link_libraries(tizentube_nx_hls_manifest_tests PRIVATE tizentube_nx_core)
    add_test(NAME hls_manifest COMMAND tizentube_nx_hls_manifest_tests)

    add_executable(tizentube_nx_source_routes_tests tests/source_routes_tests.cpp)
    target_link_libraries(tizentube_nx_source_routes_tests PRIVATE tizentube_nx_core)
    add_test(NAME source_routes COMMAND tizentube_nx_source_routes_tests)

    add_executable(tizentube_nx_progressive_prepare_tests tests/progressive_prepare_tests.cpp)
    target_link_libraries(tizentube_nx_progressive_prepare_tests PRIVATE tizentube_nx_core)
    add_test(NAME progressive_prepare COMMAND tizentube_nx_progressive_prepare_tests)

    add_executable(tizentube_nx_guest_direct_player_tests tests/guest_direct_player_tests.cpp)
    target_link_libraries(tizentube_nx_guest_direct_player_tests PRIVATE tizentube_nx_core)
    add_test(NAME guest_direct_player COMMAND tizentube_nx_guest_direct_player_tests)

    add_executable(tizentube_nx_media_download_tests tests/media_download_tests.cpp)
    target_link_libraries(tizentube_nx_media_download_tests PRIVATE tizentube_nx_core)
    add_test(NAME media_download COMMAND tizentube_nx_media_download_tests)

    add_executable(tizentube_nx_core_tests tests/core_tests.cpp)
    target_link_libraries(tizentube_nx_core_tests PRIVATE tizentube_nx_core)
    add_test(NAME core COMMAND tizentube_nx_core_tests)

    add_executable(tizentube_nx_search_result_tests tests/search_result_tests.cpp)
    target_link_libraries(tizentube_nx_search_result_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_result COMMAND tizentube_nx_search_result_tests)

    add_executable(tizentube_nx_search_presentation_tests tests/search_presentation_tests.cpp)
    target_link_libraries(tizentube_nx_search_presentation_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_presentation COMMAND tizentube_nx_search_presentation_tests)

    add_executable(tizentube_nx_search_error_tests tests/search_error_tests.cpp)
    target_link_libraries(tizentube_nx_search_error_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_error COMMAND tizentube_nx_search_error_tests)

    add_executable(tizentube_nx_search_model_tests tests/search_model_tests.cpp)
    target_link_libraries(tizentube_nx_search_model_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_model COMMAND tizentube_nx_search_model_tests)

    add_executable(tizentube_nx_home_model_tests tests/home_model_tests.cpp)
    target_link_libraries(tizentube_nx_home_model_tests PRIVATE tizentube_nx_core)
    add_test(NAME home_model COMMAND tizentube_nx_home_model_tests)

    add_executable(tizentube_nx_url_tests tests/url_tests.cpp)
    target_link_libraries(tizentube_nx_url_tests PRIVATE tizentube_nx_core)
    add_test(NAME url COMMAND tizentube_nx_url_tests)

    add_executable(tizentube_nx_network_policy_tests tests/network_policy_tests.cpp)
    target_link_libraries(tizentube_nx_network_policy_tests PRIVATE tizentube_nx_core)
    add_test(NAME network_policy COMMAND tizentube_nx_network_policy_tests)

    add_executable(tizentube_nx_service_init_tests tests/service_init_tests.cpp)
    target_link_libraries(tizentube_nx_service_init_tests PRIVATE tizentube_nx_core)
    add_test(NAME service_init COMMAND tizentube_nx_service_init_tests)

    add_executable(tizentube_nx_http1_tests tests/http1_tests.cpp)
    target_link_libraries(tizentube_nx_http1_tests PRIVATE tizentube_nx_core)
    add_test(NAME http1 COMMAND tizentube_nx_http1_tests)

    add_executable(tizentube_nx_browse_response_tests tests/browse_response_tests.cpp)
    target_compile_definitions(tizentube_nx_browse_response_tests PRIVATE TTNX_INTERNAL_SEARCH_PARSER=1)
    target_link_libraries(tizentube_nx_browse_response_tests PRIVATE tizentube_nx_core)
    add_test(NAME browse_response COMMAND tizentube_nx_browse_response_tests)

    add_executable(tizentube_nx_search_response_tests tests/search_response_tests.cpp)
    target_link_libraries(tizentube_nx_search_response_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_response COMMAND tizentube_nx_search_response_tests)

    add_executable(tizentube_nx_home_response_tests tests/home_response_tests.cpp)
    target_link_libraries(tizentube_nx_home_response_tests PRIVATE tizentube_nx_core)
    add_test(NAME home_response COMMAND tizentube_nx_home_response_tests)

    add_executable(tizentube_nx_search_metadata_tests tests/search_metadata_tests.cpp)
    target_link_libraries(tizentube_nx_search_metadata_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_metadata COMMAND tizentube_nx_search_metadata_tests)

    add_executable(tizentube_nx_search_metadata_edge_tests tests/search_metadata_edge_tests.cpp)
    target_link_libraries(tizentube_nx_search_metadata_edge_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_metadata_edge COMMAND tizentube_nx_search_metadata_edge_tests)

    add_executable(tizentube_nx_search_thumbnail_tests tests/search_thumbnail_tests.cpp)
    target_link_libraries(tizentube_nx_search_thumbnail_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_thumbnail COMMAND tizentube_nx_search_thumbnail_tests)

    add_executable(tizentube_nx_search_robustness_tests tests/search_robustness_tests.cpp)
    target_link_libraries(tizentube_nx_search_robustness_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_robustness COMMAND tizentube_nx_search_robustness_tests)

    add_executable(tizentube_nx_search_mutation_tests tests/search_mutation_tests.cpp)
    target_link_libraries(tizentube_nx_search_mutation_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_mutation COMMAND tizentube_nx_search_mutation_tests)

    add_executable(tizentube_nx_guest_search_tests tests/guest_search_tests.cpp)
    target_link_libraries(tizentube_nx_guest_search_tests PRIVATE tizentube_nx_core)
    add_test(NAME guest_search COMMAND tizentube_nx_guest_search_tests)

    add_executable(tizentube_nx_guest_home_tests tests/guest_home_tests.cpp)
    target_link_libraries(tizentube_nx_guest_home_tests PRIVATE tizentube_nx_core)
    add_test(NAME guest_home COMMAND tizentube_nx_guest_home_tests)

    add_executable(tizentube_nx_channel_model_tests tests/channel_model_tests.cpp)
    target_link_libraries(tizentube_nx_channel_model_tests PRIVATE tizentube_nx_core)
    add_test(NAME channel_model COMMAND tizentube_nx_channel_model_tests)

    add_executable(tizentube_nx_channel_response_tests tests/channel_response_tests.cpp)
    target_link_libraries(tizentube_nx_channel_response_tests PRIVATE tizentube_nx_core)
    add_test(NAME channel_response COMMAND tizentube_nx_channel_response_tests)

    add_executable(tizentube_nx_guest_channel_tests tests/guest_channel_tests.cpp)
    target_link_libraries(tizentube_nx_guest_channel_tests PRIVATE tizentube_nx_core)
    add_test(NAME guest_channel COMMAND tizentube_nx_guest_channel_tests)

    add_executable(tizentube_nx_search_executor_error_tests tests/search_executor_error_tests.cpp)
    target_link_libraries(tizentube_nx_search_executor_error_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_executor_error COMMAND tizentube_nx_search_executor_error_tests)

    add_executable(tizentube_nx_search_flow_tests tests/search_flow_tests.cpp)
    target_link_libraries(tizentube_nx_search_flow_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_flow COMMAND tizentube_nx_search_flow_tests)

    add_executable(tizentube_nx_search_worker_guard_tests tests/search_worker_guard_tests.cpp)
    target_link_libraries(tizentube_nx_search_worker_guard_tests PRIVATE tizentube_nx_core)
    add_test(NAME search_worker_guard COMMAND tizentube_nx_search_worker_guard_tests)

    add_executable(tizentube_nx_request_privacy_tests tests/request_privacy_tests.cpp)
    target_link_libraries(tizentube_nx_request_privacy_tests PRIVATE tizentube_nx_core)
    add_test(NAME request_privacy COMMAND tizentube_nx_request_privacy_tests)

    # Header-only bounded streaming scheduler: deterministic host coverage.
    add_executable(tizentube_nx_rolling_media_budget_tests
        tests/rolling_media_budget_tests.cpp)
    add_test(NAME rolling_media_budget COMMAND tizentube_nx_rolling_media_budget_tests)

    add_executable(tizentube_nx_local_live_hls_gate_tests
        tests/local_live_hls_gate_tests.cpp)
    add_test(NAME local_live_hls_gate
        COMMAND tizentube_nx_local_live_hls_gate_tests)

    add_executable(tizentube_nx_rolling_stream_coordinator_tests
        tests/rolling_stream_coordinator_tests.cpp)
    add_test(NAME rolling_stream_coordinator
        COMMAND tizentube_nx_rolling_stream_coordinator_tests)

    add_executable(tizentube_nx_playback_foundation_tests tests/playback_foundation_tests.cpp)
    target_link_libraries(tizentube_nx_playback_foundation_tests PRIVATE tizentube_nx_core)
    add_test(NAME playback_foundation COMMAND tizentube_nx_playback_foundation_tests)

    if(UNIX)
        add_executable(tizentube_nx_m3_diagnostics_tests
            tests/m3_diagnostics_tests.cpp switch/source/m3_diagnostics.cpp)
        target_include_directories(tizentube_nx_m3_diagnostics_tests PRIVATE switch/source)
        target_compile_definitions(tizentube_nx_m3_diagnostics_tests PRIVATE
            TTNX_M3_DIAG_HOST_TEST=1)
        add_test(NAME m3_diagnostics COMMAND tizentube_nx_m3_diagnostics_tests)

        add_executable(tizentube_nx_adaptive_sd_cache_tests
            tests/adaptive_sd_cache_tests.cpp switch/source/media_file_sink.cpp)
        target_include_directories(tizentube_nx_adaptive_sd_cache_tests PRIVATE switch/source)
        target_compile_definitions(tizentube_nx_adaptive_sd_cache_tests PRIVATE
            TTNX_M3_MEDIA_PLANE=1 TTNX_M3_CACHE_HOST_TEST=1)
        target_link_libraries(tizentube_nx_adaptive_sd_cache_tests PRIVATE tizentube_nx_core)
        add_test(NAME adaptive_sd_cache COMMAND tizentube_nx_adaptive_sd_cache_tests)

        add_executable(tizentube_nx_hls_sd_cache_tests
            tests/hls_sd_cache_tests.cpp switch/source/hls_cache_sink.cpp)
        target_include_directories(tizentube_nx_hls_sd_cache_tests PRIVATE switch/source)
        target_compile_definitions(tizentube_nx_hls_sd_cache_tests PRIVATE
            TTNX_M3_MEDIA_PLANE=1 TTNX_M3_CACHE_HOST_TEST=1)
        target_link_libraries(tizentube_nx_hls_sd_cache_tests PRIVATE tizentube_nx_core)
        add_test(NAME hls_sd_cache COMMAND tizentube_nx_hls_sd_cache_tests)

        # Host-test native rolling producer thread: no UI-thread HTTP, no
        # spin under backpressure, join before media deletion.
        find_package(Threads REQUIRED)
        add_executable(tizentube_nx_rolling_hls_worker_tests
            tests/rolling_hls_worker_tests.cpp
            switch/source/rolling_hls_worker.cpp)
        target_include_directories(tizentube_nx_rolling_hls_worker_tests
            PRIVATE switch/source)
        target_compile_definitions(tizentube_nx_rolling_hls_worker_tests
            PRIVATE TTNX_M4_ROLLING_HOST_TEST=1)
        target_link_libraries(tizentube_nx_rolling_hls_worker_tests
            PRIVATE Threads::Threads)
        add_test(NAME rolling_hls_worker
            COMMAND tizentube_nx_rolling_hls_worker_tests)

        # Native media/player lifetime owned as one durable transaction.
        # A decoder lease cannot be released until downloads join and the
        # renderer closes; no timestamp-guessed segment retirement.
        add_executable(tizentube_nx_rolling_playback_owner_tests
            tests/rolling_playback_owner_tests.cpp
            switch/source/rolling_playback_owner.cpp
            switch/source/rolling_hls_worker.cpp)
        target_include_directories(tizentube_nx_rolling_playback_owner_tests
            PRIVATE switch/source)
        target_compile_definitions(tizentube_nx_rolling_playback_owner_tests
            PRIVATE TTNX_M4_ROLLING_HOST_TEST=1)
        target_link_libraries(tizentube_nx_rolling_playback_owner_tests
            PRIVATE Threads::Threads)
        add_test(NAME rolling_playback_owner
            COMMAND tizentube_nx_rolling_playback_owner_tests)

        # Opt-in file-backed HLS rolling cache. Accepted M3 path unchanged.
        add_executable(tizentube_nx_rolling_hls_sink_tests
            tests/rolling_hls_sink_tests.cpp switch/source/rolling_hls_sink.cpp)
        target_include_directories(tizentube_nx_rolling_hls_sink_tests
            PRIVATE switch/source)
        target_compile_definitions(tizentube_nx_rolling_hls_sink_tests
            PRIVATE TTNX_M4_ROLLING_HOST_TEST=1)
        add_test(NAME rolling_hls_sink COMMAND tizentube_nx_rolling_hls_sink_tests)

        # Opt-in transport-to-durable-HLS bridge. Real HlsSegmentReader
        # still enforces the approved HTTPS/CDN and response contracts.
        add_executable(tizentube_nx_rolling_muxed_hls_tests
            tests/rolling_muxed_hls_tests.cpp
            switch/source/rolling_hls_feed.cpp
            switch/source/rolling_hls_sink.cpp
            switch/source/rolling_hls_worker.cpp
            switch/source/rolling_playback_owner.cpp)
        target_include_directories(tizentube_nx_rolling_muxed_hls_tests
            PRIVATE switch/source)
        target_compile_definitions(tizentube_nx_rolling_muxed_hls_tests
            PRIVATE TTNX_M4_ROLLING_HOST_TEST=1)
        target_link_libraries(tizentube_nx_rolling_muxed_hls_tests
            PRIVATE tizentube_nx_core Threads::Threads)
        add_test(NAME rolling_muxed_hls COMMAND tizentube_nx_rolling_muxed_hls_tests)

        add_executable(tizentube_nx_storage_tests tests/storage_tests.cpp switch/source/app_storage.cpp)
        target_include_directories(tizentube_nx_storage_tests PRIVATE switch/source)
        target_link_libraries(tizentube_nx_storage_tests PRIVATE tizentube_nx_core)
        add_test(NAME storage COMMAND tizentube_nx_storage_tests)
    endif()
endif()
