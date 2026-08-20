# All third-party acquisition lives here, pinned to explicit tags.
#
# Tags, not branches: a moving dependency means a build that passed yesterday
# can fail today for reasons unrelated to anything in this repo.

include(FetchContent)

# ---------------------------------------------------------------------------
# cpp-httplib -- blocking, thread-per-connection HTTP.
#
# Chosen because its chunked-transfer content provider is close to the shape
# SSE streaming needs, and because the HTTP layer is not where this server's
# concurrency lives. A ~1B Q4_K_M model on 8 CPU cores is compute-bound inside
# llama_decode; socket handling is nowhere near the bottleneck. The interesting
# decoupling is HTTP threads -> bounded queue -> one decode thread.
#
# Left header-only (HTTPLIB_COMPILE=OFF) for now: fewer moving parts. If build
# times get annoying once several translation units include it, flipping this
# on builds it as a static library instead.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.53.1
    GIT_SHALLOW    TRUE
)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# ---------------------------------------------------------------------------
# GoogleTest -- only fetched when tests are actually being built, so a
# tests-off build does no network I/O at configure time.
# ---------------------------------------------------------------------------
if(FSERVE_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.18.0
        GIT_SHALLOW    TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ---------------------------------------------------------------------------
# llama.cpp arrives in Phase 5, behind FSERVE_WITH_LLAMA (default OFF).
#
# Default-off is not just about tests. It is what keeps CI at ~90 seconds
# instead of ~15 minutes: the queue, scheduler, backpressure and shutdown
# paths are all exercised through the mock engine and need no model at all.
# ---------------------------------------------------------------------------
if(FSERVE_WITH_LLAMA)
    message(FATAL_ERROR "FSERVE_WITH_LLAMA is not wired up until Phase 5.")
endif()
