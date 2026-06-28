# ============================================================
# DirectX12 - d3d12.dll proxy and minimal DXGI/Present hook
# ============================================================
add_library(DirectX12 SHARED
    # Entry point (root)
    D3D12Wrapper.cpp

    # Core
    core/DX12Runtime.cpp
    core/DX12Input.cpp
    core/DX12Overlay.cpp

    # UI
    ui/DX12NativeOverlay.cpp
    ui/DX12UiActions.cpp

    # Hooks infrastructure
    hooks/DX12DeviceHooks.cpp
    hooks/DX12DispatchHookFlow.cpp
    hooks/DX12DrawHookFlow.cpp
    hooks/DX12HookManager.cpp
    hooks/DXGIHooks.cpp

    # Resource tracking
    resource_tracker/DX12ResourceTracker.cpp
    resource_tracker/DX12ResourceTrackerHooks.cpp
    resource_tracker/DX12ResourceTrackerQueries.cpp

    # Binding/descriptor tracking
    binding_tracker/DX12BindingTracker.cpp
    binding_tracker/DX12BindingTrackerFrameExport.cpp
    binding_tracker/DX12BindingTrackerResourceExport.cpp

    # Input Assembler (IA)

    # Shader dump & hunt
    shader/DX12ShaderDump.cpp
    shader/DX12ShaderHunt.cpp

    # Resource dump

    # Command list hooks & runtime
    command_list/DX12CommandListHooks.cpp
    command_list/DX12CommandListRuntime.cpp
    command_list/DX12CommandListLifecycle.cpp
    command_list/DX12CommandListCommandRecorder.cpp
    command_list/DX12CommandListStateCapture.cpp

    # Mod runtime
    mod_runtime/DX12ModRuntime.cpp

    # 3Dmigoto config/runtime model used only by DX12
    migoto_core/IniDocument.cpp
    migoto_core/MigotoCommandList.cpp
    migoto_core/MigotoIniLoader.cpp
    migoto_core/MigotoConfig.cpp
    migoto_core/MigotoResource.cpp
    migoto_core/MigotoShaderOverride.cpp
    migoto_core/MigotoShaderRegex.cpp
    migoto_core/MigotoTextureOverride.cpp
    ${CMAKE_SOURCE_DIR}/src/common/ini_parser_lite.cpp

)

target_include_directories(DirectX12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/binding_tracker
    ${CMAKE_CURRENT_SOURCE_DIR}/command_list
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/frame_analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/hooks
    ${CMAKE_CURRENT_SOURCE_DIR}/ia
    ${CMAKE_CURRENT_SOURCE_DIR}/migoto_core
    ${CMAKE_CURRENT_SOURCE_DIR}/mod_runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_dump
    ${CMAKE_CURRENT_SOURCE_DIR}/resource_tracker
    ${CMAKE_CURRENT_SOURCE_DIR}/shader
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
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

