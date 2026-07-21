# ============================================================
# HLSLDecompiler — HLSL 反编译库
# 目前作为 OBJECT 库供 DirectX11 链接
# ============================================================
add_library(HLSLDecompiler OBJECT
    DecompileHLSL.cpp
)

target_include_directories(HLSLDecompiler PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(HLSLDecompiler PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
)

set_target_msvc_options(HLSLDecompiler)
set_target_common_definitions(HLSLDecompiler)
