# DX11 表达式后续四提交审查

只读审查，未修改生产代码、CMake、main 或账本，未运行构建。本文件是在完整读取四提交 diff 后，继续跟踪本地真实调用和写入路径的适配建议，不代表已验证实现。

## 帧号与显式快照

`d654cf76db8d2857ae6b994296f9386ac45bcf6d` 在 CommandList.cpp/h 增加 FRAME_NUMBER 枚举、名称、evaluate 与 operand_allowed_in_context 分支。没有新文件或构建标准变化。值来自 `G->frame_no`；HackerDXGI.cpp 在 RunFrameActions 中自增，与已有全局状态一致。转 float 之后大于 2^24 的连续帧号不再可精确区分，属于当前表达式数值模型，不能报告为精确32位计数器。

`6a77c5acc70853eaaf8b358e09da6bae96c7d676` 依赖 d654 与 76e，给 TIME/FRAME_NUMBER 的 static_evaluate 增加 evaluate_variables=true 分支。沿用本地 ret=nullptr 保护，默认 false 必须继续拒绝，以免普通优化冻结帧号/时间。

建议在真实 CommandListExpression 上测试 `frame_number + time`：设置两个 G 值，默认 static_evaluate false，显式 true 返回和，nullptr 探测成功；optimise 后更新 G 值，evaluate 必须变化。也测试无 scope 的配置解析快照、frame_no=0/UINT_MAX、时间为0/有限小数，结束恢复两个字段。不要以32位回绕前后float差1作为断言。

## LOCKED 的实际写入边界

`db64f5b188c59be830a2414c07f897fdc296ce18` 仅修改 VariableFlags/名称及 ParseCommandListVariableAssignment 的写入检查。它并没有在变量对象上建立不可变存储。完整移植这条 diff 之后，下面这些已存在的生产入口仍能改变锁定变量：

| 入口 | 已读证据 | 适配建议 |
| --- | --- | --- |
| 普通 `$var = expression` | CommandList.cpp `ParseCommandListVariableAssignment`，`VariableAssignment::run` | 上游检查插在解析拿到var之后、分配command之前；应保留，失败不能把命令入队。可以给run再加防御，但不以它取代解析诊断。 |
| `store = $var, source, offset` | `ParseStoreCommand` 解析第一个参数直接写 `operation->var`；`StoreCommand::run` 回读后 `var->fval = ...` | 锁定检查应在ParseStoreCommand解析目标后，GPU拷贝之前；运行入口也可防御。不得为了测试锁定而进入当前StoreCommand其它未在本任务修复的非法资源路径。 |
| Key/Preset直接变量覆写 | Override.cpp `Override::ParseIniSection`：查var后写入mOverrideVars；`OverrideTransition::ScheduleTransition` 调度；`UpdateTransitions` 写j->first->fval | 在Override::ParseIniSection拒绝locked目标并诊断，读变量条件仍允许。即时和渐变两模式均覆盖。单修普通命令解析不覆盖此路径。 |
| Key cycle目标 | Override.cpp `KeyOverrideCycle::ParseIniSection`：查var后GetIniString到var_bufs；后续构造KeyOverride | 同样在目标收集时拒绝locked，避免进入cycle内每一档。可在ScheduleTransition防御性忽略locked，保证手工构造Override对象也不写入。 |
| 池变量赋值、批量赋值 | `PoolVariableOperation::SetVariableValue` 直接dst->fval；`SetAllPoolVariables` 经GetVariable获得全部元素 | 当前pool模板只设置PERSIST/NONE，没有INI locked池选项；常规池配置不能产生locked变量，因此不是现成INI绕过。若支持把普通locked变量作为池元素/未来模板加LOCKED，则统一SetVariableValue检查。不要无依据宣称现有Pool就是可绕过普通locked全局的入口。 |
| 初始化、类型切换、过期重置 | `InitializeVariable` 通过RegisterGlobalVariable后设置模板值；`ResetVariable` 写模板值；CopyMetadataFrom通过CopyStateFrom | 属于对象初始化/内部生命周期，不是用户赋值。不要把所有fval写入机械禁止，否则会阻断池重用或声明初始化。明确区分授权初始化与运行期写入。 |
| 持久化恢复 | IniHandler.cpp SavePersistentSettings 保存每个PERSIST变量；HackerContext::InitIniParams运行加载配置的Constants命令恢复 | `global persist locked $x=...` 的组合上游允许声明，但生成的user_config `$x = saved` 会被普通LOCKED检查拒绝；应明确策略。最低兼容策略保留上游行为并记录该组合无法恢复改变后的值；不静默把persist恢复当作可任意修改constant的豁免。 |

最小充分同步可先实现上游明确承诺的普通赋值拒绝，账本说明并测试其边界；若向用户报告“实际不可变常量”，则至少要同步处理 Store/Key/Preset/Cycle 的真实写入口。这些修复与任务直接相关，但不需要改C++标准或重构全体变量存储。

回归应使用真实解析器和生产命令：locked初始化值可读取/可作静态配置表达式；普通赋值被拒绝且command数量不变；普通未锁变量继续赋值；store locked目标解析失败；Override/Preset/Cycle不收locked目标；未锁即时/渐变行为保留；persist+locked加载及重载行为有明确断言。创建可运行StoreCommand的测试应使用有效WARP buffer，并确认没有GPU拷贝发生于拒绝路径。测试写G和全局registry后应恢复状态，避免影响后续真实LoadConfig fixture。

## 一元函数的真实实现与问题

`057b4bce3fe813ffdb1414f4896a5a5b6d4f781a` 修改 CommandList.cpp、HackerDXGI.cpp、globals.h、util.cpp/h。已实现函数工厂是countbits、sin/cos/tan/asin/acos/atan、abs/sign/ceil/floor/trunc/round/frac、sqrt/rsqrt、exp/exp2/log/log2、saturate、random。

**noise没有实现。** 它只进入function_tokens，没有DEFINE_OPERATOR或unary_operators条目。当前应让noise(...)被正常拒绝，并在账本明确上游该提交的实际能力；不要因提交说明声称noise可用，也不要自行发明算法冒充上游。

函数语法要求名称后立即接 `(`，例如 `sin(0)`，`sin (0)`不被识别。函数使用一元运算符归约，需要验证嵌套调用与括号归组；不能把原有无括号变量名/资源名误识别为函数。

### random 不应成为常量折叠

上游 `DEFINE_OPERATOR(random_operator, "random", random(rhs))` 继承通用 CommandListOperator::static_evaluate，只检查参数可否静态求值。因此 `random(5)` 在正常optimise时会被折叠为一个常量，以后的调用不再随机。这是确定的调用链问题，不是概率测试。

建议专用random operator保留工厂和名称：默认static_evaluate返回false；显式evaluate_variables=true是否允许快照需要明确与TIME/FRAME_NUMBER一致的策略。若允许，应只在caller明确请求时消耗一次随机序列。运行evaluate每次才调用随机函数。其C++类型放文件作用域，不在函数内定义。

`random_call_counter++` 是共享非原子写。RunCommandList入口本身没有统一锁，因此不能仅依据部分HackerContext路径加G锁，就声称所有随机调用串行。最小修复用std::atomic<uint32_t>::fetch_add(1, memory_order_relaxed)获得唯一序号；多线程执行顺序本来不确定，不承诺跨调度可复现。G->frame_no和gSystemTickCount仍是普通字段：若该helper允许不持G锁并发读取，应证明调用串行约定或使用明确的同步快照。仅counter原子不能修复其它状态的数据竞争，也不要为random悄悄升级全体G状态。

上游把24位hash转成 `[0,1)` float，再乘abs(max)。它保证实数上界，但浮点舍入能返回max，最直接负例是最小正subnormal：大量大于0.5的比例乘它会舍入回同一个subnormal，而契约只允许零。有限max的输出应clamp到 `nextafter(abs(max), 0.0f)` 再恢复符号。零、负零、NaN/inf行为需明确：上游±0都返回+0；inf乘0会产生NaN，其余结果inf；NaN传播。可以保留这些非有限输入的现有结果并准确记录，不能报告它们满足有限区间。对有限max保持正 `[0,max)`、负 `(max,0]`。

若测试随机确定性，最好把纯hash/给定seed计算抽为文件级小helper供真实实现调用；不复制一份算法到测试并仅比较两份复制品。现有实现的counter不可重置，实际命令连续调用只适合测范围及没有被折叠，而不应假设任意两次结果必不同（hash截断允许碰撞）。可断言含random的表达式默认不能static_evaluate，optimise之后仍不能，而不是只凭两次结果不等证明随机性。

### countbits、特殊浮点与数学边界

上游countbits先 `(uint32_t)rhs` 再手写popcount。有限非负可表示范围内是数值转整后统计位数，不是把IEEE float bitcast后计数。负数、NaN、inf和超出UINT32_MAX（float附近最大安全数是4294967040）直接转换无可移植保证。建议文件级转换helper：先拒绝/处理非有限，再明确截断和32位wrap或饱和策略；若选择32位wrap应在double中trunc/fmod，再转unsigned，避免UB，负数转二补码的选择需要说明。不要为C++20 std::popcount升级DX11；现有清最低置位循环兼容C++17。

saturate上游使用 `max(0.0, min(rhs,1.0))`；NaN行为取决于具体min/max实现/宏，不自动等同HLSL承诺。math函数应保留IEEE域结果：sqrt负数、log0/负数、asin/acos越域、rsqrt0；不要为通过回归改成吞错的零。round的C++语义是半值远离零，需与上游保持而不要无依据改成round-to-even。

建议回归：所有工厂至少一正常例；sin(0)/cos(0)、frac(-1.25)=0.75、round(±0.5)、saturate边界、sqrt(-1)/rsqrt(0)/log(0)；嵌套 `abs(-sqrt(9))`，缺参数/多参数/无闭括号拒绝；countbits(0/1/7/16777215/最大安全unsigned float)及选定非有限策略；random正/负/零/最小subnormal/最大finite范围、默认优化不折叠、显式snapshot策略、多线程counter覆盖。非有限和浮点下溢测试必须使用真实生产路径。

## 共享模块隔离与构建

本地 `src/DirectX11/CMakeLists.txt` 已编译src/common/util.cpp；`src/DirectX12/CMakeLists.txt` 当前未编译它，因此不能声称直接照搬会立即破坏当前DX12构建。仍不宜把DX11独有G->frame_no/gSystemTickCount依赖裸加进公共util：可将random和popcount helper放DX11 CommandList.cpp文件作用域，无新增CMake；或在src/common util声明/定义加MIGOTO_DX==11保护。不要改全局C++标准或DX12头来迎合DX11 helper。

gSystemTickCount新增初始化值应为0，HackerDXGI::RunFrameActions每次先保存GetTickCount再据它更新gTime，维持旧DWORD tick回绕语义。验证函数组后执行DirectX11生产和真实回归target构建；只有实际修改共享模块/头影响其它目标时，再做对应DX12集成检查。此审查没有执行上述验证。
