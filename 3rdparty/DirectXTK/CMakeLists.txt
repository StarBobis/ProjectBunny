# ============================================================
# DirectXTK — DirectX Tool Kit (静态库)
# ============================================================
add_library(DirectXTK STATIC
    Src/AlphaTestEffect.cpp
    Src/BasicEffect.cpp
    Src/BasicPostProcess.cpp
    Src/BinaryReader.cpp
    Src/CommonStates.cpp
    Src/DDSTextureLoader.cpp
    Src/DebugEffect.cpp
    Src/DGSLEffect.cpp
    Src/DGSLEffectFactory.cpp
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
    Src/PBREffect.cpp
    Src/ScreenGrab.cpp
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

set_target_msvc_options(DirectXTK)
set_target_common_definitions(DirectXTK)
