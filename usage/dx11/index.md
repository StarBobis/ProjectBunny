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

仓库中的 `Dependencies/d3dx.ini` 是 DX11 配置模板。模板默认 `hunting = 0`，没有定位面板不代表 DLL 未加载。

## 更新与移除

更新 DLL 后重启游戏；F10 不会加载新版 DLL。保留游戏适配配置和 Mod。

移除时先在加载器中禁用对应组件。手动部署的文件按备份恢复，避免删除其他工具共用的 DLL。

下一步：[管理 Mod](./mods)。
