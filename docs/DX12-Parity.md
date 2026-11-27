# DX12 功能对齐进度

对照起点为本仓库 DX11 的 `0ce23af`。以 Mod 实际能执行的操作核对兼容性，配置解析、运行时执行、重载和失败回退都属于同一项工作。独立于图形 API 的规则放在 `src/common`，资源绑定、命令提交和同步由各后端实现。

| 功能范围 | 当前已接入 | 待对齐 |
| --- | --- | --- |
| ShaderRegex | 无 Pattern 的模型匹配，逐组排除着色器 Hash（`66848dd`） | Pattern、Replace、声明插入、临时寄存器、filter_index |
| 表达式、变量和命名空间 | 基本数值和分支，二进制字面量、幂、float32 位模式比较（`8ea57d6`），21 个共享一元数学函数（`d82316d`），6 个共享位运算与移位运算符（`3ac9c70`），有界随机数 | locked、命名空间隔离、持久化、基础算术精度及逻辑短路语义 |
| 命令列表和触发时机 | 基本 run、阶段链接 | 回调和代理、abort、每帧限制、资源命令的 pre/post、清理事件 |
| 运行时参数和输入 | 窗口、光标、基本 Key 和持久化 | 逐绘制上下文、帧计数、IniParams 绑定、Preset、输入后端、过渡和暂停 |
| 资源与匹配 | 基本文件资源、别名、固定槽捕获、Hash 和绘制条件 | 通用复制、创建选项、资源池、区域、动态槽、范围、资源和着色器 getter、描述匹配、dHash Filter、贴图快照、store |
| 图形与计算 | IB/VB 和 SRV 替换、图形自定义元素、部分计算蒙皮、二进制着色器替换、显式实例绘制（`73f4e70`）、从当前 VB0/IB 自动推导绘制数量 | 独立自定义 CS、各阶段替换、图形状态合并、输入布局、流输出、同一命令列表逐绘制切换 IA 绑定、间接命令、Present 独立元素的索引绘制、文本补丁 |
| 分析与设备配置 | Hunting、F8/F9、性能 HUD、DX12 日志 | 定点 dump、按 draw 配置分析、MCP、其他 Loader/Device/Rendering/Logging 设置 |

每轮用生产入口做功能回归，共享逻辑同时跑 DX11 回归。驱动特有能力按具体效果评估。WARP 或离线配置回归覆盖的阶段与实机游戏复测分别记录。

## 已执行的回归

| 范围 | 验证入口 |
| --- | --- |
| ShaderRegex 排除规则 | DX11 的 `dx11_upstream_regression --shader-regex-exclusions`，DX12 的 `dx12_runtime_audit`。后者读入 INI、调用运行时组匹配，并重新加载配置 |
| 数字字面量与运算符 | DX11 的 `dx11_upstream_regression --audit-item P-02`，DX12 的 `dx12_runtime_audit`。后者执行 `[Constants]` 并覆盖结合顺序、负零、最大长度和非法输入 |
| 位运算与移位 | 抽取前的 DX11 求值器、抽取后的 DX11 P-02 及 DX12 运行时执行相同的 24 组表达式，包括负数右移、移位数边界、非有限值和连续运算的 float32 舍入 |
| 一元数学函数 | 两端执行 `src/common/tests/IniUnaryMathCases.h` 中同一组用例。DX11 的 `dx11_upstream_regression` 同时检查静态求值和优化，DX12 的 `dx12_runtime_audit` 同时检查变量、嵌套调用和非法调用 |
| 有界随机数 | DX11 主回归覆盖极小边界、并发采样和换种。DX12 的 `dx12_runtime_audit` 检查 Constants、嵌套顺序、非法表达式、非有限边界、按帧换种、WARP 交换链的 Present 入口和采样次数 |
| 显式实例绘制 | DX11 的 `dx11_upstream_regression` 覆盖共用的参数分割入口。DX12 的 `dx12_runtime_audit` 从 INI 经静态计划和动态执行到原生绘制回调核对参数，包括负基顶点、零实例、越界和非法表达式 |
| 自动绘制数量 | DX11 的 `dx11_upstream_regression --audit-item F-155` 核对 WARP 上的偏移、步长和空绑定。DX12 的 `dx12_runtime_audit` 经命令列表包装器记录真实缓冲视图，在绘制回调处核对原绑定、替换绑定及失效后的数量和实例范围 |

以上脚本语义已通过本地回归，游戏内 Mod 的组合行为仍需目标环境复测。
