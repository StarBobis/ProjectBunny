# Project Bunny 文档

[在线手册的源码](../usage/)按使用场景组织，适合安装 DLL、配置 Mod 和查找示例。本目录按功能域组织，适合核对配置项、查阅实现边界和维护项目。

## DirectX 11

| 功能 | 参考 |
| --- | --- |
| 配置查找、Include、命名空间、版本、重载 | [配置参考](DX11-Configuration.md) |
| 着色器与纹理覆盖、正则补丁、自定义着色器 | [Mod 编写](DX11-Mod-Authoring.md) |
| 命令列表、绘制、表达式、按键、预设和持久化 | [命令参考](DX11-Command-Reference.md) |
| 资源加载、槽位、复制、布局、读回和池 | [资源参考](DX11-Resource-Reference.md) |
| Hunting、帧分析、日志和性能观察 | [诊断](DX11-Diagnostics.md) |
| 深度重建、反投影与立体渲染修正 | [HLSL 反编译修正](DX11-Legacy-Stereo.md) |
| 扩展语法、命令、逐项参数与组合示例 | [INI 扩展语言](ini-language-extensions/README.md) |
| 上游来源、移植决策和验证记录 | [上游同步](DX11-Upstream-Sync.md) |

## DirectX 12

| 功能 | 参考 |
| --- | --- |
| 加载、配置搜索、Include、热重载、键绑定 | [配置参考](DX12-Configuration.md) |
| 覆盖匹配、着色器、管线状态和自定义着色器 | [覆盖与着色器](DX12-Overrides-and-Shaders.md) |
| 命令、变量、表达式、资源和替换 | [命令与资源参考](DX12-Command-and-Resource-Reference.md) |
| 定位、帧分析、日志、兼容性与崩溃排查 | [诊断与兼容性](DX12-Diagnostics-and-Compatibility.md) |
| 帧生成功能的历史修复 | [DLSSFG 修复报告](DX12-DLSSFG-修复报告.md)、[交接记录](DX12-DLSSFG-交接文档.md) |
| 性能工作与测量结果 | [Mod 性能报告](DX12-Mod性能优化报告.md)、[剩余热点记录](DX12-RemainingHotspots-20260918.md) |

先根据游戏实际使用的 DirectX 版本选择文档。同名 INI 节、命令和键绑定的行为按各自后端的参考为准。

## 构建与发布

- [构建、部署和集成](Build-and-Integration.md)
- [双 DX x64 发布流程](Release-Package.md)
- [站点构建](../usage/README.md)
