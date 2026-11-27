# 定位与导出

随包配置使用**小键盘 0**开启或关闭定位。以下表格列出默认按键。数字均指小键盘，使用时检查 Num Lock。

## 查找目标

停在目标可见的场景，切换选择，观察哪些物体被跳过绘制。找到目标后复制 Hash。

| 目标 | 上一个 | 下一个 | 复制 Hash |
| --- | --- | --- | --- |
| 像素着色器 PS | 1 | 2 | 3 |
| 顶点着色器 VS | 4 | 5 | 6 |
| 索引缓冲区 IB | 7 | 8 | 9 |
| 顶点缓冲区 VB | `/` | `*` | `-` |
| 计算着色器 CS | `.` + 1 | `.` + 2 | `.` + 3 |

按**小键盘 `+`**清除选择。复制 Hash 不会导出着色器文件。

## 更改定位快捷键

在主配置的 `[Hunting]` 段内设置按键，用当前重载快捷键（默认 F10）使修改生效。例如用 Ctrl + 右方向键选择下一个像素着色器，长按每秒前进四项。

```ini
[Hunting]
next_pixelshader = ctrl no_shift no_alt VK_RIGHT
repeat_rate = 4
```

上一个、下一个和复制 Hash 分别使用 `previous_`、`next_` 和 `mark_` 前缀。目标后缀是 `pixelshader`、`vertexshader`、`computeshader`、`indexbuffer` 或 `vertexbuffer`。定位开关对应 `toggle_hunting`，清除选择对应 `done_hunting`。`repeat_rate = 0` 时每次按键只前进一步，复制 Hash 始终每次按键只触发一次。

组合键用空格分隔。`no_modifiers` 要求松开 Ctrl、Alt、Shift 和两个 Win 键，`no_VK_DECIMAL` 要求松开小键盘小数点。等号右侧留空可禁用该操作。旧配置省略这些键时沿用原快捷键。也支持 XInput 手柄。例如将 `toggle_hunting` 设为 `XB2_START`，即可用 2 号手柄的 Start 键切换定位。键盘与手柄可以组成同一组合键。

## 导出

| 按键 | 作用 |
| --- | --- |
| F8 | 开启 Hunting 后抓取帧分析与资源 |
| F9 | 抓取并导出帧内着色器到 `ShaderFixes` |
| F10 | 重载 Mod 配置 |

帧分析对应 `analyse_frame`，配置重载对应 `reload_config`。F9 为 DX12 固定的着色器导出键。让目标保持可见，按一次后等待完成提示。F8 可能卡顿并产生大量文件；不要连续触发。

F8 输出位于 DLL 旁的 `FrameAnalysis-日期-时间` 目录；F9 输出位于 DLL 旁的 `ShaderFixes`。

F9 的汇编或分析输出不等于可直接编辑的 HLSL。导出失败时检查提示、目录权限与日志。

## 性能面板

默认按 F11 循环切换性能面板，按 F12 冻结或恢复显示。对应的配置键为 `monitor_performance` 和 `freeze_performance_monitor`。数据是否可用取决于构建版本；需要详细诊断时使用 TIMING 版。
