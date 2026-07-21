# ============================================================
# CompilerOptions.cmake — 统一编译器选项（MSVC）
# ============================================================

# 运行时库 — 对应 /MT (Release) 和 /MTd (Debug)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 全局 MSVC 编译选项
function(set_target_msvc_options TARGET)
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE
            /W3                          # Warning level 3
            /EHsc                        # Async exception handling (/EHa)
            /utf-8                       # UTF-8 source charset
        )

        # Debug builds: disable optimization, enable debug info
        target_compile_options(${TARGET} PRIVATE
            $<$<CONFIG:Debug>:/Od>
            $<$<CONFIG:Debug>:/RTC1>
        )

        # Release builds: optimize for speed
        target_compile_options(${TARGET} PRIVATE
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/GL>     # Whole Program Optimization
            $<$<NOT:$<CONFIG:Debug>>:/Gy>  # Function Level Linking
        )

        # Buffer security check: ON for Debug, OFF for Release
        target_compile_options(${TARGET} PRIVATE
            $<$<CONFIG:Debug>:/GS>
            $<$<NOT:$<CONFIG:Debug>>:/GS->
        )

        # Multi-processor compilation
        target_compile_options(${TARGET} PRIVATE /MP)

        # Linker options
        target_link_options(${TARGET} PRIVATE
            $<$<CONFIG:Debug>:/DEBUG>
            $<$<NOT:$<CONFIG:Debug>>:/DEBUG /OPT:REF /OPT:ICF>
            $<$<CONFIG:Release>:/LTCG>   # Link Time Code Generation
            $<$<CONFIG:Release>:/INCREMENTAL:NO>
        )

        # 如果使用 Ninja，添加必要的链接器标志
        if(CMAKE_GENERATOR MATCHES "Ninja")
            target_link_options(${TARGET} PRIVATE /NOLOGO)
        endif()
    endif()
endfunction()

# 设置预处理器定义
function(set_target_common_definitions TARGET)
    target_compile_definitions(${TARGET} PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        UNICODE
        _UNICODE
        _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1
        _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT=1
        _WINDOWS
    )
endfunction()
