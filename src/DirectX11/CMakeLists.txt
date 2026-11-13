# ============================================================
# DirectX11 — 主模块 d3d11.dll (DirectX 11 API Hook)
# ============================================================
find_package(Git QUIET)
set(DX11_VERSION_TAG "" CACHE STRING "Optional DX11 version tag override")
set(DX11_VERSION_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(DX11_VERSION_HEADER "${DX11_VERSION_DIRECTORY}/dx11-version.generated.h")
add_custom_target(dx11_version_header
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_FILE=${DX11_VERSION_HEADER}"
        "-DFALLBACK_VERSION=${PROJECT_VERSION}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DVERSION_TAG=${DX11_VERSION_TAG}"
        -P "${CMAKE_SOURCE_DIR}/cmake/GenerateDX11Version.cmake"
    BYPRODUCTS "${DX11_VERSION_HEADER}"
    VERBATIM
)
set_source_files_properties(DirectX11.rc PROPERTIES OBJECT_DEPENDS "${DX11_VERSION_HEADER}")

add_library(DirectX11 SHARED
    # DirectX11 自身源文件
    CommandList.cpp
    cursor.cpp
    D3D11Wrapper.cpp
    DX11Version.h
    DLLMainHook.cpp
    ExpressionFunctions.cpp
    FrameAnalysis.cpp
    FrameTiming.cpp
    FrameTiming.h
    PngColorSpace.cpp
    PngColorSpace.h
    HackerContext.cpp
    TextureUploadTracking.cpp
    TextureUploadTracking.h
    DHash64Gpu.cpp
    DHash64Gpu.h
    HackerDevice.cpp
    HackerDXGI.cpp
    HackerInputLayout.cpp
    HookAddresses.c
    HookedContext.cpp
    HookedDevice.cpp
    HookedDXGI.cpp
    Hunting.cpp
    IniHandler.cpp
    InitializationGate.h
    D3D11Initialization.h
    input.cpp
    lock.cpp
    Logging.cpp
    Logging.h
    Overlay.cpp
    Override.cpp
    profiling.cpp
    ResourceHash.cpp
    ShaderRegex.cpp
    ShaderBindings.cpp
    ShaderBindings.h
    d3d11TokenizedProgramFormat.hpp

    # 从外部项目编译的源文件 (直接包含编译)
    ${CMAKE_SOURCE_DIR}/src/common/iid.cpp
    ${CMAKE_SOURCE_DIR}/src/common/ini_parser_lite.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util.cpp
    ${CMAKE_SOURCE_DIR}/src/D3D_Shaders/Assembler.cpp
    ${CMAKE_SOURCE_DIR}/src/D3D_Shaders/SignatureParser.cpp
    ${CMAKE_SOURCE_DIR}/src/HLSLDecompiler/DecompileHLSL.cpp

    # 资源文件
    DirectX11.rc
)

add_dependencies(DirectX11 dx11_version_header)
target_include_directories(DirectX11 PRIVATE
    ${DX11_VERSION_DIRECTORY}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src                              # 跨项目引用: BinaryDecompiler/, HLSLDecompiler/, D3D_Shaders/
    ${CMAKE_SOURCE_DIR}/src/common                        # log.h, util.h, util_min.h, shader.h, version.h
    ${CMAKE_SOURCE_DIR}/src/HLSLDecompiler                 # DecompileHLSL.h
    ${CMAKE_SOURCE_DIR}/src/BinaryDecompiler
    ${CMAKE_SOURCE_DIR}/src/BinaryDecompiler/include
    ${CMAKE_SOURCE_DIR}/3rdparty                           # Nektra/NktHookLib.h
    ${CMAKE_SOURCE_DIR}/3rdparty/DirectXTK/Inc             # SpriteFont.h, DDSTextureLoader.h, etc.
    ${CMAKE_SOURCE_DIR}/3rdparty/crc32c-hw-1.0.5/include   # crc32c.h
    ${CMAKE_SOURCE_DIR}/3rdparty/pcre2                      # pcre2.h (预编译 lib)
)

target_link_libraries(DirectX11 PRIVATE
    BinaryDecompiler
    DirectXTK
    Nektra::NktHookLib
    crc32c
    XINPUT9_1_0.lib
    d3dcompiler.lib
    DXGI.lib
    Shlwapi.lib
    Dbghelp.lib
)

target_compile_features(DirectX11 PRIVATE cxx_std_17)

# pcre2 预编译库 — 根据架构和配置选择
set(PCRE2_DIR "${CMAKE_SOURCE_DIR}/3rdparty/pcre2")
target_link_libraries(DirectX11 PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>>:${PCRE2_DIR}/pcre2-8-32d.lib>
    $<$<AND:$<NOT:$<CONFIG:Debug>>,$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>>:${PCRE2_DIR}/pcre2-8-32.lib>
    $<$<AND:$<CONFIG:Debug>,$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>>:${PCRE2_DIR}/pcre2-8-64d.lib>
    $<$<AND:$<NOT:$<CONFIG:Debug>>,$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>>:${PCRE2_DIR}/pcre2-8-64.lib>
)

# 模块定义文件 — CMake 3.4+ 自动处理 .def 文件
target_sources(DirectX11 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/d3d11Wrapper.def
)

# ForceSymbolReferences: 强制链接特定符号
target_link_options(DirectX11 PRIVATE
    $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:/INCLUDE:_CreateDXGIFactory@8>
    $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>:/INCLUDE:CreateDXGIFactory>
)

set_target_properties(DirectX11 PROPERTIES
    OUTPUT_NAME "d3d11"
)

# 预处理器定义
target_compile_definitions(DirectX11 PRIVATE
    UNICODE
    _UNICODE
    CRC32C_STATIC=1
    PCRE2_STATIC
    PCRE2_CODE_UNIT_WIDTH=8
    MIGOTO_DX=11
    DX11_VERSION_GENERATED=1
    _USRDLL
    $<$<CONFIG:Debug>:_DEBUG_LAYER=1>
)

set_target_msvc_options(DirectX11)
set_target_common_definitions(DirectX11)

if(BUILD_TESTING)
    configure_file(tests/data/wic.png wic-regression.png COPYONLY)
    # Exercise the real parser/globals/resource lifecycle without exporting test
    # hooks from d3d11.dll or replacing production dependencies with mocks.
    get_target_property(DX11_REGRESSION_SOURCES DirectX11 SOURCES)
    list(FILTER DX11_REGRESSION_SOURCES EXCLUDE REGEX "\\.(def|rc)$")
    add_executable(dx11_upstream_regression
        ${DX11_REGRESSION_SOURCES}
        tests/upstream_regression.cpp
        tests/persistence_regression.cpp
        tests/input_layout_regression.cpp
        tests/ini_expression_regression.cpp
        tests/layout_override_regression.cpp
        tests/tokenizer_regression.cpp
        tests/time_locked_regression.cpp
        tests/unary_function_regression.cpp
        tests/readback_pool_regression.cpp
        tests/readback_hash_regression.cpp
        tests/argument_reader_regression.cpp
        tests/spatial_grid_regression.cpp
        tests/enum_option_regression.cpp
        tests/command_list_callback_regression.cpp
        tests/draw_dispatch_regression.cpp
        tests/store_readback_regression.cpp
        tests/resource_region_regression.cpp
        tests/reader_variable_regression.cpp
        tests/pool_lifecycle_regression.cpp
        tests/utf8_tokenizer_regression.cpp
        tests/pool_missing_regression.cpp
        tests/proxy_recovery_regression.cpp
        tests/persist_preservation_regression.cpp
        tests/critical_section_guard_regression.cpp
        tests/store_structured_regression.cpp
        tests/legacy_source_regression.cpp
        tests/shader_bindings_regression.cpp
        tests/frame_timing_regression.cpp
        tests/callback_namespace_regression.cpp
        tests/png_color_space_regression.cpp
        tests/texture_properties_regression.cpp
        tests/color_detection_options_regression.cpp
        tests/log_level_regression.cpp
        tests/input_toggle_hold_regression.cpp
        tests/initialization_gate_regression.cpp
        tests/parser_optimization_regression.cpp
        tests/input_scope_regression.cpp
        tests/persist_unknown_pool_regression.cpp
        tests/file_append_regression.cpp
        tests/resource_metadata_v2_regression.cpp
        tests/ini_parser_v2_regression.cpp
        tests/persist_settings_v2_regression.cpp
        tests/slot_batch_regression.cpp
        tests/pool_range_flags_regression.cpp
        tests/texture_override_cache_regression.cpp
        tests/dynamic_slot_regression.cpp
        tests/dynamic_pipeline_slot_regression.cpp
        tests/slot_range_regression.cpp
        tests/slot_range_cb_regression.cpp
        tests/slot_range_exception_regression.cpp
        tests/range_capture_example_regression.cpp
        tests/cache_resource_data_regression.cpp
        tests/store_cache_regression.cpp
        tests/check_to_range_regression.cpp
        tests/view_reference_regression.cpp
        tests/conditional_batch_regression.cpp
        tests/conditional_batch_followup_regression.cpp
        tests/region_reference_followup_regression.cpp
        tests/region_followup_regression.cpp
        tests/stream_output_regression.cpp
        tests/shader_summary_regression.cpp
        tests/shader_dump_filter_regression.cpp
        tests/stream_output_append_regression.cpp
        tests/texture_upload_state_regression.cpp
        tests/original_resource_metadata_regression.cpp
        tests/dhash_gpu_regression.cpp
        tests/member_argument_regression.cpp
        tests/dhash_cache_regression.cpp
        tests/hamming_distance_regression.cpp
        tests/adversarial_region_regression.cpp
        tests/adversarial_dhash_regression.cpp
        tests/adversarial_store_regression.cpp
        tests/round2_spatial_boundary_regression.cpp
        tests/round2_handle_info_exception_regression.cpp
        tests/round2_reload_snapshot_regression.cpp
        tests/round2_region_clamp_regression.cpp
        tests/round2_snapshot_stride_regression.cpp
        tests/dump_range_regression.cpp
        tests/slot_range_numeric_regression.cpp
        tests/profiling_generation_regression.cpp
        tests/dx11_version_regression.cpp
    )
    target_include_directories(dx11_upstream_regression PRIVATE
        $<TARGET_GENEX_EVAL:DirectX11,$<TARGET_PROPERTY:DirectX11,INCLUDE_DIRECTORIES>>)
    target_compile_definitions(dx11_upstream_regression PRIVATE
        $<TARGET_GENEX_EVAL:DirectX11,$<TARGET_PROPERTY:DirectX11,COMPILE_DEFINITIONS>>)
    target_link_libraries(dx11_upstream_regression PRIVATE
        $<TARGET_GENEX_EVAL:DirectX11,$<TARGET_PROPERTY:DirectX11,LINK_LIBRARIES>>)
    target_compile_features(dx11_upstream_regression PRIVATE cxx_std_17)
    set_target_msvc_options(dx11_upstream_regression)
    set_target_common_definitions(dx11_upstream_regression)
    add_dependencies(dx11_upstream_regression DirectX11 dx11_version_header)
    target_link_libraries(dx11_upstream_regression PRIVATE Version.lib)
    add_executable(dx11_legacy_version_regression tests/legacy_version_regression.cpp)
    target_compile_definitions(dx11_legacy_version_regression PRIVATE MIGOTO_DX=12)
    target_include_directories(dx11_legacy_version_regression PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
    target_compile_features(dx11_legacy_version_regression PRIVATE cxx_std_20)
    set_target_properties(dx11_legacy_version_regression PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    add_dependencies(dx11_upstream_regression dx11_legacy_version_regression)
    add_test(NAME dx11_legacy_version_regression COMMAND dx11_legacy_version_regression)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND AND GIT_FOUND)
        add_test(NAME dx11_version_generator_regression
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/test-dx11-version.py"
                --source-root "${CMAKE_SOURCE_DIR}"
                --work-dir "${CMAKE_SOURCE_DIR}/build/version-generator-regression")
    endif()
    add_test(NAME dx11_upstream_regression COMMAND dx11_upstream_regression)
endif()
