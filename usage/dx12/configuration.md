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
| `[Key名称]` | 按键 |
| `[CommandList名称]` | 复用命令 |
| `[Resource名称]` | 文件资源、占位资源 |
| `[TextureOverride名称]` | 资源与绘制匹配 |
| `[ShaderOverride名称]` | 按着色器 Hash 匹配 |
| `[CustomShader名称]` | 自定义绘制/计算 |
| `[ShaderRegex名称]` | 仅无 Pattern 的模型筛选命令路径 |

解析器识别部分 DX11 节名用于兼容，不等于运行时实现其功能。完整边界见[兼容范围](./compatibility)。

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

模板中的 `[Loader] target/module/loader` 描述外部加载方式；`module` 为 `d3d12.dll`。`launch`、`require_admin`、`delay` 等是否生效由所用加载器决定。

DX11 的 Device、Rendering、Hunting 和 Logging 参数不用于配置 DX12 的对应工具。不要把两个版本的主配置直接互换。

F10 重载 Mod 配置；DLL 与注入方式变更需要重启。
