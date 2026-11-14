# Project Bunny 重装小兔

[English](README.md) | **简体中文**

[![GitHub stars](https://img.shields.io/github/stars/StarBobis/ProjectBunny?style=social)](https://github.com/StarBobis/ProjectBunny/stargazers) [![GitHub forks](https://img.shields.io/github/forks/StarBobis/ProjectBunny?style=social)](https://github.com/StarBobis/ProjectBunny/network/members) [![GitHub issues](https://img.shields.io/github/issues/StarBobis/ProjectBunny)](https://github.com/StarBobis/ProjectBunny/issues) [![GitHub license](https://img.shields.io/github/license/StarBobis/ProjectBunny)](https://github.com/StarBobis/ProjectBunny/blob/main/LICENSE.txt)

Project Bunny 基于 3Dmigoto 进行重构与扩展，支持 DirectX 11 和 DirectX 12，重点推进 DX12 开发。

## 主要特点

- 使用 CMake 和 Ninja 构建。
- DX11 基于 [SpectrumQT/XXMI-Libs-Package](https://github.com/SpectrumQT/XXMI-Libs-Package) 持续开发，使用 C++17。
- DX12 使用 C++20。
- DX11 与 DX12 一起发布，均为 Release x64。

## 下载与文档

从 [GitHub Releases](https://github.com/StarBobis/ProjectBunny/releases/latest) 下载最新版本。`ProjectBunny-PACKAGE-vX.Y.Z.zip` 包含 `d3d11.dll`、`d3d12.dll`、`d3dcompiler_47.dll`、DXC 运行库、示例 `d3dx.ini` 和许可证。`Manifest.json` 提供版本、源提交及 SHA256 校验和。

加载器（`Run.exe`）由 [SSMT5](https://github.com/StarBobis/SSMT5) 维护和提供，本仓库不再构建或分发。

详细说明见[用户手册源码](usage/)与[发布流程](docs/Release-Package.md)。

## 开发环境

- Windows x64，当前在 Windows 11 Pro 下开发。
- Visual Studio C++ x64 构建工具及 Windows SDK。
- CMake 和 Ninja，可通过 PATH 或 Visual Studio 自带的 CMake 工具使用。
- 推荐使用 VS Code 与 C++ Extension Pack。

目前不维护 32 位构建。

## 构建

同时构建 DX11 和 DX12 的 Release x64，不部署或启动游戏：

```powershell
.\scripts\build-cmake.ps1 -Configuration Release -Platform x64 -ReleasePackage
```

产物复制至 `x64/Release/`，CMake 构建目录为 `build/cmake-x64-release/`。

DX12 开发构建（可能根据 `build_config.json` 部署并启动游戏）：

```powershell
.\scripts\build_release_x64.ps1
```

仅构建 DX11 Release x64，不部署：

```powershell
.\scripts\build_release_dx11_x64.ps1
```

产物为 `build/dx11-release-x64/src/DirectX11/d3d11.dll`。

## 发布版本

```powershell
.\scripts\publish_release.ps1
```

脚本构建双 DX，自动递增最新 Release 的 patch 版本，打包并发布到本仓库。指定版本时添加 `-Version X.Y.Z`。所有附件先上传至草稿，成功后公开。支持 `-Token`、`GITHUB_TOKEN`、`GH_TOKEN` 或 Windows Git Credential Manager 提供凭据。

## 支持开发

<a href="https://afdian.com/a/NicoMico666"><img width="200" src="https://pic1.afdiancdn.com/static/img/welcome/button-sponsorme.png" alt="Support development on Afdian"></a>

## Star 历史

![Star 历史](https://api.star-history.com/svg?repos=StarBobis/ProjectBunny&theme=default)

## 特别感谢

- [原版 3Dmigoto](https://github.com/bo3b/3Dmigoto)
- [SpectrumQT 的 XXMI-Libs-Package](https://github.com/SpectrumQT/XXMI-Libs-Package)
