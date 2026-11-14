# 管理 Mod

## 安装

将 Mod 解压到 `Mods` 下，保留资源与 INI 的相对位置：

```text
Mods/
└─ Example/
   ├─ mod.ini
   ├─ texture.dds
   └─ mesh.buf
```

确认 `d3dx.ini` 包含：

```ini
[Include]
include_recursive = Mods
exclude_recursive = DISABLED*
exclude_recursive = desktop.ini
```

进入游戏后按 F10。贴图、模型与按键应按 Mod 说明生效；出现警告先查日志。

## 禁用

将目录改名为 `DISABLED Example`，再按 F10。恢复原名即可启用。此方式依赖上面的排除规则。

## 冲突

同一角色或资源先只启用一个 Mod。出现重叠、闪烁或缺失时，禁用其他替换同一目标的 Mod，再重载。

不要靠调整文件夹顺序解决冲突；按 Mod 作者提供的合并方案处理。

## ShaderFixes

传统着色器补丁放在 `ShaderFixes`，不是任意 `Mods` 子目录。按 F10 重载；文件名及格式遵循补丁包原样。

## 保存的选项

声明为 `persist` 的变量写入 `d3dx_user.ini`，重启后恢复。不要把它当作主配置编辑。

需要全部恢复默认时，备份后按 **Ctrl + Alt + F10**。这会清除整份用户设置，不只当前 Mod。
