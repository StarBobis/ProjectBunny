# Microsoft DirectXTex may2026 (4feb3e1), DDS capture and layout support.
add_library(DirectXTex STATIC
    BC.cpp
    BC4BC5.cpp
    BC6HBC7.cpp
    DirectXTexCompress.cpp
    DirectXTexMipmaps.cpp
    DirectXTexResize.cpp
    DirectXTexWIC.cpp
    DirectXTexDDS.cpp
    DirectXTexImage.cpp
    DirectXTexUtil.cpp
    DirectXTexD3D11.cpp
    DirectXTexConvert.cpp
)
target_include_directories(DirectXTex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(DirectXTex PRIVATE cxx_std_17)
set_target_msvc_options(DirectXTex)
