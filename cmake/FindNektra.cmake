# ============================================================
# FindNektra.cmake — 查找 Nektra Hook Library (prebuilt)
# ============================================================
#
# 导出目标:
#   Nektra::NktHookLib — Nektra Hook Library
#
# 该模块在 3rdparty/Nektra/ 中查找预编译的 .lib 文件，
# 并根据架构 (x86/x64) 和配置 (Debug/Release) 选择正确的 lib。

set(NEKTRA_DIR "${CMAKE_SOURCE_DIR}/3rdparty/Nektra" CACHE PATH "Path to Nektra Hook Library")

if(NOT NEKTRA_DIR)
    return()
endif()

# Nektra lib 文件命名规则:
#   x86:  NktHookLib.lib (Release), NktHookLib_Debug.lib (Debug)
#   x64:  NktHookLib64.lib (Release), NktHookLib64_Debug.lib (Debug)

set(NEKTRA_LIB_RELEASE_X86 "${NEKTRA_DIR}/NktHookLib.lib")
set(NEKTRA_LIB_DEBUG_X86   "${NEKTRA_DIR}/NktHookLib_Debug.lib")
set(NEKTRA_LIB_RELEASE_X64 "${NEKTRA_DIR}/NktHookLib64.lib")
set(NEKTRA_LIB_DEBUG_X64   "${NEKTRA_DIR}/NktHookLib64_Debug.lib")

set(NEKTRA_HEADER "${NEKTRA_DIR}/NktHookLib.h")

if(EXISTS "${NEKTRA_HEADER}")
    add_library(Nektra::NktHookLib INTERFACE IMPORTED)

    # 根据架构和配置选择正确的 lib
    target_link_libraries(Nektra::NktHookLib INTERFACE
        $<$<AND:$<CONFIG:Debug>,$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>>:${NEKTRA_LIB_DEBUG_X86}>
        $<$<AND:$<NOT:$<CONFIG:Debug>>,$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>>:${NEKTRA_LIB_RELEASE_X86}>
        $<$<AND:$<CONFIG:Debug>,$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>>:${NEKTRA_LIB_DEBUG_X64}>
        $<$<AND:$<NOT:$<CONFIG:Debug>>,$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>>:${NEKTRA_LIB_RELEASE_X64}>
    )

    target_include_directories(Nektra::NktHookLib INTERFACE "${NEKTRA_DIR}")

    message(STATUS "Found Nektra Hook Library: ${NEKTRA_DIR}")
else()
    message(WARNING "Nektra Hook Library NOT found at ${NEKTRA_DIR}")
endif()
