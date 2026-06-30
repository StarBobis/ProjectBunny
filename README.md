
# DX12部分失败了

原因：无法解决为了维持常驻资源池而导致CPU热路径太多导致GPU占用率低而导致的掉帧问题

如果不掉帧只能放弃维护资源池，则模型替换\GPU-PreSkinning类型Mod无法生效

就像马斯克说的：不要优化一个不该存在的事物

此项目停止开发


# Project Bunny 重装小兔

[![GitHub stars](https://img.shields.io/github/stars/StarBobis/ProjectBunny?style=social)](https://github.com/StarBobis/ProjectBunny/stargazers) [![GitHub forks](https://img.shields.io/github/forks/StarBobis/ProjectBunny?style=social)](https://github.com/StarBobis/ProjectBunny/network/members) [![GitHub issues](https://img.shields.io/github/issues/StarBobis/ProjectBunny)](https://github.com/StarBobis/ProjectBunny/issues) [![GitHub license](https://img.shields.io/github/license/StarBobis/ProjectBunny)](https://github.com/StarBobis/ProjectBunny/blob/main/LICENSE.txt)

This project is used to refactor and improve the original 3Dmigoto project.


# Core Change

- 使用CMake重构项目，更加灵活
- 使用Ninja编译项目
- DX11部分随 SpectrumQT的 XXMI-Libs-Package 版本 3Dmigoto 而更新.
- 使用C++ 17标准，而不是C++14标准
- 使用VSCode开发而不是Visual Studio开发


# Development Environment

- Windows 11 Pro
- VSCode
- C++ Extension Pack [VSCode extension]
- C++ 14 Standard

# Build

Build the Debug x64 version:

```
.\build_debug_x64.ps1
```

Build the Release x64 version:

```
.\build_release_x64.ps1
```

(32-bit builds are not currently maintained because there are very few use cases and users. The project focuses on x64 first.)

# Support Development

<a href="https://afdian.com/a/NicoMico666"><img width="200" src="https://pic1.afdiancdn.com/static/img/welcome/button-sponsorme.png" alt=""></a>


# Star History

![Star Trend](https://api.star-history.com/svg?repos=StarBobis/ProjectBunny&theme=default)


# Special Thanks

- Original 3Dmigoto Repository: https://github.com/bo3b/3Dmigoto
- SpectrumQT's 3Dmigoto Fork Repository: https://github.com/SpectrumQT/XXMI-Libs-Package

