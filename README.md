# 🐰 Project Bunny · 重装小兔

**[📖 Online documentation](https://starbobis.github.io/ProjectBunny/)** · [📦 Download](https://github.com/StarBobis/ProjectBunny/releases/latest) · [简体中文](README.zh-CN.md)

Project Bunny extends 3Dmigoto for Windows games using **DirectX 11 or DirectX 12**. Use it to inspect what a game renders, find the resources and shaders behind a draw call, and build mods that replace them.

[![GitHub stars](https://img.shields.io/github/stars/StarBobis/ProjectBunny?style=social)](https://github.com/StarBobis/ProjectBunny/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/StarBobis/ProjectBunny?style=social)](https://github.com/StarBobis/ProjectBunny/network/members)
[![GitHub issues](https://img.shields.io/github/issues/StarBobis/ProjectBunny)](https://github.com/StarBobis/ProjectBunny/issues)
[![License](https://img.shields.io/github/license/StarBobis/ProjectBunny)](LICENSE.txt)

## ✨ What sets Project Bunny apart

- **Chinese in-game notifications on DX11（中文绿字）.** The Unicode overlay renders Chinese characters in notifications, warnings, and errors, including messages that contain Chinese paths or INI section names. Notifications use Microsoft YaHei UI on Windows. [Overlay troubleshooting](usage/dx11/troubleshooting.md#屏幕通知中的中文)
- **DX12 model replacement with GPU skinning support.** Mods can target compute passes that produce skinned vertex data as well as the draws that consume it. The DX12 runtime also supports texture and shader overrides, custom HLSL draws, and INI reloads while the game is running. [DX12 texture and model overrides](usage/dx12/texture-overrides.md)
- **DX12 tooling that connects a draw to its resources.** Use Hunting to identify shaders and buffers, then capture a frame to inspect draw and dispatch order, resource bindings, and exported files. The in-game profiling HUD and TIMING builds help diagnose rendering stalls. [DX12 frame analysis](usage/dx12/frame-analysis.md)
- **Extended DX11 INI authoring.** Resource and variable pools, buffer regions, GPU readback, command-list callbacks, and perceptual texture matching support mods that track per-object state or match textures by appearance. [DX11 INI reference](docs/ini-language-extensions/README.md)

## 🚀 Get started

1. Check which DirectX version your game is running and choose an appropriate game adapter and loader.
2. Download the [latest x64 release](https://github.com/StarBobis/ProjectBunny/releases/latest). Follow the [DX11 installation guide](usage/dx11/index.md) for `d3d11.dll` or the [DX12 installation guide](usage/dx12/index.md) for `d3d12.dll`.
3. Start with a mod made for that game and DirectX version. Keep the game adapter's `d3dx.ini` when updating the DLL.

Each release includes both proxy DLLs, `d3dcompiler_47.dll`, the DXC runtime, a sample `d3dx.ini`, and license notices. `Manifest.json` records the release version, source commit, and SHA-256 checksums. [Release packaging](docs/Release-Package.md)

If your setup uses `Run.exe`, get it from [SSMT5](https://github.com/StarBobis/SSMT5), which maintains the loader separately.

## 🛠️ Build and contribute

Use Windows x64 with Visual Studio C++ build tools, the Windows SDK, CMake, and Ninja. The DX11 backend uses C++17 and follows SpectrumQT's XXMI-Libs-Package. The DX12 backend uses C++20.

Build both Release x64 DLLs locally with no game deployment or launch. The wrapper collects the outputs in `x64/Release/`.

```powershell
.\scripts\build-cmake.ps1 -Configuration Release -Platform x64 -ReleasePackage
```

For a DX11-only build, run `./scripts/build_release_dx11_x64.ps1`. The DX12 development wrapper, `./scripts/build_release_x64.ps1`, can deploy to a configured game and launch it according to `build_config.json`.

[Feature reference](docs/README.md) · [Build guide](docs/Build-and-Integration.md) · [Release workflow](docs/Release-Package.md)

## 💛 Support and credits

Project Bunny builds on [3Dmigoto](https://github.com/bo3b/3Dmigoto) and [SpectrumQT's XXMI-Libs-Package](https://github.com/SpectrumQT/XXMI-Libs-Package).

<a href="https://afdian.com/a/NicoMico666"><img width="200" src="https://pic1.afdiancdn.com/static/img/welcome/button-sponsorme.png" alt="Support development on Afdian"></a>

![Star history](https://api.star-history.com/svg?repos=StarBobis/ProjectBunny&theme=default)
