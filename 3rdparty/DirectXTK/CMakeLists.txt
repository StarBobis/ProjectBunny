# ============================================================
# DirectXTK — DirectX Tool Kit (静态库)
# ============================================================
add_library(DirectXTK STATIC
    Audio/AudioEngine.cpp
    Audio/DynamicSoundEffectInstance.cpp
    Audio/SoundCommon.cpp
    Audio/SoundEffect.cpp
    Audio/SoundEffectInstance.cpp
    Audio/SoundStreamInstance.cpp
    Audio/WaveBank.cpp
    Audio/WaveBankReader.cpp
    Audio/WAVFileReader.cpp
    Src/AlphaTestEffect.cpp
    Src/BasicEffect.cpp
    Src/BasicPostProcess.cpp
    Src/BufferHelpers.cpp
    Src/BinaryReader.cpp
    Src/CommonStates.cpp
    Src/DDSTextureLoader.cpp
    Src/DebugEffect.cpp
    Src/DGSLEffect.cpp
    Src/DGSLEffectFactory.cpp
    Src/DirectXHelpers.cpp
    Src/DualPostProcess.cpp
    Src/DualTextureEffect.cpp
    Src/EffectCommon.cpp
    Src/EffectFactory.cpp
    Src/EnvironmentMapEffect.cpp
    Src/GamePad.cpp
    Src/GeometricPrimitive.cpp
    Src/Geometry.cpp
    Src/GraphicsMemory.cpp
    Src/Keyboard.cpp
    Src/Model.cpp
    Src/ModelLoadCMO.cpp
    Src/ModelLoadSDKMESH.cpp
    Src/ModelLoadVBO.cpp
    Src/Mouse.cpp
    Src/NormalMapEffect.cpp
    Src/PBREffectFactory.cpp
    Src/PBREffect.cpp
    Src/ScreenGrab.cpp
    Src/ScreenGrabRows.h
    Src/SimpleMath.cpp
    Src/SkinnedEffect.cpp
    Src/SpriteBatch.cpp
    Src/PrimitiveBatch.cpp
    Src/SpriteFont.cpp
    Src/ToneMapPostProcess.cpp
    Src/VertexTypes.cpp
    Src/WICTextureLoader.cpp
)

target_include_directories(DirectXTK PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
)

target_include_directories(DirectXTK PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# Compile the shaders that match this DirectXTK revision using the Windows SDK.
file(GLOB shader_inputs CONFIGURE_DEPENDS Src/Shaders/*.fx Src/Shaders/*.fxh Src/Shaders/*.hlsl Src/Shaders/*.hlsli)
set(shader_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
set(shader_stamp "${shader_directory}/compiled.stamp")
add_custom_command(OUTPUT "${shader_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E env "CompileShadersOutput=${shader_directory}" cmd /c CompileShaders.cmd
    COMMAND "${CMAKE_COMMAND}" -E touch "${shader_stamp}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Src/Shaders"
    DEPENDS Src/Shaders/CompileShaders.cmd ${shader_inputs}
    VERBATIM)
target_sources(DirectXTK PRIVATE "${shader_stamp}")
target_include_directories(DirectXTK PRIVATE "${shader_directory}" Audio)
target_compile_definitions(DirectXTK PRIVATE _WIN32_WINNT=0x0603 USING_XINPUT)
target_precompile_headers(DirectXTK PRIVATE Src/pch.h)

set_target_msvc_options(DirectXTK)
set_target_common_definitions(DirectXTK)
