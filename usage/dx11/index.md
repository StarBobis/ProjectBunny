# 安装

适用于以 DirectX 11 运行的游戏，核心文件为 `d3d11.dll`。

## 使用游戏适配包

1. 安装该游戏支持的加载器及适配包。
2. 关闭游戏，备份现有 `d3d11.dll`、`d3dx.ini` 和 `d3dx_user.ini`。
3. 按适配包要求放置 DLL，通过加载器启动游戏。
4. 安装一个已适配当前游戏版本的 Mod，确认效果。

使用 XXMI Launcher 时，在启动器内选择对应游戏；不要用通用配置覆盖游戏适配包的 `d3dx.ini`。

## 文件位置

以下目录以加载器部署位置为准，不一定在游戏根目录：

```text
d3d11.dll
d3dx.ini
Mods/
ShaderFixes/
d3dx_user.ini       ← 运行后保存的设置
d3d11_log.txt       ← 日志
```

仓库中的 `Dependencies/d3dx.ini` 是 DX11 配置模板。模板有一个注释字符使用旧编码。手动以模板建配置时，可用 Windows-1252 打开模板，再把部署副本另存为 UTF-8。模板默认 `hunting = 0`，需要定位资源时先开启 Hunting。

## 更新与移除

从 [GitHub Releases](https://github.com/StarBobis/ProjectBunny/releases/latest) 下载 x64 包，关闭游戏后替换适配包中的 `d3d11.dll`，再重启游戏。保留现有的游戏配置、持久设置和 Mod。发布包里的 `d3dx.ini` 是 DX12 配置，DX11 手动建配置时请使用上面的 DX11 模板。F10 仅重载 INI。

从 v1.3.22 起，可用 [Filter 与 PS 槽位匹配](./resources#按-dhash-匹配像素着色器贴图槽位)。已有贴图匹配配置可继续使用。更新后若遇到绘制异常，可按[运行期优化排错](./troubleshooting#更新后排查-dx11-运行期优化)对照。

移除时先在加载器中禁用对应组件。手动部署的文件按备份恢复，避免删除其他工具共用的 DLL。

下一步：[管理 Mod](./mods) · [MCP 自动化与定点抓取](./mcp)。
