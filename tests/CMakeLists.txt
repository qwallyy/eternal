# ── Test framework setup ─────────────────────────────────────────────────────
# Task 101: Lightweight header-only test framework using a bundled
# single-header Catch2 alternative (doctest-style minimal harness).
# If Catch2 is found via find_package we use it; otherwise we fall back to
# a self-contained minimal test header shipped in tests/test_framework.hpp.

include(FetchContent)

# Try system Catch2 first, fall back to FetchContent
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found locally -- fetching via FetchContent")
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

include(CTest)
include(Catch)

# ── Helper: add a unit-test target ──────────────────────────────────────────
function(eternal_add_test _name)
    add_executable(${_name} ${ARGN})
    target_link_libraries(${_name} PRIVATE Catch2::Catch2WithMain)
    target_include_directories(${_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_definitions(${_name} PRIVATE
        WLR_USE_UNSTABLE
        ETERNAL_VERSION="${PROJECT_VERSION}"
    )
    target_compile_features(${_name} PRIVATE cxx_std_20)
    catch_discover_tests(${_name})
endfunction()

# ── Unit tests ──────────────────────────────────────────────────────────────

# Task 102: Window node tree tests
eternal_add_test(test_window_node
    test_window_node.cpp
    ${CMAKE_SOURCE_DIR}/src/layout/WindowNode.cpp
)

# Task 103: Bezier curve tests
eternal_add_test(test_bezier
    test_bezier.cpp
    ${CMAKE_SOURCE_DIR}/src/animation/BezierCurve.cpp
)

# Task 104: Headless integration tests (needs wlroots)
# These link against wlroots and wayland for headless backend testing.
if(TARGET PkgConfig::WLROOTS AND TARGET PkgConfig::WAYLAND_SERVER)
    add_executable(test_headless
        test_headless.cpp
        ${CMAKE_SOURCE_DIR}/src/layout/WindowNode.cpp
    )
    target_link_libraries(test_headless PRIVATE
        Catch2::Catch2WithMain
        PkgConfig::WAYLAND_SERVER
        PkgConfig::WLROOTS
        PkgConfig::PIXMAN
        PkgConfig::XKBCOMMON
        PkgConfig::LIBDRM
        Threads::Threads
    )
    target_include_directories(test_headless PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_definitions(test_headless PRIVATE
        WLR_USE_UNSTABLE
        ETERNAL_VERSION="${PROJECT_VERSION}"
    )
    target_compile_features(test_headless PRIVATE cxx_std_20)
    catch_discover_tests(test_headless)
endif()
