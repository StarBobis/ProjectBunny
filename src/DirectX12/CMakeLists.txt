# ============================================================
# DirectX12 - d3d12.dll proxy and minimal DXGI/Present hook
# ============================================================
add_library(DirectX12 SHARED
    # Proxy and presentation integration
    D3D12Proxy.cpp
    DX12Wrapper.cpp
    DXGIHooks.cpp

    # Tracking and command execution modules
    DX12ResourceTracking.cpp
    DX12BindingTracking.cpp
    DX12CommandLists.cpp

    # UI
    DX12NativeOverlay.cpp

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
)

target_compile_features(DirectX12 PRIVATE cxx_std_17)

target_sources(DirectX12 PRIVATE
    d3d12Wrapper.def
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
		DX12SparseDescriptorStoreTests.cpp
	)
	target_include_directories(DX12SparseDescriptorStoreTests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
	)
	target_compile_features(DX12SparseDescriptorStoreTests PRIVATE cxx_std_17)
	add_test(NAME DX12SparseDescriptorStoreTests COMMAND DX12SparseDescriptorStoreTests)

	add_executable(DX12DescriptorHeapShardStateTests
		DX12DescriptorHeapShardStateTests.cpp
	)
	target_include_directories(DX12DescriptorHeapShardStateTests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
	)
	target_compile_features(DX12DescriptorHeapShardStateTests PRIVATE cxx_std_17)
	add_test(NAME DX12DescriptorHeapShardStateTests COMMAND DX12DescriptorHeapShardStateTests)

	add_executable(DX12DescriptorStateTests
		DX12DescriptorStateTests.cpp
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
        DX12PreSkinCacheIdentityTests.cpp
    )
    target_include_directories(DX12PreSkinCacheIdentityTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_features(DX12PreSkinCacheIdentityTests PRIVATE cxx_std_17)
    add_test(NAME DX12PreSkinCacheIdentityTests COMMAND DX12PreSkinCacheIdentityTests)

    add_executable(DX12DirectIaFastRejectCacheTests
        DX12DirectIaFastRejectCacheTests.cpp
    )
    target_include_directories(DX12DirectIaFastRejectCacheTests PRIVATE
        $<TARGET_PROPERTY:DirectX12,INCLUDE_DIRECTORIES>
    )
    target_compile_features(DX12DirectIaFastRejectCacheTests PRIVATE cxx_std_17)
    add_test(NAME DX12DirectIaFastRejectCacheTests COMMAND DX12DirectIaFastRejectCacheTests)

    set(DX12_TEST_TARGETS
        DX12SparseDescriptorStoreTests
        DX12DescriptorHeapShardStateTests
        DX12DescriptorStateTests
        DX12PreSkinCacheIdentityTests
        DX12DirectIaFastRejectCacheTests
    )
    foreach(DX12_TEST_TARGET IN LISTS DX12_TEST_TARGETS)
        target_include_directories(${DX12_TEST_TARGET} PRIVATE
            $<TARGET_PROPERTY:DirectX12,INCLUDE_DIRECTORIES>
        )
        if(MSVC)
            target_compile_options(${DX12_TEST_TARGET} PRIVATE /utf-8)
        endif()
    endforeach()
endif()
