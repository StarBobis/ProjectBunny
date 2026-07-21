# ============================================================
# crc32c-hw — 硬件加速 CRC32C 库
# ============================================================
add_library(crc32c STATIC
    src/crc32c.cpp
)

target_include_directories(crc32c PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(crc32c PUBLIC CRC32C_STATIC=1)

set_target_msvc_options(crc32c)
set_target_common_definitions(crc32c)
