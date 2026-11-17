# Test-build-only adaptation: Ninja writes UTF-8 response files, while the
# installed MSVC linker interprets BOM-less input in the system ANSI code page.
# Normalize only this generated link response; keep production sources, flags
# and library inputs unchanged.
function(bunny_prepare_business_link)
    if(BUNNY_PROXY_ASAN AND TARGET D3DCompiler_46 AND MSVC)
        get_target_property(proxy_sources D3DCompiler_46 SOURCES)
        get_target_property(proxy_includes D3DCompiler_46 INCLUDE_DIRECTORIES)
        get_target_property(proxy_definitions D3DCompiler_46 COMPILE_DEFINITIONS)
        get_target_property(proxy_options D3DCompiler_46 COMPILE_OPTIONS)
        get_target_property(proxy_links D3DCompiler_46 LINK_LIBRARIES)
        get_target_property(proxy_link_options D3DCompiler_46 LINK_OPTIONS)
        add_library(bunny_compiler_asan SHARED ${proxy_sources})
        target_include_directories(bunny_compiler_asan PRIVATE ${proxy_includes})
        target_compile_definitions(bunny_compiler_asan PRIVATE ${proxy_definitions} _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
        target_compile_options(bunny_compiler_asan PRIVATE ${proxy_options} /GL- /fsanitize=address /Zi)
        target_compile_features(bunny_compiler_asan PRIVATE cxx_std_17)
        target_link_libraries(bunny_compiler_asan PRIVATE ${proxy_links})
        target_link_options(bunny_compiler_asan PRIVATE ${proxy_link_options})
        set_target_properties(bunny_compiler_asan PROPERTIES OUTPUT_NAME compiler_audit_asan
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/audit")
    endif()
    if(TARGET dx11_upstream_regression AND CMAKE_GENERATOR STREQUAL "Ninja" AND MSVC)
        set_property(TARGET dx11_upstream_regression PROPERTY CXX_LINKER_LAUNCHER
            "${Python3_EXECUTABLE};${CMAKE_SOURCE_DIR}/模拟环境/scripts/encode-link-response.py;${CMAKE_BINARY_DIR};${CMAKE_BINARY_DIR}/CMakeFiles/dx11_upstream_regression.rsp")
    endif()
endfunction()
cmake_language(DEFER CALL bunny_prepare_business_link)
