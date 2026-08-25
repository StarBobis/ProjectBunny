# ============================================================
# 3Dmigoto-Injector-V2 - Run.exe loader/injector
# Imported from SSMT4-Alpha (cpp/3Dmigoto-Injector-V2). Reads
# d3dx.ini next to the exe, launches the target and injects the
# 3Dmigoto module (d3d12.dll) via a global CBT hook / NT injection.
# Requires administrator rights (embedded UAC manifest below).
# ============================================================
add_executable(3Dmigoto-Injector-V2
    Main.cpp
    3Dmigoto-Injector-V2.rc
    D3dxIniUtils.hpp
    HybridNtInjectorUtils.hpp
    InjectorUtils.hpp
    resource.h
)

set_target_properties(3Dmigoto-Injector-V2 PROPERTIES
    OUTPUT_NAME "Run"
)

target_compile_features(3Dmigoto-Injector-V2 PRIVATE cxx_std_20)

target_compile_definitions(3Dmigoto-Injector-V2 PRIVATE
    _CONSOLE
)

set_target_msvc_options(3Dmigoto-Injector-V2)
set_target_common_definitions(3Dmigoto-Injector-V2)

if(MSVC)
    target_compile_options(3Dmigoto-Injector-V2 PRIVATE
        /permissive-
    )

    target_link_options(3Dmigoto-Injector-V2 PRIVATE
        /MANIFEST:EMBED
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    )
endif()
