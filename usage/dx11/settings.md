# 全局设置

以下配置写入主 `d3dx.ini`。游戏适配包已有的系统、设备及资源跟踪设置优先保留。

## Loader

| 字段 | 用途 |
| --- | --- |
| `target` | 实际游戏进程名 |
| `loader` | 允许加载 DLL 的加载器 |
| `module` | DX11 DLL 文件名 |
| `launch` | 外部 Loader 自动启动路径 |
| `require_admin` | 需要时提升 Loader 权限 |
| `delay` | 确认加载后延迟退出，-1 不自动退出 |

XXMI Launcher 不采用模板中所有传统 Loader 参数；用启动器自己的游戏配置管理启动。

## System

| 字段 | 用途 |
| --- | --- |
| `screen_width/screen_height` | 适配中使用的屏幕尺寸 |
| `proxy_d3d11/proxy_d3d9` | 其他代理库链，需核对兼容性 |
| `dll_initialization_delay` | DLL 初始化延迟 |
| `config_initialization_delay` | 配置初始化延迟 |
| `skip_early_includes_load` | 延后递归 include 的早期加载 |
| `load_library_redirect` | 动态库重定向模式 |
| `check_foreground_window` | 输入的前台窗口检查 |
| `hook` | 原生接口钩子策略 |
| `allow_check_interface/allow_create_device/allow_platform_update` | 接口与设备兼容开关 |
| `settings_auto_save_interval` | 持久值自动保存间隔，秒 |
| `clear_unknown_settings` | 清理未知持久设置 |
| `force_detect_color_space` | PNG 额外 sRGB 元数据检测 |

初始化和 hook 设置需按游戏适配要求调整，通常需要重启。不要一次修改多个兼容开关来试错。

## Device

| 字段 | 用途 |
| --- | --- |
| `width/height/refresh_rate` | 请求分辨率、刷新率 |
| `filter_refresh_rate` | 限制枚举的刷新率 |
| `full_screen` | 全屏模式覆盖 |
| `force_full_screen_on_key/toggle_full_screen` | 全屏快捷键 |
| `allow_windowcommands` | 窗口控制许可 |
| `upscaling/upscale_mode` | 上采样及模式 |
| `get_resolution_from` | 分辨率信息来源，模板为 swap_chain |
| `hide_cursor` | 光标隐藏 |

这些是 DX11 设备控制，不能复制到 DX12 期待相同行为。

## Input

| 选项 | 行为 |
| --- | --- |
| `optimize_hotkey_dispatch` | 默认 `1`。同一轮热键分发复用相同按键和组合键的状态。模组的简单标量门控可提前拒绝无效的按下动作，仍跟踪按住和松开。设为 `0` 可恢复逐绑定查询。关闭 `mod_runtime_optimizations` 时也会回退。 |

## Rendering

| 字段 | 用途 |
| --- | --- |
| `shader_hash/texture_hash` | Hash 策略，改变后已有 Hash 可能不再匹配 |
| `override_directory/cache_directory/storage_directory` | 补丁、缓存、原始着色器目录 |
| `cache_shaders` | 着色器缓存 |
| `cache_resource_data` | CPU 缓冲数据缓存类型 |
| `mod_runtime_optimizations` | 默认 `1`。设为 `0` 后，跨绘制的 TextureOverride 空候选复用、资源和槽位写入去重、SlotFix 空清理跳过、标量条件预解析和字面量赋值批处理均切回原路径。此开关也控制 ShaderOverride 查询、OM 保存、资源别名解析及绘制和计算参数预解析等优化。显式配置的 `draw_has_override_slots` 和 `auto_check_texture_slots` 继续生效 |
| `expression_short_circuit` | 默认 `1`。仅对确认可跳过求值的 `&&`、`\|\|` 使用短路。设为 `0` 可单独恢复逻辑表达式和标量条件的原求值路径。关闭 `mod_runtime_optimizations` 时也沿用原求值路径 |
| `draw_gate_early_reject` | 默认 `1`。对可证明安全的 `DRAW_HAS_OVERRIDE` 分支，提前筛选并在未命中时缩短命令派发。设为 `0` 可单独对照原路径。关闭 `mod_runtime_optimizations` 时也沿用原路径 |
| `profile_draw_gate` | 默认 `0`。设为 `1` 后，CPU Summary 显示筛选、IB/VB/SRV 原生读取、候选判断、未命中入口的耗时、执行次数与快路径次数。核对调用量后设回 `0` 再比较整帧耗时，以免详细采样影响结果 |
| `shader_regex_skip_empty_links` | 默认 `1`。ShaderRegex 链接在当前条件可证明目标列表无工作时跳过派发。设为 `0` 可恢复原路径。关闭 `mod_runtime_optimizations` 或 `expression_short_circuit` 时也使用原路径 |
| `profile_shader_regex_links` | 默认 `0`。设为 `1` 后，CPU Summary 显示可判定的链接、确认空的链接、跳过和仍派发的空链接次数。核对计数后设回 `0`，再比较帧时 |
| `draw_gate_skip_empty_post` | 默认 `1`。已有绘制未命中结论、没有待恢复的槽位且所有相关分支可证明为空时，跳过 ShaderRegex 绘制后链接的派发。设为 `0` 可单独退回原路径。关闭 `draw_gate_early_reject` 或 `mod_runtime_optimizations` 也会回退 |
| `profile_post_draw_entry` | 默认 `0`。设为 `1` 后，CPU Summary 显示可判定的绘制后未命中链接、确认空、跳过和仍派发的次数。比较正常帧时前设回 `0` |
| `draw_has_override_slots` | `DRAW_HAS_OVERRIDE` 使用的槽位列表。按游戏和模组填写，例如 `ib vb[0:2] ps-t0`。缺省时走直接检查 |
| `auto_check_texture_slots` | 自动批量检查 TextureOverride 的槽位列表，例如 `ps-t[0:9]`。缺省时关闭自动批量检查 |
| `rasterizer_disable_scissor` | 全局关闭裁剪 |
| `track_texture_updates` | 跟踪纹理更新对 Hash 的影响 |
| `track_implicit_index_buffers` | 隐式索引缓冲跟踪 |
| `track_region_hashes` | 区域 Hash 跟踪，同时启用 VB/IB/CB 数据缓存 |
| `allow_buffer_resize` | 缓冲扩容 |
| `ini_params` | IniParams 槽位 |
| `recursive_include` | 着色器 include 搜索行为 |
| `assemble_signature_comments` | 汇编签名注释处理 |
| `disassemble_undecipherable_custom_data` | 难解码数据的反汇编处理 |
| `patch_assembly_cb_offsets` | 汇编 CB 偏移修补 |
| `export_fixed/export_shaders/export_hlsl/dump_usage` | 输出调试数据 |
| `dhash_mip_size/dhash_allow_rehash` | 感知 Hash 策略 |

DX11 配置模板在 `[Rendering]` 段显式开启运行期优化。遇到兼容问题时，在当前游戏加载器的 `d3dx.ini` 中把 `mod_runtime_optimizations` 改成 `0`，然后按 F10 重载。若游戏卡住或无法进入场景，退出游戏后修改该设置，再重新启动。排查结束后设回 `1`。

模板还保留 `fix_sv_position`、`fix_ZRepair_*`、`fix_InvTransform`、`fix_BackProjectionTransform*`、`fix_ObjectPosition*`、`fix_MatrixOperand*`、`recompile_all_vs` 等旧立体修复/反编译选项。它们针对具体 shader 算式，沿用适配包给出的完整组合；普通贴图/模型 Mod 无需打开。

## 日志

```ini
[Logging]
log_level = warning
show_warnings = 1
```

`log_level` 接受 `disabled/warning/info/debug`。合法值优先于旧 `calls/debug`；缺失或非法时才回退。旧模式 `calls = 1` 开 info，再加 `debug = 1` 开 debug，单独 debug 不开启日志。

`show_warnings` 独立控制屏幕/声音提醒。`unbuffered` 控制日志缓冲；`force_cpu_affinity`、`debug_locks`、`crash` 属于诊断选项，按复现需要开启，结束后恢复。

旧的 `[Profile]`、`[Stereo]`、`[ConvergenceMap]` 名称可能被 INI 接受，不代表本分支具备原版 NVIDIA 立体功能。不要仅凭节名存在推断功能支持。
