# ============================================================
# DirectX12 - d3d12.dll proxy and minimal DXGI/Present hook
# ============================================================
add_library(DirectX12 SHARED
    D3D12Wrapper.cpp
    DX12BindingTracker.cpp
    DX12BindingTrackerFrameExport.cpp
    DX12BindingTrackerResourceExport.cpp
    DX12CommandListCommandRecorder.cpp
    DX12CommandListLifecycle.cpp
    DX12CommandListStateCapture.cpp
    DX12CommandListRuntime.cpp
    DX12CommandListHooks.cpp
    DX12DeviceHooks.cpp
    DX12DispatchHookFlow.cpp
    DX12DrawHookFlow.cpp
    DX12FrameAnalysis.cpp
    DX12FrameAnalysisManifest.cpp
    DX12HookManager.cpp
    DX12IaRegionHash.cpp
    DX12IaReplacementExecutor.cpp
    DX12Input.cpp
    DX12Json.cpp
    DX12ModRuntime.cpp
    DX12Overlay.cpp
    DX12Profiling.cpp
    DX12ResourceDump.cpp
    DX12ResourceMetadataDump.cpp
    DX12ResourceTracker.cpp
    DX12ResourceTrackerHooks.cpp
    DX12ResourceTrackerQueries.cpp
    DX12Runtime.cpp
    DX12ShaderDump.cpp
    DX12ShaderHunt.cpp
    DX12State.cpp
    DXGIHooks.cpp
)

target_include_directories(DirectX12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/3rdparty
    ${CMAKE_SOURCE_DIR}/3rdparty/crc32c-hw-1.0.5/include
)

target_link_libraries(DirectX12 PRIVATE
    BunnyCore
    Nektra::NktHookLib
    crc32c
    dxgi.lib
    User32.lib
    Gdi32.lib
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
