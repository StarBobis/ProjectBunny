# ============================================================
# D3D_Shaders — 着色器汇编/反汇编工具
# ============================================================
add_executable(D3D_Shaders
    Assembler.cpp
    Shaders.cpp
    SignatureParser.cpp
)

target_include_directories(D3D_Shaders PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(D3D_Shaders PRIVATE
    d3dcompiler.lib
)

set_target_msvc_options(D3D_Shaders)
set_target_common_definitions(D3D_Shaders)

target_compile_definitions(D3D_Shaders PRIVATE
    _CONSOLE
    _LIB
)
