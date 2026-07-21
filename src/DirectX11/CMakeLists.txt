# ============================================================
# DirectX11 — 主模块 d3d11.dll (DirectX 11 API Hook)
# ============================================================
add_library(DirectX11 SHARED
    # DirectX11 自身源文件
    CommandList.cpp
    cursor.cpp
    D3D11Wrapper.cpp
    DLLMainHook.cpp
    FrameAnalysis.cpp
    HackerContext.cpp
    HackerDevice.cpp
    HackerDXGI.cpp
    HookAddresses.c
    HookedContext.cpp
    HookedDevice.cpp
    HookedDXGI.cpp
    Hunting.cpp
    IniHandler.cpp
    input.cpp
    lock.cpp
    Overlay.cpp
    Override.cpp
    profiling.cpp
    ResourceHash.cpp
    ShaderRegex.cpp

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

target_include_directories(DirectX11 PRIVATE
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
    _USRDLL
    $<$<CONFIG:Debug>:_DEBUG_LAYER=1>
)

set_target_msvc_options(DirectX11)
set_target_common_definitions(DirectX11)
