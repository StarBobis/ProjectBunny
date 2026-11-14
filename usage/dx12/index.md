# 安装

适用于以 DirectX 12 运行的游戏，核心文件为 `d3d12.dll`。

1. 关闭游戏，备份现有 DLL、`d3dx.ini` 和 `d3dx_user.ini`。
2. 使用支持该游戏的 DX12 加载方案，部署 `d3d12.dll` 及其配套文件。
3. 保留游戏适配包的配置；首次手动配置时，以仓库 `config/d3dx.ini` 为模板。
4. 启动游戏，按 F10，检查配置重载提示。

配置模板含 `[Loader]`，不表示任意加载器都支持 DX12。使用 XXMI Launcher 时，以启动器对应游戏组件的支持情况为准。

## 文件位置

```text
d3d12.dll
d3dx.ini
Mods/
ShaderFixes/
d3dx_user.ini       ← 保存的设置
d3d12_log.txt       ← 诊断日志（取决于构建版本）
```

DX12 优先查找进程当前工作目录的 `d3dx.ini`，其次查找 DLL 所在目录。修改配置无效时，先排除两处存在不同配置。

## 与 DX11 的区别

| 项目 | DX12 |
| --- | --- |
| Mod | 使用 DX12 游戏适配版本 |
| 定位开关 | 小键盘 0 |
| F9 | 导出着色器 |
| 配置模板 | `config/d3dx.ini` |
| DX11 全局设置 | 不直接套用 `[Hunting]`、`[Device]`、`[Rendering]`、`[Logging]` |

替换 DLL 后重启游戏。移除时在加载器中禁用组件，或恢复手动部署前的备份。

下一步：[管理 Mod](./mods)。
