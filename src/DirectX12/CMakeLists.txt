# ============================================================
# DirectX12 - d3d12.dll proxy and minimal DXGI/Present hook
# ============================================================
add_library(DirectX12 SHARED
    # Layered module headers (later layers include earlier ones)
    DX12Core.h
    DX12Hooks.h
    DX12Tracking.h
    DX12Runtime.h
    DX12Resources.h
    DX12Presentation.h

    # Proxy and presentation integration
    D3D12Proxy.cpp
    DX12Wrapper.cpp

    # Tracking and command execution modules
    DX12ResourceTracking.cpp
    DX12BindingTracking.cpp
    DX12CommandLists.cpp

    # Shader dump & hunt
    DX12ShaderTools.cpp

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
    dbghelp.lib
)

target_compile_features(DirectX12 PRIVATE cxx_std_17)

target_sources(DirectX12 PRIVATE
    d3d12Wrapper.def
)

set_target_properties(DirectX12 PROPERTIES
    OUTPUT_NAME "d3d12"
)

# Optional: build a Release binary with the detailed hot-path timing
# instrumentation compiled in (per-stage DX12HotPathTiming logs, ApiWall
# buckets, queue slices). Usage: cmake -DDX12_RELEASE_TIMING=ON ...
option(DX12_RELEASE_TIMING "Enable DX12 detailed timing instrumentation in non-Debug builds" OFF)

target_compile_definitions(DirectX12 PRIVATE
    UNICODE
    _UNICODE
    _USRDLL
    MIGOTO_DX=12
    $<$<CONFIG:Debug>:DX12_ENABLE_DETAILED_TIMING>
)

if(DX12_RELEASE_TIMING)
    target_compile_definitions(DirectX12 PRIVATE
        DX12_ENABLE_DETAILED_TIMING
        DX12_RELEASE_TIMING
    )
endif()

set_target_msvc_options(DirectX12)
set_target_common_definitions(DirectX12)

if(MSVC)
    # DX12ModRuntime.cpp aggregates the DX12 implementation units; Debug
    # symbols exceed the default COFF section limit without this MSVC switch.
    target_compile_options(DirectX12 PRIVATE /bigobj)

    # Package global data and discard unreferenced COMDATs in optimized builds.
    # These complement /Gy + /OPT:REF already supplied by CompilerOptions.cmake.
    target_compile_options(DirectX12 PRIVATE
        $<$<CONFIG:Release>:/Gw>
        $<$<CONFIG:Release>:/Zc:inline>
    )
endif()
