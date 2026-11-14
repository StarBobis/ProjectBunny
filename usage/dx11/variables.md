# 变量与持久设置

```ini
[Constants]
global $enabled = 1
global persist $variant = 0
global locked $scale = 1.25

[CommandListUpdate]
local $next = $variant + 1
$variant = $next
```

| 声明 | 生命周期 |
| --- | --- |
| `global` | 当前配置内共享，重载时重新初始化 |
| `global persist` | 保存并在下次启动、重载后恢复 |
| `local` | 命令列表局部作用域 |
| `locked` | 可读，禁止后续赋值 |

变量先声明后使用。初始化表达式在解析时取值，不会随引用变量自动变化；运行时计算放入命令列表。

## IniParams

`x`、`y`、`z`、`w` 是第 0 组分量；`x1`、`y1` 等对应后续组：

```ini
[Constants]
x = 1
y1 = 0.5
```

它们供着色器读取；`$variant` 等普通变量不会自动成为着色器参数。需在命令中显式赋给所用分量。

`[Rendering] ini_params` 指定 IniParams 的资源槽，模板为 `120`。修改槽位必须同步修改着色器声明，避免占用游戏使用的槽。

## 保存、恢复与清理

`persist` 值写入 `[Include] user_config`，默认 `d3dx_user.ini`。重载、自动保存等安全时机会保存设置。

```ini
[System]
settings_auto_save_interval = 60
clear_unknown_settings = 0
```

`clear_unknown_settings = 0` 保留暂时禁用 Mod 的未知设置；缺省解析值为 `1`，配套模板显式设为 `0`。启用清理时，未知集合需经过重载宽限期后才删除。

移除变量的 `persist` 后，旧保存值不再覆盖该声明。`locked` 值也不会作为可变持久值恢复。

恢复全部默认：先备份，再按 Ctrl + Alt + F10。只想重置单个 Mod 时，退出游戏后处理其对应的保存项，避免误清其他 Mod。
