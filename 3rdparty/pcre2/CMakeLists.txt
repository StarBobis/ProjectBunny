# PCRE2 10.49 (6f9d7c1), including its pinned SLJIT compiler (de0259c).
include(FetchContent)
set(PCRE2_BUILD_PCRE2GREP OFF)
set(PCRE2_BUILD_TESTS OFF)
set(PCRE2_SUPPORT_JIT ON CACHE BOOL "Shader regex JIT support" FORCE)
set(PCRE2_SHOW_REPORT OFF)
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(pcre2
    URL "${CMAKE_CURRENT_SOURCE_DIR}/pcre2-10.49.tar.gz"
    URL_HASH SHA256=44b3ed9aa5abc7eb0a95e89ac93327859f5cfab1f647b7ca89b9443b2681b6e5)
FetchContent_MakeAvailable(pcre2)
