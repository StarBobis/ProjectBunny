# ============================================================
# D3DCompiler_46 — D3DCompiler 包装器 DLL
# ============================================================
add_library(D3DCompiler_46 SHARED
    ${CMAKE_SOURCE_DIR}/src/D3DCompiler/d3dcWrapper.cpp
    ${CMAKE_SOURCE_DIR}/src/D3DCompiler/d3dcWrapper.h
)

target_include_directories(D3DCompiler_46 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/3rdparty/crc32c-hw-1.0.5/include
)

target_link_libraries(D3DCompiler_46 PRIVATE
    d3dcompiler.lib
)

# 模块定义文件控制 DLL 导出
target_sources(D3DCompiler_46 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/D3DCompiler_46.def
)

set_target_properties(D3DCompiler_46 PROPERTIES
    OUTPUT_NAME "d3dcompiler_47"
)

set_target_msvc_options(D3DCompiler_46)
set_target_common_definitions(D3DCompiler_46)

target_compile_definitions(D3DCompiler_46 PRIVATE
    _USRDLL
)
