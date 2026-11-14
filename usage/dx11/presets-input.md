# 预设、过渡与输入开关

## 预设

Preset 在条件成立或被调用时应用变量/IniParams 覆盖，不是新的绘制命令入口。

```ini
[PresetExample]
x = 0.5
transition = 100
transition_type = linear
release_transition = 200
release_transition_type = cosine

[ShaderOverrideExample]
hash = 0123456789abcdef
preset = PresetExample
```

替换 Hash。时间单位为毫秒。`condition` 可限定生效条件；`run` 可调用激活/释放命令列表。

## Key 的完整选项

| 字段 | 作用 |
| --- | --- |
| `key` | 正向键，可列多个共享同一状态的绑定 |
| `back` | cycle 的反向键 |
| `type` | hold、toggle、cycle；省略为普通按下应用 |
| `condition` | 触发条件 |
| `run` | 调用命令列表 |
| `wrap` | cycle 是否首尾循环 |
| `smart` | cycle 根据当前值选择后续项 |
| `delay/release_delay` | 按下/释放延时 |
| `transition/release_transition` | 按下/释放过渡时长 |
| `transition_type/release_transition_type` | linear 或 cosine |

cycle 中多个数值列表按位置配对。Key 直接设值用简单值；复杂表达式放到 `run` 的命令列表。

## 暂停 Mod 按键

```ini
[Input]
input = 1
toggle_input = ctrl alt shift VK_END
input_disable_mode = mods
```

Ctrl + Alt + Shift + End 切换输入。

| 模式 | 关闭输入后 |
| --- | --- |
| `mods` | 暂停递归加载 Mod 的绑定，工具键仍可用 |
| `all` | 暂停普通 Mod 与工具绑定，保留输入切换键 |

不影响游戏自身输入。输入开关运行状态跨 F10 保留；关闭时不会立即恢复正在 HOLD 的值，恢复后重新采样释放。

Mod 分类依据 INI 实际递归加载来源，修改 `namespace` 不能改变分类。旧 `[System] disable_input/toggle_input` 应迁移到 `[Input]`。
