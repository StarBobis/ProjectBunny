# Project Bunny

**English** | [简体中文](README.zh-CN.md)

[![GitHub stars](https://img.shields.io/github/stars/StarBobis/ProjectBunny?style=social)](https://github.com/StarBobis/ProjectBunny/stargazers) [![GitHub forks](https://img.shields.io/github/forks/StarBobis/ProjectBunny?style=social)](https://github.com/StarBobis/ProjectBunny/network/members) [![GitHub issues](https://img.shields.io/github/issues/StarBobis/ProjectBunny)](https://github.com/StarBobis/ProjectBunny/issues) [![GitHub license](https://img.shields.io/github/license/StarBobis/ProjectBunny)](https://github.com/StarBobis/ProjectBunny/blob/main/LICENSE.txt)

Project Bunny refactors and extends 3Dmigoto for DirectX 11 and DirectX 12, with a focus on DX12 development.

## Highlights

- CMake and Ninja build workflow.
- DX11 development based on [SpectrumQT/XXMI-Libs-Package](https://github.com/SpectrumQT/XXMI-Libs-Package), using C++17.
- DX12 development using C++20.
- Both APIs shipped together as Release x64 binaries.

## Downloads and documentation

Download the latest package from [GitHub Releases](https://github.com/StarBobis/ProjectBunny/releases/latest). `ProjectBunny-PACKAGE-vX.Y.Z.zip` contains `d3d11.dll`, `d3d12.dll`, `d3dcompiler_47.dll`, `Run.exe`, the DXC runtime, a sample `d3dx.ini`, and license notices. `Manifest.json` lists the version, source commit, and SHA256 checksums.

See the [user manual source](usage/) and [release workflow](docs/Release-Package.md) for details.

## Development environment

- Windows x64 (developed on Windows 11 Pro).
- Visual Studio C++ x64 build tools and Windows SDK.
- CMake and Ninja, available on PATH or through Visual Studio's CMake tools.
- VS Code with the C++ Extension Pack is the recommended editor.

32-bit builds are not currently maintained.

## Build

Build both DX11 and DX12 as Release x64, without deploying to or launching a game:

```powershell
.\scripts\build-cmake.ps1 -Configuration Release -Platform x64 -ReleasePackage
```

Outputs are copied to `x64/Release/`; the CMake build directory is `build/cmake-x64-release/`.

For the DX12 development build (may deploy and launch according to `build_config.json`):

```powershell
.\scripts\build_release_x64.ps1
```

For a DX11-only Release x64 build without deployment:

```powershell
.\scripts\build_release_dx11_x64.ps1
```

Its output is `build/dx11-release-x64/src/DirectX11/d3d11.dll`.

## Publish a release

```powershell
.\scripts\publish_release.ps1
```

This builds both APIs, increments the latest release's patch version, packages the files, and publishes to this repository. To select a version explicitly, add `-Version X.Y.Z`. All assets are uploaded to a draft before publication. Authentication supports `-Token`, `GITHUB_TOKEN`, `GH_TOKEN`, or Windows Git Credential Manager.

## Support development

<a href="https://afdian.com/a/NicoMico666"><img width="200" src="https://pic1.afdiancdn.com/static/img/welcome/button-sponsorme.png" alt="Support development on Afdian"></a>

## Star history

![Star history](https://api.star-history.com/svg?repos=StarBobis/ProjectBunny&theme=default)

## Acknowledgments

- [Original 3Dmigoto](https://github.com/bo3b/3Dmigoto)
- [SpectrumQT's XXMI-Libs-Package](https://github.com/SpectrumQT/XXMI-Libs-Package)
