cmake_minimum_required(VERSION 3.20)
project(3DMigotoDX12
    VERSION 1.3.16
    DESCRIPTION "3Dmigoto - DirectX API Hooking and Shader Manipulation Tool"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

foreach(CONF Debug Release)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONF}
        ${PROJECT_SOURCE_DIR}/x64/${CONF})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONF}
        ${PROJECT_SOURCE_DIR}/x64/${CONF})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONF}
        ${PROJECT_SOURCE_DIR}/x64/${CONF})
endforeach()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CompilerOptions)

find_package(Nektra)

add_subdirectory(3rdparty/crc32c-hw-1.0.5)
add_subdirectory(3rdparty/DirectXTK)

add_subdirectory(src/Core)
add_subdirectory(src/BinaryDecompiler)
add_subdirectory(src/D3D_Shaders)
add_subdirectory(src/D3DCompiler_46)
add_subdirectory(src/DirectXGI)
add_subdirectory(src/DirectX11)
add_subdirectory(src/DirectX12)
