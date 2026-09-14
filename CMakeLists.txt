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

    if(UNIX)
        add_executable(tizentube_nx_storage_tests tests/storage_tests.cpp switch/source/app_storage.cpp)
        target_include_directories(tizentube_nx_storage_tests PRIVATE switch/source)
        target_link_libraries(tizentube_nx_storage_tests PRIVATE tizentube_nx_core)
        add_test(NAME storage COMMAND tizentube_nx_storage_tests)
    endif()
endif()
