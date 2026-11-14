# 按键与变量

在 Mod 的 INI 中添加：

```ini
[Constants]
global persist $variant = 0

[KeyVariant]
key = NO_MODIFIERS F6
type = cycle
$variant = 0, 1
```

按 F10 重载。F6 在 0、1 间循环，选择保存到用户设置。变量必须被替换逻辑引用，才会改变画面。

## 按键类型

| `type` | 行为 |
| --- | --- |
| `cycle` | 按列表循环 |
| `hold` | 按住应用，松开恢复 |
| `toggle` | 按下应用，再次按下恢复 |

反向循环可用 `back`：

```ini
[KeyCycle]
key = NO_MODIFIERS F6
back = NO_MODIFIERS F7
type = cycle
$variant = 0, 1, 2
```

此段替代第一个按键示例，避免 F6 重复绑定。

## 组合键

字母与数字直接写，如 `K`、`1`；其他按键可写 `F6`、`VK_NUMPAD1`、`RBUTTON`。

- `ctrl K`：Ctrl + K。
- `NO_ALT K`：未按 Alt 时的 K。
- `NO_MODIFIERS F6`：未按 Ctrl、Alt、Shift、Win 时的 F6。

F8–F12 和小键盘已用于内置工具，Mod 按键尽量避开。

DX12 的 Key 参考字段为 `key`、`back`、`type`、`condition`、`run` 和 `$变量` 的值列表。多个变量列表按循环位置配对。

DX11 的 `smart`、`wrap`、`transition`、释放延时和多后端输入配置不能视为同等支持；需要复杂变化时通过命令列表显式实现。

## 条件调用

```ini
[KeyNext]
key = NO_MODIFIERS F6
run = CommandListNext

[CommandListNext]
if $variant == 0
    $variant = 1
else
    $variant = 0
endif
```

它是独立的 F6 示例，使用前删除其他 F6 绑定。
