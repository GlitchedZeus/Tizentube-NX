cmake_minimum_required(VERSION 3.20)
project(TizenTubeNX VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tizentube_nx_core
    src/core/url.cpp
    src/core/content_filter.cpp
    src/core/settings.cpp
)
target_include_directories(tizentube_nx_core PUBLIC include)
target_compile_options(tizentube_nx_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(tizentube_nx_host src/host/main.cpp)
target_link_libraries(tizentube_nx_host PRIVATE tizentube_nx_core)

option(TTNX_BUILD_TESTS "Build host tests" ON)
if(TTNX_BUILD_TESTS)
    enable_testing()
    add_executable(tizentube_nx_core_tests tests/core_tests.cpp)
    target_link_libraries(tizentube_nx_core_tests PRIVATE tizentube_nx_core)
    add_test(NAME core COMMAND tizentube_nx_core_tests)
    if(UNIX)
        add_executable(tizentube_nx_storage_tests tests/storage_tests.cpp switch/source/app_storage.cpp)
        target_include_directories(tizentube_nx_storage_tests PRIVATE switch/source)
        target_link_libraries(tizentube_nx_storage_tests PRIVATE tizentube_nx_core)
        add_test(NAME storage COMMAND tizentube_nx_storage_tests)
    endif()
endif()
