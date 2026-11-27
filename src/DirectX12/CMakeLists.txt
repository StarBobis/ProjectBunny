# ============================================================
# DirectX12 - d3d12.dll proxy and minimal DXGI/Present hook
# ============================================================
add_library(DirectX12 SHARED
    # Layered module headers (later layers include earlier ones)
    DX12Core.h
    DX12Hooks.h
    Hooks/DX12DispatchHookFlow.h
    Hooks/DX12IaReplacement.h
    Hooks/DX12DrawHookFlow.h
    Hooks/DX12HookRegistry.h
    Proxy/D3D12ProcessGate.h
    Proxy/D3D12ProxyRuntime.h
    DX12Tracking.h
    Tracking/DX12BindingCollect.h
    Tracking/DX12BindingTrackerQuery.h
    Tracking/DX12CaptureLedger.h
    Tracking/DX12CommandListHookState.h
    Tracking/DX12CommandQueueExecution.h
    Tracking/DX12DescriptorArena.h
    Tracking/DX12FrameAnalysisTracking.h
    Tracking/DX12DescriptorHeapShard.h
    Tracking/DX12DescriptorRecords.h
    Tracking/DX12DescriptorSlotArray.h
    Tracking/DX12ResourcePublishedState.h
    Tracking/DX12BufferResolveCache.h
    DX12Runtime.h
    Runtime/DX12DescriptorHeapShadowUtils.h
    Runtime/DX12DeviceHooks.h
    Runtime/DX12ResourceTrackerHooks.h
    Runtime/DX12ExecuteIndirectRuntime.h
    Runtime/DX12FrameAnalysisManifest.h
    Runtime/DX12VmFrameVariablesUtils.h
    Runtime/DX12Overlay.h
    Runtime/DX12Input.h
    DX12Resources.h
    Resources/DX12ResourceLifetime.h
    Resources/DX12PreSkinBindingCache.h
    Resources/DX12PsoDecisionCache.h
    Resources/DX12ResourceHookRuntime.h
    Resources/DX12RuntimeControl.h
    DX12Presentation.h
    Presentation/DX12WrapperNamespace.h
    Presentation/DXGIHookRuntime.h
    Presentation/DX12WrapperRuntimeUtils.h
    Presentation/DXGIHookState.h
    Presentation/DX12WrapperCore.h
    Presentation/DX12DebugWrappers.h
    Presentation/DX12SwapChainQueues.h
    Presentation/DX12FactoryWrappers.h
    Presentation/DX12CommandWrappers.h
    Presentation/DX12ResourceWrappers.h
    Presentation/DX12PipelineWrappers.h
    Presentation/DX12CommandListWrapper.h
    Presentation/DX12DeviceWrapper.h
    Core/DX12Platform.h
    Core/DX12CoreModels.h
    Core/Models/DX12PsoModels.h
    Core/Models/DX12BindingModels.h
    Core/Models/DX12BindingTrackerModels.h
    Core/DX12Diagnostics.h
    Core/Diag/DX12Diag.h
    Core/Diag/DX12ProfilingHud.h
    Core/Diag/DX12ProfilingText.h
    Core/DX12RuntimeState.h
    Core/DX12RuntimeState.cpp
    Core/DX12JsonUtils.h
    Core/DX12CommandState.h
    Core/CommandState/DX12IaDecisionState.h
    Core/CommandState/DX12IaHashCaches.h
    Core/CommandState/DX12SimulateContext.h
    Core/CommandState/DX12CommandListRuntime.h
    Core/CommandState/DX12FrameAnalysisState.h
    Core/DX12HotPath.h
    Core/HotPath/DX12HotPathState.h
    Core/DX12CommandFlow.h
    Core/CommandFlow/DX12CommandListBindingFlow.h
    Core/CommandFlow/DX12HuntListModels.h
    Core/CommandFlow/DX12CommandListStateFlow.h
    Core/DX12ModState.h
    Core/ModState/DX12ModIaReplacement.h
    Core/ModState/DX12PreSkinDispatchOverride.h
    Core/ModState/DX12PreSkinOutputResizeBinding.h
    Core/ModState/DX12PreSkinPlanUtils.h
    Config/DX12Config.h
    Config/DX12ConfigLoader.h
    Config/DX12ConstantsVmUtils.h
    Resources/DX12ResourceDump.h
    Resources/Dump/DX12PsoInputLayoutUtils.h
    Resources/Dump/DX12ResourceCollectQueue.h
    Resources/Dump/DX12ResourceDumpCore.h
    Resources/Dump/DX12ResourceDumpBufferText.h
    Resources/Dump/DX12ResourceDumpDedupedNaming.h
    Resources/Dump/DX12ResourceDumpDds.h
    Resources/Dump/DX12ResourceDumpPolicy.h
    Resources/Dump/DX12ResourceDumpNaming.h
    Resources/Dump/DX12TextureHashUtils.h
    Resources/Dump/DX12ResourceDumpFileWriter.h
    Resources/Dump/DX12ResourceDumpPrepare.h
    Resources/Dump/DX12ResourceDumpGpuCopy.h
    Resources/Dump/DX12ResourceDumpIndexWriter.h
    Resources/Dump/DX12ResourceDump.h
    ModRuntime/DX12ModRuntimeInternal.h
    ModRuntime/DX12DrawTextureSrvViewUtils.h
    ModRuntime/DX12DdsTextureLoadUtils.h
    ModRuntime/DX12DrawSrvReplacementUtils.h
    ModRuntime/DX12BundleSrvMirror.h
    ModRuntime/DX12CommandListExecutor.h
    ModRuntime/DX12ShaderOverrideIaTargetPlanUtils.h

    # Proxy and presentation integration
    Proxy/D3D12Proxy.cpp
    Presentation/DX12WrapperApi.cpp
    Presentation/DXGIHooks.cpp
    Presentation/DX12WaitHooks.cpp
    Presentation/DX12DbgHelpHooks.cpp

    # Tracking and command execution modules
    Tracking/DX12CaptureLedger.cpp
    Tracking/DX12BindingCollectQueue.cpp
    Tracking/DX12BindingTracker.cpp
    Tracking/DX12BindingExport.cpp
    Tracking/DX12ResourceCollectQueue.cpp
    Tracking/DX12ResourceTrackingCore.cpp
    Tracking/DX12ResourceMapCapture.cpp
    Tracking/DX12ResourceTrackerHooks.cpp
    Tracking/DX12ResourceContentHashing.cpp
    Tracking/DX12DescriptorQuery.cpp
    Tracking/DX12AsyncReadback.cpp
    Core/CommandState/DX12CommandListRuntime.cpp
    Core/CommandFlow/DX12CommandListStateCapture.cpp
    Tracking/DX12CommandListHookState.cpp

    # Shader dump & hunt
    ShaderTools/DX12ShaderDump.cpp
    ShaderTools/DX12ShaderModelParse.h
    ShaderTools/DX12ShaderHunt.cpp
    ShaderTools/DX12NativeOverlay.cpp

    # Mod runtime
    ModRuntime/DX12ModRuntimeLoad.cpp
    ModRuntime/DX12PreSkinRuntime.cpp
    ModRuntime/DX12LoadedResources.cpp
    ModRuntime/DX12DrawSrvReplacement.cpp
    ModRuntime/DX12BundleSrvMirror.cpp
    ModRuntime/DX12TextureOverrideMatching.cpp
    ModRuntime/DX12CommandListExecutor.cpp
    ModRuntime/DX12ShaderOverrideRuntime.cpp

    # 3Dmigoto config/runtime model used only by DX12
    ${CMAKE_SOURCE_DIR}/src/common/ini_parser_lite.cpp
    ${CMAKE_SOURCE_DIR}/src/common/ExpressionFunctions.cpp
    ${CMAKE_SOURCE_DIR}/src/common/FrameRate.cpp

)

target_include_directories(DirectX12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    # Config and ModRuntime headers include each other by bare name
    # (DX12ModRuntimeInternal.h -> DX12ConstantsVmUtils.h -> DX12Config.h), so
    # both directories are on the search path rather than spelled out per
    # include.
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/ModRuntime
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
    windowscodecs.lib
    ole32.lib
    XINPUT9_1_0.lib
    User32.lib
    Gdi32.lib
    comctl32.lib
    Dwmapi.lib
    Shlwapi.lib
    dbghelp.lib
)

target_compile_features(DirectX12 PRIVATE cxx_std_20)

target_sources(DirectX12 PRIVATE
    d3d12Wrapper.def
)

set_target_properties(DirectX12 PROPERTIES
    OUTPUT_NAME "d3d12"
)

# Optional: build a Release binary with the unified diagnostics compiled in
# (Core/Diag/DX12Diag.h: async logger + per-thread bucket profiler, frame
# summaries on a writer thread). Usage: cmake -DDX12_RELEASE_TIMING=ON ...
option(DX12_RELEASE_TIMING "Enable DX12 timing diagnostics in non-Debug builds" OFF)

target_compile_definitions(DirectX12 PRIVATE
    UNICODE
    _UNICODE
    _USRDLL
    MIGOTO_DX=12
)

if(DX12_RELEASE_TIMING)
    target_compile_definitions(DirectX12 PRIVATE
        DX12_RELEASE_TIMING
    )
    # Crash self-diagnosis (report + breadcrumbs + minidump in d3d12_log.txt).
    # TIMING-only by construction: not compiled into a normal Release binary,
    # whose breadcrumb macro is an empty inline.
    target_sources(DirectX12 PRIVATE
        Core/Diag/DX12CrashReport.cpp
    )
endif()

set_target_msvc_options(DirectX12)
set_target_common_definitions(DirectX12)

if(MSVC)
    # The ModRuntime implementation units are heavy; Debug symbols exceed the
    # default COFF section limit without this MSVC switch.
    target_compile_options(DirectX12 PRIVATE /bigobj)

    # Package global data and discard unreferenced COMDATs in optimized builds.
    # These complement /Gy + /OPT:REF already supplied by CompilerOptions.cmake.
    target_compile_options(DirectX12 PRIVATE
        $<$<CONFIG:Release>:/Gw>
        $<$<CONFIG:Release>:/Zc:inline>
    )
endif()

# F-201b: the shader-model container parser is pure C++ (no D3D12 device), so
# its audit binary builds and runs without the proxy.  Opt-in until DX12 grows
# a full audit harness; see the usage line in tests/shader_model_bounds_audit.cpp.
option(DX12_AUDIT_TESTS "Build the DX12 audit test executables" OFF)

if(BUILD_TESTING AND DX12_AUDIT_TESTS)
    add_executable(dx12_shader_model_audit tests/shader_model_bounds_audit.cpp)
    target_include_directories(dx12_shader_model_audit PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ShaderTools
    )
    target_compile_features(dx12_shader_model_audit PRIVATE cxx_std_20)
    set_target_msvc_options(dx12_shader_model_audit)
    set_target_common_definitions(dx12_shader_model_audit)
    add_test(NAME dx12_shader_model_audit
        COMMAND dx12_shader_model_audit --audit-item F-201b)
    set_tests_properties(dx12_shader_model_audit PROPERTIES TIMEOUT 60)

    # Exercise the same wrapper, queues, worker and caches against WARP. Keep
    # the executable away from the proxy DLL so it loads the system runtime.
    get_target_property(dx12_sources DirectX12 SOURCES)
    list(FILTER dx12_sources EXCLUDE REGEX "(Proxy/D3D12Proxy.cpp|d3d12Wrapper.def)$")
    add_executable(dx12_runtime_audit tests/runtime_audit.cpp ${dx12_sources})
    target_include_directories(dx12_runtime_audit PRIVATE
        $<TARGET_PROPERTY:DirectX12,INCLUDE_DIRECTORIES>)
    target_link_libraries(dx12_runtime_audit PRIVATE
        $<TARGET_PROPERTY:DirectX12,LINK_LIBRARIES> d3d12.lib)
    target_compile_definitions(dx12_runtime_audit PRIVATE MIGOTO_DX=12)
    target_compile_features(dx12_runtime_audit PRIVATE cxx_std_20)
    set_target_msvc_options(dx12_runtime_audit)
    set_target_common_definitions(dx12_runtime_audit)
    target_compile_options(dx12_runtime_audit PRIVATE /bigobj)
    set_target_properties(dx12_runtime_audit PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/audit"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/audit")
    add_test(NAME dx12_runtime_audit COMMAND dx12_runtime_audit)
    set_tests_properties(dx12_runtime_audit PROPERTIES TIMEOUT 60)
endif()
