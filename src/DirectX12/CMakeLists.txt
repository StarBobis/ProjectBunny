# ============================================================
# DirectX12 - d3d12.dll proxy and minimal DXGI/Present hook
# ============================================================
add_library(DirectX12 SHARED
    # Proxy entry point
    proxy/D3D12Proxy.cpp
    proxy/DX12Wrapper.cpp

    # Resource tracking
    resource/DX12ResourceCollectQueue.cpp
    resource/DX12ResourceTracker.cpp
    resource/DX12ResourceTrackerHooks.cpp
    resource/DX12ResourceTrackerQueries.cpp

    # Binding/descriptor tracking
    binding/DX12CaptureLedger.cpp
    binding/DX12BindingCollectQueue.cpp
    binding/DX12BindingTracker.cpp
    binding/DX12BindingTrackerFrameExport.cpp
    binding/DX12BindingTrackerResourceExport.cpp

    # Command list hooks & runtime
    command/DX12CommandListHooks.cpp
    command/DX12CommandListRuntime.cpp
    command/DX12CommandListLifecycle.cpp
    command/DX12CommandListCommandRecorder.cpp
    command/DX12CommandListStateCapture.cpp

    # DXGI/Present hooks
    dxgi/DXGIHooks.cpp

    # UI
    overlay/DX12NativeOverlay.cpp

    # Shader dump & hunt
    dump/DX12ShaderDump.cpp
    shader/DX12ShaderHunt.cpp

    # Mod runtime
    mod/DX12ModRuntime.cpp

    # 3Dmigoto config/runtime model used only by DX12
    ${CMAKE_SOURCE_DIR}/src/common/ini_parser_lite.cpp

)

target_include_directories(DirectX12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/binding
    ${CMAKE_CURRENT_SOURCE_DIR}/command
    ${CMAKE_CURRENT_SOURCE_DIR}/descriptor
    ${CMAKE_CURRENT_SOURCE_DIR}/dump
    ${CMAKE_CURRENT_SOURCE_DIR}/dxgi
    ${CMAKE_CURRENT_SOURCE_DIR}/frame_analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/helper
    ${CMAKE_CURRENT_SOURCE_DIR}/hooks
    ${CMAKE_CURRENT_SOURCE_DIR}/ia
    ${CMAKE_CURRENT_SOURCE_DIR}/mod
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay
    ${CMAKE_CURRENT_SOURCE_DIR}/proxy
    ${CMAKE_CURRENT_SOURCE_DIR}/queue_cpu
    ${CMAKE_CURRENT_SOURCE_DIR}/resource
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/shader
    ${CMAKE_CURRENT_SOURCE_DIR}/types
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
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

target_compile_features(DirectX12 PRIVATE cxx_std_17)

target_sources(DirectX12 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/proxy/d3d12Wrapper.def
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

if(BUILD_TESTING)
	add_executable(DX12SparseDescriptorStoreTests
		tests/DX12SparseDescriptorStoreTests.cpp
	)
	target_include_directories(DX12SparseDescriptorStoreTests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/descriptor
	)
	target_compile_features(DX12SparseDescriptorStoreTests PRIVATE cxx_std_17)
	add_test(NAME DX12SparseDescriptorStoreTests COMMAND DX12SparseDescriptorStoreTests)

	add_executable(DX12DescriptorHeapShardStateTests
		tests/DX12DescriptorHeapShardStateTests.cpp
	)
	target_include_directories(DX12DescriptorHeapShardStateTests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/descriptor
	)
	target_compile_features(DX12DescriptorHeapShardStateTests PRIVATE cxx_std_17)
	add_test(NAME DX12DescriptorHeapShardStateTests COMMAND DX12DescriptorHeapShardStateTests)

	add_executable(DX12DescriptorStateTests
		tests/DX12DescriptorStateTests.cpp
	)
	target_include_directories(DX12DescriptorStateTests PRIVATE
		$<TARGET_PROPERTY:DirectX12,INCLUDE_DIRECTORIES>
	)
	target_compile_definitions(DX12DescriptorStateTests PRIVATE
		UNICODE
		_UNICODE
		MIGOTO_DX=12
	)
	target_compile_features(DX12DescriptorStateTests PRIVATE cxx_std_17)
	add_test(NAME DX12DescriptorStateTests COMMAND DX12DescriptorStateTests)

    add_executable(DX12PreSkinCacheIdentityTests
        tests/DX12PreSkinCacheIdentityTests.cpp
    )
    target_include_directories(DX12PreSkinCacheIdentityTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/command
        ${CMAKE_CURRENT_SOURCE_DIR}/mod
    )
    target_compile_features(DX12PreSkinCacheIdentityTests PRIVATE cxx_std_17)
    add_test(NAME DX12PreSkinCacheIdentityTests COMMAND DX12PreSkinCacheIdentityTests)

    add_executable(DX12DirectIaFastRejectCacheTests
        tests/DX12DirectIaFastRejectCacheTests.cpp
    )
    target_include_directories(DX12DirectIaFastRejectCacheTests PRIVATE
        $<TARGET_PROPERTY:DirectX12,INCLUDE_DIRECTORIES>
    )
    target_compile_features(DX12DirectIaFastRejectCacheTests PRIVATE cxx_std_17)
    add_test(NAME DX12DirectIaFastRejectCacheTests COMMAND DX12DirectIaFastRejectCacheTests)
endif()


