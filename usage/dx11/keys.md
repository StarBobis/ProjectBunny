# 按键与变量

在 Mod 的 INI 中声明变量和按键。以下例子按 F6 在 0、1 间循环，并保存选择：

```ini
[Constants]
global persist $variant = 0

[KeyVariant]
key = F6
type = cycle
$variant = 0, 1
```

变量只有被替换逻辑引用时才会改变画面。

## 按键类型

| `type` | 行为 |
| --- | --- |
| `cycle` | 每次按下切换到列表的下一值 |
| `hold` | 按住时应用，松开后恢复原值 |
| `toggle` | 首次按下应用，再次按下恢复原值 |

临时设值：

```ini
[KeyTemporary]
key = NO_MODIFIERS F7
type = hold
$variant = 1
```

## 组合键

| 写法 | 按键 |
| --- | --- |
| `K` | 字母 K |
| `ctrl K` | Ctrl + K |
| `NO_ALT K` | K，且未按 Alt |
| `NO_MODIFIERS F6` | F6，且未按 Ctrl、Alt、Shift、Win |
| `VK_NUMPAD1` | 小键盘 1 |
| `XB_A` | Xbox 手柄 A |

普通 `key = F6` 不排除修饰键。避免与游戏或其他 Mod 重复绑定。

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

这是另一种切换写法；不要与前面的 F6 示例同时启用。修改后按 F10。
