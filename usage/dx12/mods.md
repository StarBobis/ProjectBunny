# 管理 Mod

将 DX12 Mod 解压到 `Mods`，保留 INI 与资源的相对位置：

```text
Mods/
└─ Example/
   ├─ mod.ini
   ├─ texture.dds
   └─ mesh.buf
```

`d3dx.ini` 需要包含：

```ini
[Include]
include_recursive = Mods
exclude_recursive = DISABLED*
exclude_recursive = desktop.ini
```

## 重载

修改 INI 或替换资源后按 F10，等待重载完成。存在错误或警告时先检查提示与日志，不要连续按键。

F10 重载 Mod 配置，不替换正在运行的 DLL。

## 禁用与冲突

把 Mod 目录改名为 `DISABLED Example`，按 F10。恢复原名即可启用。

模型重叠、贴图错误或闪烁时，只保留一个替换该目标的 Mod，再逐个启用。DX11 Mod 需要适配，复制目录不能完成转换。

## 保存的选项

`global persist` 变量默认保存在 `d3dx_user.ini`。可在主配置指定路径：

```ini
[Include]
user_config = d3dx_user.ini
```

恢复默认：退出游戏，备份并移走该文件，再启动。运行中删除可能被自动保存重新写回。

DX11 的 **Ctrl + Alt + F10** 清空设置快捷键不适用于这里。
