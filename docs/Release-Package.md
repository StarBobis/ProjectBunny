# 双 DX x64 Release 发布

执行 `powershell -ExecutionPolicy Bypass -File scripts/publish_release.ps1`，自动递增 ProjectBunny 最新正式版的 patch；首次发布使用 `-Version 1.3.16`。发布到 `StarBobis/ProjectBunny`，不会修改仓库可见性。

脚本构建 DirectX11、DirectX12 和 D3DCompiler_46，使用 Release x64，关闭 DX12_RELEASE_TIMING，不部署或启动游戏。构建缓存与打包文件放在 build 下。

产物参考 XXMI-Libs-Package 的扁平 PACKAGE zip + Manifest.json 布局：

- `ProjectBunny-PACKAGE-vX.Y.Z.zip`：d3d11.dll、d3d12.dll、d3dcompiler_47.dll、dxcompiler.dll、dxil.dll、d3dx.ini 和许可证。
- `Manifest.json`：包版本、源提交、configuration、architecture 与每个文件的 SHA256。这里使用校验和，不冒充上游签名格式。

上传至草稿，全部文件上传成功后公开为 latest。失败的草稿保留用于诊断；同版本重试前需处理该草稿与已有 tag，避免覆盖正式版。使用 Token 参数、环境变量或 Windows Git Credential Manager，不打印凭据。

本地验证打包使用 `-Version X.Y.Z -PackageOnly`，不会创建 Release；`-SkipBuild` 仅供复用已确认的 Release 构建。发布包版本与 CMake 的 DLL 内部版本是两个字段，脚本在不一致时告警。

2026-11-14：本地双 DX Release 构建通过；打包检查 AMD64 PE 与 SHA256。未进行游戏内验证。

Run.exe 由 SSMT5 维护和提供，ProjectBunny 不保留其源码、构建目标或发布副本。开发部署只更新 DLL，不覆盖目标目录已有的加载器。DX12 对外部 Run.exe 的引导进程识别仍保留。
