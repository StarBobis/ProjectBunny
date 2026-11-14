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

## Rendering

| 字段 | 用途 |
| --- | --- |
| `shader_hash/texture_hash` | Hash 策略，改变后已有 Hash 可能不再匹配 |
| `override_directory/cache_directory/storage_directory` | 补丁、缓存、原始着色器目录 |
| `cache_shaders` | 着色器缓存 |
| `cache_resource_data` | CPU 缓冲数据缓存类型 |
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
