# INI 与命名空间

`d3dx.ini` 是入口；Mod 的 INI 由 `[Include]` 加载。`;` 开头为注释。节名、命令名不区分大小写；文件路径保留原样。

```ini
[Include]
include = Common\settings.ini
include_recursive = Mods
exclude_recursive = DISABLED*
exclude_recursive = desktop.ini
user_config = d3dx_user.ini
```

| 项目 | 用法 |
| --- | --- |
| `include` | 加载指定 INI，可重复写 |
| `include_recursive` | 递归加载目录中的 INI，可重复写 |
| `exclude_recursive` | 排除递归加载中的名称模式 |
| `user_config` | 最后加载的持久设置文件 |

资源文件优先相对其所属 INI 查找。移动 INI 时一起移动配套资源，或调整路径。

## 节的职责

| 节 | 作用 |
| --- | --- |
| `[Constants]` | 声明变量，初始化命令 |
| `[Present]` | 每帧执行命令 |
| `[Key名称]`、`[Preset名称]` | 按键与临时预设 |
| `[TextureOverride名称]` | 匹配资源与绘制 |
| `[ShaderOverride名称]` | 匹配着色器 |
| `[ShaderRegex名称]` | 按汇编模式匹配、修改着色器 |
| `[Resource名称]`、`[Pool名称]` | 声明资源与池 |
| `[CustomShader名称]` | 自定义着色器、状态与绘制 |
| `[CommandList名称]` | 可复用命令 |
| `[ClearRenderTargetView]` | 渲染目标清理前后命令 |
| `[ClearDepthStencilView]` | 深度模板清理前后命令 |
| `[ClearUnorderedAccessViewUint]`、`[ClearUnorderedAccessViewFloat]` | UAV 清理前后命令 |
| `[Loader]`、`[System]`、`[Device]`、`[Rendering]`、`[Input]`、`[Logging]`、`[Hunting]` | 全局设置 |

同类型自定义节用后缀区分，例如 `[ResourceBody]`、`[ResourceHair]`。不要重用同一名称表示不同对象。

## 命名空间

递归加载的 Mod 会隔离自定义名称。需要稳定的跨 Mod 接口时，在文件节之前显式命名：

```ini
namespace = Example

[Constants]
global $enabled = 1

[CommandListUpdate]
$enabled = 1
```

其他命名空间中使用：

```ini
run = CommandList\Example\Update
```

`[Constants]`、`[Present]` 等固定节仍是共享入口；其中的命名变量保留各自作用域。不要把全局系统配置放进每个 Mod。

## 重载边界

F10 重新读取 INI、Mod 资源与补丁；持久值随后覆盖默认值。DLL、设备创建及部分系统设置需要重启游戏。
