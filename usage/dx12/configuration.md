# INI 与命名空间

DX12 优先读取进程工作目录的 `d3dx.ini`，其次读取 DLL 目录。配置路径确定后，include 与资源以所属文件的位置解析。

```ini
[Include]
include = Common\settings.ini
include_recursive = Mods
exclude_recursive = DISABLED*
exclude_recursive = desktop.ini
user_config = d3dx_user.ini
```

`include` 加载指定文件；`include_recursive` 递归加载目录。两者可重复，排除规则用于递归加载。用户设置最后加载，覆盖持久变量默认值。

## 可用配置入口

| 节 | 用途 |
| --- | --- |
| `[Constants]` | 全局变量、持久声明与 IniParams 初始化 |
| `[Present]` | 每帧命令 |
| `[Input]` | 快捷键的初始状态、开关键和禁用范围 |
| `[Key名称]` | 按键 |
| `[CommandList名称]` | 复用命令 |
| `[Resource名称]` | 文件资源、占位资源 |
| `[TextureOverride名称]` | 资源与绘制匹配 |
| `[ShaderOverride名称]` | 按着色器 Hash 匹配 |
| `[CustomShader名称]` | 自定义绘制/计算 |
| `[ShaderRegex名称]` | 仅无 Pattern 的模型筛选命令路径 |

解析器识别部分 DX11 节名用于兼容，不等于运行时实现其功能。完整边界见[兼容范围](./compatibility)。

## 临时禁用快捷键

`[Input]` 的 `input` 决定首次加载时是否启用快捷键，默认为 `on`。可以配置一个始终可用的开关键。

```ini
[Input]
input = on
input_disable_mode = mods
toggle_input = ctrl alt F7
```

| 禁用范围 | 按下开关键后的效果 |
| --- | --- |
| `mods`（默认） | 暂停通过 `include_recursive` 加载的文件里的 `[Key]`，包括循环的 `back`。主配置、显式 `include` 文件和 Hunting 快捷键仍可用。 |
| `all` | 同时暂停主配置、显式包含文件和 Hunting 的快捷键，包括 F10。再次按 `toggle_input` 可以重新启用。 |

如果同一文件既被显式包含，又出现在递归目录中，首次加载它的方式决定范围。当前一批配置里的包含指令会先处理，新文件自己的包含指令在下一批处理。修改 `namespace` 不改变范围。同一命名空间出现重复的 `[Key]` 节名时，以先加载的节为准。

重载会读取新的范围和开关键，并保留当前启用状态。在禁用期间松开 `hold` 键，重新启用后的第一帧会处理松开并恢复变量。

## 命名空间

递归加载的自定义节与变量按 INI 命名空间隔离。需要跨文件接口时，使用显式 namespace：

```ini
namespace = Example

[Constants]
global $enabled = 1

[CommandListUpdate]
$enabled = 1
```

从其他命名空间调用 `run = CommandList\Example\Update`。`[Present]` 和 `[Constants]` 仍为共享入口，内部命名变量保留所属命名空间。

## Loader 与系统设置

在 `[System]` 中设置 `check_foreground_window = 1` 后，只有游戏进程的窗口在前台时才处理 Mod 和 Hunting 快捷键。默认值为 0。若需要在另一个控制窗口中使用快捷键，可以填写它的完整标题，大小写必须一致。

```ini
[System]
check_foreground_window = 1
additional_foreground_window = My Controller
```

切到其他窗口时，正在进行的变量过渡会继续。已按下的 `hold` 会保持，回到允许的窗口后才检查按键是否已经松开。窗口标题最多可配置 259 个 UTF-16 代码单元。更换标题后按 F10 即可生效。删除标题再按 F10 会沿用上次的标题，重启游戏后才会清除该例外。

模板中的 `[Loader] target/module/loader` 描述外部加载方式；`module` 为 `d3d12.dll`。`launch`、`require_admin`、`delay` 等是否生效由所用加载器决定。

DX12 的设备、Hunting 与日志输出选项见 DX12 配置模板。表达式求值还支持以下两个与 DX11 同名的开关，默认均为 1。

```ini
[Rendering]
mod_runtime_optimizations = 1
expression_short_circuit = 1
```

例如在 `if $enabled && vertex_count > 0` 中，若 `$enabled` 为 0，就会跳过右侧比较。整个逻辑子表达式都满足 DX11 的可跳过条件时才会短路。`0 && random(1)` 仍会取一次随机数，含有时间读取或普通加减法的逻辑子表达式也会对两侧求值。

任一开关关闭、正在捕获帧或主 INI 的 `[Logging] log_level = debug` 生效时，逻辑运算对两侧求值。旧式 `calls = 1` 和 `debug = 1` 同时生效时也使用此规则。

这两个开关在 `[Constants]` 之前读取。首次加载和 F10 重载时，引用 Mod 变量（如 `$enabled`）都会回退到默认值 1。需要关闭时可直接填写 `0`。

F10 重载 Mod 配置；DLL 与注入方式变更需要重启。
