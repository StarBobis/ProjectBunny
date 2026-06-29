# ============================================================
# DirectX12 - d3d12.dll proxy and minimal DXGI/Present hook
# ============================================================
add_library(DirectX12 SHARED
    # Entry point (root)
    D3D12Proxy.cpp

    # Core

    # UI
    DX12NativeOverlay.cpp

    # Hooks infrastructure
    DXGIHooks.cpp

    # Resource tracking
    DX12ResourceTracker.cpp
    DX12ResourceTrackerHooks.cpp
    DX12ResourceTrackerQueries.cpp

    # Binding/descriptor tracking
    DX12BindingTracker.cpp
    DX12BindingTrackerFrameExport.cpp
    DX12BindingTrackerResourceExport.cpp

    # Input Assembler (IA)

    # Shader dump & hunt
    DX12ShaderDump.cpp
    DX12ShaderHunt.cpp

    # Resource dump

    # Command list hooks & runtime
    DX12CommandListHooks.cpp
    DX12CommandListRuntime.cpp
    DX12CommandListLifecycle.cpp
    DX12CommandListCommandRecorder.cpp
    DX12CommandListStateCapture.cpp

    # Mod runtime
    DX12ModRuntime.cpp

    # 3Dmigoto config/runtime model used only by DX12
    ${CMAKE_SOURCE_DIR}/src/common/ini_parser_lite.cpp

)

target_include_directories(DirectX12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/3rdparty
    ${CMAKE_SOURCE_DIR}/3rdparty/crc32c-hw-1.0.5/include
)

target_link_libraries(DirectX12 PRIVATE
    Nektra::NktHookLib
    crc32c
    dxgi.lib
    d3dcompiler.lib
    User32.lib
    Gdi32.lib
    comctl32.lib
    Dwmapi.lib
    Shlwapi.lib
)

target_sources(DirectX12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/d3d12Wrapper.def
)

set_target_properties(DirectX12 PROPERTIES
    OUTPUT_NAME "d3d12"
)

target_compile_definitions(DirectX12 PRIVATE
    UNICODE
    _UNICODE
    _USRDLL
    MIGOTO_DX=12
)

set_target_msvc_options(DirectX12)
set_target_common_definitions(DirectX12)


