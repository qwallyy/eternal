# Task 105: Memory leak checks - Sanitizer CMake configuration
#
# Usage:
#   cmake -DENABLE_ASAN=ON  ..    # AddressSanitizer
#   cmake -DENABLE_UBSAN=ON ..    # UndefinedBehaviorSanitizer
#   cmake -DENABLE_TSAN=ON  ..    # ThreadSanitizer
#   cmake -DENABLE_MSAN=ON  ..    # MemorySanitizer (Clang only)
#
# Note: ASAN and TSAN are mutually exclusive.
# Note: MSAN requires an entire instrumented libc++ (Clang only).

option(ENABLE_ASAN  "Enable AddressSanitizer"           OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer"  OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer"             OFF)
option(ENABLE_MSAN  "Enable MemorySanitizer (Clang)"     OFF)

# ── Validation ──────────────────────────────────────────────────────────────

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASAN and TSAN cannot be used simultaneously")
endif()

if(ENABLE_MSAN AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "MemorySanitizer requires Clang -- disabling MSAN")
    set(ENABLE_MSAN OFF)
endif()

# ── Sanitizer flags ─────────────────────────────────────────────────────────

set(SANITIZER_COMPILE_FLAGS "")
set(SANITIZER_LINK_FLAGS "")

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=address)
endif()

if(ENABLE_UBSAN)
    message(STATUS "UndefinedBehaviorSanitizer enabled")
    list(APPEND SANITIZER_COMPILE_FLAGS
        -fsanitize=undefined
        -fno-sanitize-recover=all     # make UB errors fatal
        -fno-omit-frame-pointer
    )
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=undefined)
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=thread)
endif()

if(ENABLE_MSAN)
    message(STATUS "MemorySanitizer enabled (Clang)")
    list(APPEND SANITIZER_COMPILE_FLAGS
        -fsanitize=memory
        -fno-omit-frame-pointer
        -fsanitize-memory-track-origins=2
    )
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=memory)
endif()

# ── Apply to targets ────────────────────────────────────────────────────────

# Call this function to apply sanitizer flags to a target.
function(eternal_apply_sanitizers _target)
    if(SANITIZER_COMPILE_FLAGS)
        target_compile_options(${_target} PRIVATE ${SANITIZER_COMPILE_FLAGS})
    endif()
    if(SANITIZER_LINK_FLAGS)
        target_link_options(${_target} PRIVATE ${SANITIZER_LINK_FLAGS})
    endif()
endfunction()

# ── ASAN_OPTIONS environment setup ──────────────────────────────────────────
#
# Recommended ASAN_OPTIONS for running eternal:
#
#   export ASAN_OPTIONS="detect_leaks=1:check_initialization_order=1:strict_init_order=1:detect_stack_use_after_return=1:halt_on_error=0:print_stats=1:detect_odr_violation=1"
#   export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
#   export TSAN_OPTIONS="second_deadlock_stack=1:halt_on_error=0"
#
# For suppressions, create a file at tests/asan_suppressions.txt:
#   export LSAN_OPTIONS="suppressions=tests/lsan_suppressions.txt"

# ── CI integration script stub ──────────────────────────────────────────────
#
# Add to your CI pipeline (.github/workflows/sanitizers.yml):
#
#   sanitizer-check:
#     runs-on: ubuntu-latest
#     strategy:
#       matrix:
#         sanitizer: [asan, ubsan, tsan]
#     steps:
#       - uses: actions/checkout@v4
#       - name: Install dependencies
#         run: |
#           sudo apt-get update
#           sudo apt-get install -y \
#             libwayland-dev wayland-protocols libwlroots-dev \
#             libxkbcommon-dev libinput-dev libpixman-1-dev \
#             libdrm-dev libegl-dev libgles2-mesa-dev
#       - name: Configure
#         run: |
#           cmake -B build \
#             -DCMAKE_BUILD_TYPE=Debug \
#             -DENABLE_ASAN=${{ matrix.sanitizer == 'asan' && 'ON' || 'OFF' }} \
#             -DENABLE_UBSAN=${{ matrix.sanitizer == 'ubsan' && 'ON' || 'OFF' }} \
#             -DENABLE_TSAN=${{ matrix.sanitizer == 'tsan' && 'ON' || 'OFF' }}
#       - name: Build
#         run: cmake --build build -j$(nproc)
#       - name: Test
#         run: cd build && ctest --output-on-failure
