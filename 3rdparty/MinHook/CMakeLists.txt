# MinHook v1.3.4 (c3fcafd).
add_library(minhook STATIC src/buffer.c src/hook.c src/trampoline.c)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_sources(minhook PRIVATE src/hde/hde64.c)
else()
    target_sources(minhook PRIVATE src/hde/hde32.c)
endif()
target_include_directories(minhook PUBLIC include)
