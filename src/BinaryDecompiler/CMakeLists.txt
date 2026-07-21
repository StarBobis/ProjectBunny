# ============================================================
# BinaryDecompiler — 二进制着色器反编译静态库
# ============================================================
add_library(BinaryDecompiler STATIC
    decode.cpp
    decodeDX9.cpp
    reflect.cpp
)

target_include_directories(BinaryDecompiler PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/internal_includes
    ${CMAKE_SOURCE_DIR}/src/common
)

set_target_msvc_options(BinaryDecompiler)
set_target_common_definitions(BinaryDecompiler)
