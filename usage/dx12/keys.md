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

## 按键条件

`condition` 在按键触发时求值。例如，`condition = $enabled && res_width >= 1920` 只在已声明的 `$enabled` 非零且分辨率宽度至少为 1920 时触发。这里可读取全局变量、`x0` 等 IniParams 分量、`time`、`frame_number`、`res_width`、`res_height`、`effective_dpi` 和 `hunting`。按键条件会读取逻辑运算两侧的值。

按键处于命令列表之外，`frame_time`、`fps`、`frame_analysis` 和 `draw_type` 等操作数只能放在命令列表内。按键条件写错时，加载会告警，该按键随后按无条件执行。即使写成 `0 && draw_type` 也会触发这个回退。同一段有多行 `condition` 时取第一条非空值。

普通按键条件最多读取 259 个 UTF-16 代码单元。对于 `type = cycle`，可写 `condition = $enabled, 0, 1`，每档单独解析，单档条件可以超过上述长度。某一档条件写错，只会使该档按无条件执行。

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
