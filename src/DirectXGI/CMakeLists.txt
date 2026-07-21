# ============================================================
# DirectXGI — DXGI 包装器 DLL
# ============================================================
add_library(dxgi SHARED
    DXGIWrapper.cpp
)

target_include_directories(dxgi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/3rdparty/DirectXTK/Inc
)

target_link_libraries(dxgi PRIVATE
    Nektra::NktHookLib
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    comctl32.lib
    $<$<NOT:$<CONFIG:Debug>>:dinput8.lib>
    XINPUT9_1_0.lib
)

target_compile_definitions(dxgi PRIVATE
    DXGI_LOGGING=1
    DIRECTINPUT_VERSION=0x0800
    _USRDLL
    DIRECTX11_EXPORTS
    $<$<CONFIG:Debug>:_DEBUG_LAYER=1>
    $<$<CONFIG:Debug>:HOOK_SYSTEM32=1>
)

# 模块定义文件
target_sources(dxgi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/d3dxgiWrapper.def
)
target_link_options(dxgi PRIVATE
    /INCLUDE:D3D11CreateDevice
)

set_target_properties(dxgi PROPERTIES
    OUTPUT_NAME "dxgi"
)

set_target_msvc_options(dxgi)
set_target_common_definitions(dxgi)
