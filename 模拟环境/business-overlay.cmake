# Test-build-only adaptation: Ninja writes UTF-8 response files, while the
# installed MSVC linker interprets BOM-less input in the system ANSI code page.
# Normalize only this generated link response; keep production sources, flags
# and library inputs unchanged.
function(bunny_prepare_business_link)
    if(TARGET dx11_upstream_regression AND CMAKE_GENERATOR STREQUAL "Ninja" AND MSVC)
        set_property(TARGET dx11_upstream_regression PROPERTY CXX_LINKER_LAUNCHER
            "${Python3_EXECUTABLE};${CMAKE_SOURCE_DIR}/模拟环境/scripts/encode-link-response.py;${CMAKE_BINARY_DIR};${CMAKE_BINARY_DIR}/CMakeFiles/dx11_upstream_regression.rsp")
    endif()
endfunction()
cmake_language(DEFER CALL bunny_prepare_business_link)
