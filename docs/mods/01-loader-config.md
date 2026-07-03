# [Loader] — 注入加载配置

配置 DLL 注入行为和启动参数。

## Keys

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `target` | 字符串 | *(空)* | 目标进程名/匹配模式 |
| `module` | 字符串 | *(空)* | 要注入的 DLL 路径 |
| `entry_point` | 字符串 | `CBTProc` | DLL 入口函数名 |
| `launch` | 字符串 | *(空)* | 要启动的可执行文件 |
| `working_dir` | 字符串 | *(空)* | 启动进程的工作目录（别名: `working_directory`） |
| `check_version` | bool | `true` | 是否检查 DLL 版本兼容性 |
| `require_admin` | bool | `false` | 是否要求管理员权限 |
| `wait_for_target` | bool | `true` | 是否等待目标进程启动 |
| `hook_proc` | int | `5` | Hook 过程类型 |
| `delay` | int | `0` | 注入前延迟（毫秒） |

## 案例

### 基础注入配置

```ini
[Loader]
target = game.exe
module = d3d12.dll
entry_point = CBTProc
launch = C:\Games\MyGame\game.exe
working_dir = C:\Games\MyGame
check_version = true
require_admin = false
wait_for_target = true
delay = 0
```

### 等待进程 + 管理员权限

```ini
[Loader]
target = game.exe
module = d3d12.dll
require_admin = true
wait_for_target = true
hook_proc = 5
```
