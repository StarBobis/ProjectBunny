# 文档开发

本站使用 VitePress 2 默认主题，以 Bun 管理依赖和运行脚本。

## 本地开发

在 `usage` 目录执行：

```sh
bun install --frozen-lockfile
bun run docs:dev
```

## 构建与预览

```sh
bun run docs:build
bun run docs:preview
```

构建产物位于 `.vitepress/dist`。子路径部署通过 `VITEPRESS_BASE` 设置 `base`；默认 `/`。

## 发布 GitHub Pages

先提交并推送文档，再从仓库根目录手动运行：

```powershell
.\scripts\publish-docs.ps1
```

需要已登录的 GitHub CLI。脚本仅触发远端 `main` 的 `pages.yml`；不会提交、推送或上传本地未提交文件。也可在 GitHub Actions 的 **Publish documentation → Run workflow** 发布。

工作流只有 `workflow_dispatch`，commit、push 和 PR 均不触发发布。GitHub Pages 的 Source 保持 **GitHub Actions**。

Actions 使用 Bun 锁文件安装依赖，再调用 `scripts/build-docs.ps1` 构建，并从 Pages 设置读取站点子路径。本地检查相同路径：

```powershell
.\scripts\build-docs.ps1 -BasePath /ProjectBunny/
```

## 维护内容

- `index.md`：文档首页。
- `dx11/`、`dx12/`：按图形 API 分开的用户文档。
- `guide/index.md`：旧指南入口。
- `.vitepress/config.ts`：站点设置、导航和侧边栏。

新增页面后，在配置中添加相应导航或侧边栏链接。`README.md` 是维护说明，不作为站点页面发布。

## 内容核对

用户页只写操作、语法、参数和使用限制，不放同步过程或未经测量的性能结论。

| 主题 | 用户页 | 核对来源 |
| --- | --- | --- |
| DX11 配置 | configuration、settings、presets-input | `Dependencies/d3dx.ini`、`src/DirectX11/IniHandler.cpp`、`Override.cpp` |
| DX11 命令与表达式 | command-lists、expressions、runtime-values、draw | `src/DirectX11/CommandList.cpp`、`CommandList.h` |
| DX11 资源与扩展 | resources、resource-copy、regions、metadata、pools、readback、perceptual-hash、layouts | 上述解析器及 `docs/ini-language-extensions/` |
| DX11 着色器与抓帧 | shaders、shader-regex、texture-overrides、custom-shaders、frame-analysis | `Dependencies/d3dx.ini`、INI/命令解析器 |
| DX12 语言与输入 | configuration、expressions、command-lists、compatibility | `src/DirectX12/Config/DX12Config.h`、`src/common/MigotoCommandVmUtils.hpp`、`MigotoKeyBindingUtils.hpp` |
| DX12 资源与替换 | resources、texture-overrides、shaders | `DX12CommandListExecutor.h`、`DX12ShaderOverrideRuntime.cpp`、`DX12PreSkinRuntime.cpp` |
| DX12 自定义绘制 | custom-shaders、recipes | `DX12CustomShaderElement.h`、`DX12CustomShaderDevice.h`、`DX12CustomShaderPipeline.h`、`DX12CustomShaderCompile.h` |
| DX12 工具与参数 | hunting、frame-analysis、runtime-values | `DX12RuntimeControl.h`、`DX12VmFrameVariablesUtils.h`、`DX12FrameAnalysisState.h` |

支持范围须检查解析、加载与执行，避免把“字段存在”写成“功能已接入”。

验证包括 VitePress 构建、导航覆盖、相对链接和代码块配对。DX12 recipes 的两段 HLSL 已以 `main` 入口通过 DXC 的 VS/PS 6.0 编译；游戏内绘制仍待验证。含示例 Hash、资源文件或管线前提的 INI 片段需在对应游戏验证。
