# BetterCallDialyn V2.0 语法支持排查清单（对照 ProjectBunny DX12 运行时）

- 审查对象：`C:\Users\Administrator\Desktop\BetterCallDialynV2.0\Dialyn.ini`（XXMI v1.6.3 生成，439 行）
- 对照实现（DX12）：
  - 解析层：`src/DirectX12/Config/DX12Config.h`、`src/DirectX12/Config/DX12ConfigLoader.h`
  - 表达式/变量 VM：`src/common/MigotoCommandVmUtils.hpp`、`src/common/MigotoIniCommandListUtils.hpp`
  - 热键：`src/common/MigotoKeyBindingUtils.hpp`
  - 执行层：`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp`、`DX12CommandListExecutor.h/.cpp`、`DX12TextureOverrideMatching.cpp`、`DX12LoadedResources.cpp`、`DX12DrawSrvReplacementUtils.h`、`DX12DdsTextureLoadUtils.h`
  - DX11（SpectrumQT/XXMI fork）作为 3Dmigoto 标准语义的参考：`src/DirectX11/*`

图例：✅ 已支持（有真实实现）｜⚠️ 部分支持（语法可解析，但语义受限或近似）｜❌ 未支持（解析被忽略/执行 no-op/无对应机制）｜🔶 语法本身支持，但依赖外部提供的内容（如 XXMI 库 ini），缺了则静默无效

证据位置缩写：CFG=`DX12Config.h` ｜ LOD=`DX12ModRuntimeLoad.cpp` ｜ EXH=`DX12CommandListExecutor.h` ｜ EXC=`DX12CommandListExecutor.cpp` ｜ INT=`DX12ModRuntimeInternal.h` ｜ IAR=`Hooks/DX12IaReplacement.h` ｜ DSU=`DX12DrawSrvReplacementUtils.h` ｜ PSR=`DX12PreSkinRuntime.cpp` ｜ MTM=`DX12TextureOverrideMatching.cpp`｜ VM=`common/MigotoCommandVmUtils.hpp`（均在 `src/DirectX12/…` 或 `src/common/…` 下，行号已核对）

---

## 一、文件 / 基本结构层

| # | 特性 | Dialyn.ini 位置 | 结论 | 证据 |
|---|------|------------------|------|------|
| 1 | 分号 `;` 注释、空行 | 全文（如 L1–3） | ✅ | DX12Config.h:109-113 `IsCommentOrEmpty` |
| 2 | `#` 注释 | —（未用） | ✅ | 同上 |
| 3 | 同名节多次出现自动合并（两个 [Constants]/[Present]） | L4/L84、L16/L88 | ✅ | DX12Config.h:305-318 AppendParse 合并到同一 section |
| 4 | UTF-8（含 BOM）/ ANSI 编码 | 文件头 | ✅ | DX12Config.h:215-262 |

## 二、[Constants] 段与全局变量

| # | 特性 | 位置 | 结论 | 证据 |
|---|------|------|------|------|
| 5 | `global persist $Top = 2` 声明并初始化 | L5–14 | ✅ 加载时执行 | DX12ModRuntimeInternal.h:2479-2534 `DX12ConstantsVmInitializerUtils::Initialize`（load/reload 时 ClearGlobals 后按序赋值）；变量名前缀解析 MigotoCommandVmUtils.hpp:416-428 |
| 6 | `global persist` 的 **persist（重载保留）语义** | L5–14 | ✅ 进程内重载保留（本次补齐）：重载前把顶层 persist 声明变量的当前值快照，ClearGlobals 与 [Constants] 重放后写回（跨进程/重启持久化未做，见第十二节） | MigotoCommandVmUtils.hpp:462-489（Save/RestorePersistSnapshot）；DX12ModRuntimeInternal.h:2479-2523 |
| 7 | `global $active = 0`（非 persist） | L84–86 | ✅ | 同上 |
| 8 | [Constants] 内 if/赋值流程 | —（Dialyn 未用 if） | ✅ | Initialize 内 ApplyAction 支持 ControlFlow/Assign |
| 9 | 表达式初始化（`$x = 1+1` 之类） | —（未用） | ✅ | State::Assign → Evaluate（MigotoCommandVmUtils.hpp:291-297） |

## 三、[Present] 段（每帧 Present 触发）

| # | 特性 | 位置 | 结论 | 证据 |
|---|------|------|------|------|
| 10 | [Present] 节每帧（每次 Present）执行一次 | L16-17、L88-90 | ✅ | DX12CommandListExecutor.cpp:154-187 `DX12ModPreparePresentReplacement` → RunNamedCommandList("Present")，按 Present 计数去重 |
| 11 | `post $active = 0`（后置变量赋值，命令列表结束时写入） | L17、L89 | ✅ | ParseActionFromEntry → PostVariableAssignment（DX12Config.h:848-851）；执行/延迟刷新：DX12CommandListExecutor.h:1465-1469 + `CommandVmRunScope` 析构 FlushPostAssignments（70-97 行） |
| 12 | `run = CommandListCreditInfo`（Present 里运行命令列表） | L90 | ✅（目标节必须存在） | Run 动作 DX12Config.h:797-801；DX12CommandListExecutor.h:1310-1316 |

## 四、[Key…] 热键段（Dialyn 用 type=cycle 的服装切换）

| # | 特性 | 位置 | 结论 | 证据 |
|---|------|------|------|------|
| 13 | `key = no_alt no_shift no_CTRL 7` 修饰符组合 | L20 等全部 | ✅（不区分大小写） | MigotoKeyBindingUtils.hpp:92-96, 200-266（ctrl/shift/alt/control + no_* 前缀） |
| 14 | 数字/字母主键（7、8、9…） | L20/26/33… | ✅ | 单字符映射 ParseVirtualKey（267-300 行） |
| 15 | `VK_UP/VK_DOWN/VK_LEFT/VK_RIGHT` 等"名字型"虚拟键 | L45、L58-59 等 | ✅ | 剥掉 vk_ 后匹配 up/down/left/right/…（270-344 行） |
| 16 | `VK_OEM_PERIOD`、`VK_OEM_2` 等 OEM 命名键 | L33（KeyEarrings）、L39（KeyTelephone） | ✅ **本次补齐**：DX12 键名解析改用共享键表（common/vkeys.h 的 VKMappings/ParseVKey，与 DX11 同一张表），覆盖全部 VK_* 名（OEM_1..OEM_8/OEM_102）、0x 十六进制、F1-F24、媒体键、小键盘等 | vkeys.h:7-169；MigotoKeyBindingUtils.hpp:294-307（ParseVirtualKey → ParseVKey；独立编译测试全过） |
| 17 | `back = …`（反向循环键） | L27、L52、L59、L72、L79 | ✅ **本次补齐**：type=cycle 的节现在同时监听 `key=`（前进）与 `back=`（后退）组合，wrap+按当前值重新定位后反向步进（与 DX11 默认 wrap/smart 一致；DX11 也只在 cycle 注册 back） | Binding.back/hasBack/wasBackDown（MigotoKeyBindingUtils.hpp:57-66）；ProcessBindings 149-191；ActivateCycle 方向参数 368-397 |
| 18 | `condition = $active == 1` | L21 等全部 | ✅ | ConditionActive → EvaluateExpression（374-381 行） |
| 19 | `type = cycle` + `$HairA = 0,1`（值表循环、按当前值定位后步进） | L19-82 全部 | ✅ | BindingType::Cycle（138-142 行）；ApplyVariables/FindCurrentCycleIndex（406-491 行） |
| 20 | cycle 值支持表达式（如数值运算） | —（未用） | ✅ | ApplyVariables 用 EvaluateExpression（445-456 行） |
| 21 | 每帧轮询输入（含修饰键实时状态） | — | ✅ | ProcessDx12KeyBindingsForFrame（DX12CommandListExecutor.cpp:625-639）；IsCombinationDown 用 GetAsyncKeyState |

> ✅ **热键现状（本次补齐后）**：Dialyn 的 10 组热键全部可解析；`VK_OEM_PERIOD`/`VK_OEM_2`（KeyEarrings/KeyTelephone）与 8 组 `back=` 反向功能均可用。

## 五、[TextureOverride…] 节（核心）

| # | 特性 | 位置 | 结论 | 证据 |
|---|------|------|------|------|
| 22 | `hash = 339f41eb`（32 位十六进制） | 全部 | ✅ | DX12Config.h:1287-1314 |
| 23 | `handling = skip`（吞掉原绘制） | L96、L115 等 7 处 | ✅ | config.handlingSkip + HandlingSkip action（DX12Config.h:1473-1484）；运行时 SetSkip → replacement->skip（EXH:1323-1326/1405-1408）。消费规则（DX12IaReplacement.h:1162-1259）：skip 且本节未追加任何 draw/dispatch/SRV patch → 原 draw 被吞掉什么都不画；skip + 有 drawindexed → 只画新 draw；无 skip + 有新 draw → 原 draw 与新 draw 都画（Dialyn 正是 skip 节 + drawindexed 节成对使用） |
| 24 | `ib = Resource…`（替换索引缓冲） | L158、L208 等 | ✅ | DX12Config.h:1486-1495；执行 DX12CommandListExecutor.h:1327-1330 + ApplyIaStateResourceOverrides（385-459 行）；另有"纯几何替换"静态表快路径 DX12ModRuntimeInternal.h:1773-1862 |
| 25 | `vb0/vb1/vb2 = Resource…`（替换顶点缓冲槽，含 if 块内使用） | L99、L106、L126 等 | ✅ | ParseVertexBufferKey（DX12Config.h:1410-1424）；执行同 ib（SetVertexBuffer 1331-1336 行） |
| 26 | `draw = 23993, 0` | L100、L139 | ✅ | DX12Config.h:813-820；AppendDraw（333-353 行） |
| 27 | `drawindexed = 6624, 0, 0`（3 参：count/startIndex/baseVertex） | 全文多处 | ✅ | DX12Config.h:822-835；AppendDraw |
| 28 | `drawindexed = auto` / `from_caller` | —（未用） | ✅ | 同上（829-832、814-816/823-825 行） |
| 29 | `ps-t9 = Resource…`（替换像素着色器 t9 槽） | L225 | ✅ 真实替换绘制中的 SRV：TO 匹配执行末尾（EXH:621-631）→ DrawSrvReplacementUtils ApplyConfig/ApplyCompiledPlan（DSU:1715-1840）→ 按槽位匹配当前绑定并生成 drawSrvPatches（DSU:939-1069）→ 画 draw 前经镜像/根表改写注入（DX12DrawSrvReplacement.cpp:7-21、DSU:1326+），随后逻辑/物理恢复（DSU:1842-1903） | 解析 DX12Config.h:1599-1604 |
| 30 | `vs-tN` | —（未用） | ✅ | 同 29 |
| 31 | `this = Resource…`（整体替换本绑定 SRV） | —（未用） | ✅ | drawReplaceThisResource（DX12Config.h:1612-1616）；执行依赖 match.hasThisSrvTarget（DSU:1007-1030），失败仅日志 stage=slot_patch_fail |
| 32 | `cs-tN` / `srvN` / `match_cs_tN_hash` / `match_srvN_hash`（pre-skin 专用） | —（未用） | ✅（仅 pre-skin compute 替换路径；普通 draw 阶段不适用） | DX12Config.h:1572-1597；pre-skin 应用 DX12PreSkinRuntime.cpp:3047-3092/3137-3149/3748+ |
| 33 | `Resource\ZZMI\Diffuse = ref ResourceDialynHairDiffuse` 等（键名以 Resource 开头的绑定 + ref 值） | L159-162、L175-178、L210-224 等 | ❌ **解析为 ResourceAssignment 动作，但执行路径（编译 op 表 9 类与动态解释两条）均无处理 = no-op**；`ref ` 值前缀 / 资源别名重定向在 DX12 侧完全未实现 | 解析：IsResourceAssignmentKey（MigotoIniCommandListUtils.hpp:139-142）→ CFG:864-868；编译 default→unsupported（LOD:1865-1866）；动态执行落空 break（EXH:1362-1366、1444-1448）。INT:1789-1794 仅将 ResourceAssignment 视为不破坏"静态几何表"资格的动作类型，并非执行它 |
| 34 | `run = CommandList\ZZMI\SetTextures` | L163、L179、L226、L294 | 🔶 语法 ✅：run 是节 actions 里的内联 Run 动作，按书写顺序（受 if/Active 约束）执行（CFG:797-801；EXH:1310-1316/1390-1398；编译期会内联目标节计划 LOD:2041-2052/2142-2151，缺目标则整节标 unsupported 回退动态）。但目标节在 ZZMI 库 ini 中；**库未被 include 时查无此节 → 静默跳过**（SnapshotCommandListConfig → null，INT:3916-3925；EXH:1311-1315），无任何报错/警告 | |
| 35 | `match_first_index = 0`（以及 match_vertex_count/index_count/instance_count/first_vertex/first_instance） | L157、L173、L207 等 | ✅ 真实生效 | 解析 NumericMatch（DX12Config.h:1352-1390）；运行时按 draw 参数过滤（DX12TextureOverrideMatching.cpp:30-46 `TextureOverrideMatchesDrawContext`，缓存键含 firstIndex） |
| 36 | `match_priority` | —（未用） | ✅ | DX12Config.h:1545-1552，bucket 排序 1669-1676 |
| 37 | `override_vertex_count` / `override_byte_stride` / `uav_byte_stride`（顶点上限提升 VLR） | L110-111、L130-131、L148-149 | ✅ 有完整执行链，但作用于 **pre-skin/VLR 管线**（游戏 CS 输出缓冲扩容：建资源时放大 desc、UAV view 放大、IA 视图重定向、dispatch/CBV 顶点数补丁），并非通用地把 draw 顶点数改写 | 解析 DX12Config.h:1618-1660；资源分配放大 DX12ModRuntimeLoad.cpp:348-394（挂 Create\*Resource）；UAV/IA/dispatch 链 DX12PreSkinRuntime.cpp:2259-2317/2216-2256、DX12IaReplacement.h:293-584 |
| 38 | `if / else if / endif`（含嵌套 if、含分支内赋值与绘制） | L98-101、L117-122、L165-169、L230-269 等 | ✅ | ControlFlow 逐条解析；运行期 ApplyControlFlow（VM MigotoCommandVmUtils.hpp:333-368）+ ApplyVmAction（DX12CommandListExecutor.h:1453-1483） |
| 39 | 表达式：`==` `!=` `>` `<` `>=` `<=` `&&` `||` `!` `()` 四则 `+ - * / // %` | L21、L98、L165 等 | ✅ | 表达式递归下降解析器 MigotoCommandVmUtils.hpp:40-230 |
| 40 | 表达式内变量 `$HairA`、`$active`（大小写不敏感） | 全部 if 行 | ✅ | 名称小写化统一（NormalizeVariableName），写读都保留 `$` 前缀一致 |
| 41 | `DRAW_TYPE`（绘制类型内置变量：mod 用 `DRAW_TYPE == 1` 判断 drawindexed） | L98、L117、L137 | ⚠️ **恒等于 1.0**，不随当前绘制类型变化 | LookupVariable 中 `draw_type` 直接返回 1.0（MigotoCommandVmUtils.hpp:446-447）。DX11 才按 DrawType 枚举注入（DirectX11/DrawCallInfo.h）。对 3Dmigoto 语义而言 DrawIndexed=1，故只有原绘制确实是 indexed 时语义才等价；该近似需谨慎 |
| 42 | `time` 内置变量 | —（未用，Dialyn 的 L306 用作赋值 RHS） | ✅ | steady_clock 秒（MigotoCommandVmUtils.hpp:441-445） |
| 43 | 分支内 `$active = 1`（节执行时设置全局） | L105、L121 | ✅ | VariableAssignment；Executor ApplyVmAction 1453-1464 |
| 44 | 同一 hash 多节共存（如 IB skip 节 + 替换节 + 匹配段节） | L151-199、L201-280 | ✅ 全部命中候选都会执行（按 match_priority、节名排序后逐个） | DX12TextureOverrideMatching.cpp:624-644（收集所有匹配项）；排序 DX12Config.h:1669-1676 |
| 45 | 每节多条 drawindexed 顺序叠加（多条替换绘制） | L227-228 等 | ✅ | draws 累积进 replacement（DX12CommandListExecutor.h:646-653/919-922） |
| 46 | 注释夹在动作序列中 | L164、L186 等 | ✅ | 解析器忽略注释行 |

## 六、[CommandList…] 命令列表

| # | 特性 | 位置 | 结论 | 证据 |
|---|------|------|------|------|
| 47 | 命令列表内 if/赋值/endif | L302-308 | ✅ | 同 #38/43 |
| 48 | 命令列表内嵌套 `run =` | —（未用） | ✅ | Run 动作递归执行（深度上限 16） |
| 49 | `pre run = CustomShader\ShaderFixes\help.ini\FormatText`（节内 pre-链接键） | L305 | ❌ **实际不生效**：语法解析 OK（键"pre run"→ 该节自己的 config.commandLists.pre），但运行期只有 [TextureOverride]/[ShaderOverride] 的 pre 链接会被执行器主动迭代（EXH:603-609/250）；[CommandList] 节被 run= 调用时只执行其 actions，没有宿主去跑它自己的 pre 链接。且目标 FormatText 属 XXMI 库（见 #34） | ParseLinksFromEntry（DX12Config.h:908-927）；RunCommandList 只执行 actions（DX12CommandListExecutor.h:1290-1370） |
| 50 | `pre Resource\ShaderFixes\help.ini\Notification = ResourceCreditInfo`（单行 pre 前缀资源命令） | L304 | ❌ 整行解析失败被丢弃（"pre run" 是唯一被识别的 pre 拼写；`pre Resource…=` 不匹配任何 action/链接） | ParseActionFromEntry 与 ParseLinksFromEntry 均不命中（DX12Config.h:788-927） |
| 51 | `pre $\ShaderFixes\help.ini\notification_timeout = time + 5.0` | L306 | ⚠️ 解析为普通变量赋值（键以 `$` 开头），会把整串 `$\shaderfixes\…` 当作全局变量名写入；DX12 没有 DX11 那种跨 ini 作用域变量/通知系统，**无实际效果** | IsVariableAssignmentKey 只看首字符 `$`（MigotoIniCommandListUtils.hpp:116-120）；ExtractVariableName（MigotoCommandVmUtils.hpp:416-428） |
| 52 | `$creditinfo = 1`（防重放标志） | L307 | ✅ | 变量赋值 |

## 七、[Resource…] 资源节

| # | 特性 | 位置 | 结论 | 证据 |
|---|------|------|------|------|
| 53 | `type = Buffer` + `stride` + `filename = *.buf`（裸数据上传 GPU） | L312-375 | ✅ | ResourceConfigLooksLikeBuffer（DX12ModRuntimeLoad.cpp:1661-1674）；上传 DX12LoadedResources.cpp:131-183 |
| 54 | `format = DXGI_FORMAT_R32_UINT`（IB 格式） | L329、L354 等 | ✅ | ParseDx12ResourceFormat（DX12ModRuntimeLoad.cpp:5-21，支持 R16/R32 及数值/全名） |
| 55 | filename 相对 ini 目录解析，失败回退 root | 全部 | ✅ | ResolveResourcePath/fallback（DX12ModRuntimeLoad.cpp:1634-1651、1685-1707） |
| 56 | 无 `type=` 的纹理资源 + `*.dds` 文件（Dialyn 纹理节均未写 type） | L377-432 | ✅ 按 `.dds` 扩展名识别为纹理 | ResourceConfigLooksLikeTexture（DX12DdsTextureLoadUtils.h:99-105） |
| 57 | BC 压缩 DDS（BC1–BC7、DXT1/3/5 legacy fourcc）+ 全 mip 链 | Textures/*.dds | ✅（边界：array/cubemap/3D 拒绝；type 缺省的 .dds 会先被 raw-buffer 判定建一个浪费的 UPLOAD 空缓冲） | DX12DdsTextureLoadUtils.h:107-150/237-306/342-364/374-403 |
| 58 | `data = "Created by MajorUzumaki"`（字符串 Buffer） | L434-436 | ❌ **只支持十六进制字节串**；带引号/非 hex 文本解析失败 → 资源无法生成（DX11 中引号字符串用于帮助/通知文本，DX12 无此机制） | LoadResourceBytes data 分支（DX12ModRuntimeLoad.cpp:1709-1728） |

## 八、跨文件引用（XXMI 部署约定）

| # | 特性 | 位置 | 结论 | 证据 |
|---|------|------|------|------|
| 59 | include/include_recursive/exclude_recursive | —（Dialyn 由宿主 d3dx.ini include） | ✅ | DX12ConfigLoader.h:192-299 |
| 60 | 引用外部 ini 中的节：`CommandList\ZZMI\SetTextures`、`CustomShader\ShaderFixes\help.ini\FormatText`、`Resource\ShaderFixes\help.ini\Notification` | L163 等、L305、L304 | 🔶 名字查找式解析、**加载期零校验**：include 树只把被点名 ini 按"相对主 ini 目录路径"打命名空间并入全局节表（DX12ConfigLoader.h:166-179、214-299）；引用缺 key 时编译期 plan.unsupported → 运行时静默 no-op（原 draw 照常），[Resource] 缺失才会在加载时弹一次警告（DX12ModRuntimeLoad.cpp:1902-2007/2117-2193；DX12LoadedResources.cpp:93-103/306-327）。DX12 不隐式加载 ShaderFixes/ZZMI | ResolveNamespacedSectionReference（DX12Config.h:183-213） |
| 61 | 运行时对缺失引用是否有诊断 | — | ❌ 无（排错只能看行为/日志字段） | 同上 |

## 九、Reload / 状态保持

| # | 特性 | 结论 | 证据 |
|---|------|------|------|
| 62 | F10 风格显式 reload | ✅（仅显式触发，无文件监听/自动 watch；状态条文案见 DX12ModRuntimeLoad.cpp:1586-1592） | DX12Input.h:12-43；DX12ModRuntimeLoad.cpp:1605-1630 |
| 63 | reload 后全局变量保留（persist 语义） | ❌ 每次 reload ClearGlobals + 只重放 [Constants]；运行期写入（含热键 toggle 状态）全丢 | DX12ModRuntimeInternal.h:2482-2494；MigotoCommandVmUtils.hpp:482-490 |

## 十、其它运行时机制

| # | 特性 | 结论 | 证据 |
|---|------|------|------|
| 64 | 重载 JSON 状态、加载警告/错误通道 | ✅ | DX12ModRuntimeLoad.cpp:1190-1630 |
| 65 | 每帧帧变量（window_width/cursor_*/rt_width/DPI…）可读 | ✅ | DX12VmFrameVariablesUtils.h:92-113；VM 懒收集门（MigotoCommandVmUtils.hpp:24-31） |
| 66 | 通知（Notification/help.ini 文本）系统 | ❌（DX11 专属；DX12 无 FormatText/Notification 实现） | 全库检索仅 DX11（Overlay.cpp / help.ini） |
| 67 | 字符串格式化资源（FormatText/notification_timeout） | ❌ | 同上 |

## 附：Dialyn 未用到、但同语法族的其它命令键状态（供整体兼容性参考）

| 命令键 | 状态 | 说明 |
|--------|------|------|
| `dispatch = x,y,z` | ✅ | 追加替换 dispatch（EXH:355-365）；pre-skin 旁路 PSR:4420-4455 |
| `checktextureoverride = ib / vbN / ps-tN / cs-tN…` | ✅ | 在动作流中途对目标再发起一次 TO 匹配并执行（EXH:828-840/907-911） |
| `vs/ps/cs = Resource`（SetShader） | ❌ 静默忽略 | 解析 CFG:842-846；执行空 break（EXH:1363-1365/1445-1447），编译标 unsupported |
| `blend/cull/topology/depth/stencil/oN`（SetRenderState） | ❌ 静默忽略 | 解析 CFG:875-878（MigotoIniCommandListUtils.hpp:158-164） |
| `xN/yN/zN/wN = expr`（SetShaderConstant） | ⚠️ 降级 | 只写入名为 x0/y0… 的普通变量，不真正改写 shader constant（EXH:1475-1479） |
| `vs-cbN/ps-cbN/cs-cbN`、`cs-uN`、`ps-uN`（CB/UAV 绑定动作） | ⚠️ 部分 | 生成"虚拟目标绑定"参与匹配/查找（ApplySetDescriptorResourceAction EXH:461-495），不直接改写命令流描述符 |
| `[ShaderOverride]` / `[ShaderRegex]` / `[CustomShader]` / `[BuiltIn*]` / `[Preset]` 节族 | ✅（Dialyn 未使用） | 解析与运行时存在（DX12Config.h:1011-1146/1807-1986） |

## 十一、结论摘要（Dialyn 直接受影响项）

1. **❌ 会使该 Mod 功能失效的点（无需移植就不可用）**：
   - `Resource\ZZMI\Diffuse/NormalMap/LightMap/MaterialMap = ref …` 贴图槽绑定（no-op）＋ 依赖 `CommandList\ZZMI\SetTextures`（XXMI 库）→ **贴图替换链路不通**；需改写为 DX12 支持的 `ps-tN`/`this=` 形式并按游戏实际槽位重映射（Dialyn 仅 ps-t9 那一处能用）；
   - 通知逻辑（[CommandListCreditInfo] 的 pre 行、`data="…"`、notification_timeout）→ 无效果。
2. **⚠️ 语义近似点**：`DRAW_TYPE` 恒为 1（按绘制类型注入未实现）；persist 只做到进程内重载保留（跨进程/自动保存未做）。
3. **✅ 主体几何/绘制替换链路完整可用**：hash 匹配（IB/VB=资源元数据哈希 + 区域哈希兜底，SRV=内容哈希+描述符哈希，见 MTM:813-823/1359-1444）、match_first_index 分段（DrawContext 逐项过滤）、handling=skip（skip+有新 draw=只画新 draw）、ib/vbN 替换、条件 drawindexed 分支（多条顺序全部执行）、表达式/变量、[Present] 与按键 cycle（前向 + back 反向）——Dialyn 中所有 mesh/IB/VB 部分写法兼容。**注意**：DX12 的 IB/VB hash 口径是"视图/资源元数据哈希"（含 stride/format/slot，非内容哈希，DX12ResourceContentHashing.cpp:1431-1493），与 XXMI/DX11 录制出的 hash 不一定互通，正式使用前需在 DX12 下用本项目 hunting 工具重新录 hash。
4. **🔶 运行前提**：Dialyn.ini 是 XXMI 生态产物，引用 ShaderFixes/ZZMI 帮助库；在 Bunny DX12 上要让贴图与通知生效，必须把引用的库 ini 一并纳入加载集（或用 DX12 原生等价写法替换后再加载），缺失引用不会报错只会静默。
5. **架构提示**：命令执行是"解析 → 静态编译计划（仅 9 类 op）→ 含控制流/变量等自动回退动态解释（executor+VM）"双层；Dialyn 几乎全部节都带 if/变量，会走动态解释路径，功能正确但属于慢路径（每次命中 draw 现算）。

## 十二、对照 DX11 补齐进度（本次实现 + 遗留风险）

### 已完成（低风险，编译验证通过：DirectX12 与 DirectX11 目标）
| 项 | 改动 |
|----|------|
| `back =` 反向循环键 | MigotoKeyBindingUtils.hpp：Binding 增加 back 组合与独立边沿状态；type=cycle 时 back 按下反向步进（wrap + 按当前值重新定位，与 DX11 默认 smart/wrap 对齐）；解析/绑定注册逻辑同步放宽 |
| VK 键名表对齐 DX11 | DX12 键名解析改为共享 common/vkeys.h 的 VKMappings（含 OEM_1-8/OEM_102、0x 十六进制、F1-F24、媒体/小键盘/鼠标名等，与 DX11 同一表） |
| vkeys.h 去 DX11 依赖 | include 从 util.h（DX11 专用大头）改为 util_min.h + wchar.h；VKMappings 元素改 `const wchar_t *`；ARRAYSIZE/wstring 依赖本地化（DX11 input.cpp 不受影响，已重编译通过） |
| persist 变量进程内重载保留 | MigotoCommandVmUtils 增加 Save/RestorePersistSnapshot；DX12ConstantsVmInitializerUtils 在 ClearGlobals 前快照 [Constants] 顶层 `global persist $x`/`persist $x` 声明变量现值，重放默认值后写回（if 块内的声明不参与，避免复活失效值） |

### 未做（风险高 / 需要更大系统，不建议在本次顺手改）
| 项 | 风险说明 |
|----|----------|
| `Resource\name = ref X` 别名/重定向语义 | 需贯通"绑定槽→资源"命名体系与绘制描述符改写，改动面大且 DX12 已提供等价物 `ps-tN`/`this=`；Dialyn 需改写 ini 使用 |
| ZZMI / ShaderFixes / help.ini 帮助库 | 属 XXMI 部署生态（外部 ini 库），DX12 未内置；缺 include 即静默 |
| 通知 / FormatText / notification_timeout | DX12 无 overlay 文本渲染/通知子系统，全量移植是独立大功能 |
| `DRAW_TYPE` 按绘制类型注入 | 需把 draw 类型上下文贯穿所有表达式求值点（热键 condition/TO 动作/Present/Constants），涉及多线程录制路径，风险中高；Dialyn 对 indexed draw 的用法在恒 1 下近似成立 |
| `data = "文本"` 字符串 Buffer | 无通知/格式化消费方，做成 hex 之外无收益 |
| persist 跨进程持久化（d3dx_user.ini 自动保存） | 需要磁盘用户配置系统（读写时机/冲突/清理），DX11 专属大机制 |
| Key 节多个 `key=`/`back=` 行注册多组合 | DX11 支持一节约多个键（全部注册）；DX12 仍为单组合（重复行后者生效），重构为多组合需重做按键状态机，暂列遗留差异 |
