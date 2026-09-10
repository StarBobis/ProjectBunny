# DX12 Mod 能力补齐：CustomShader 与条件绑定保真（长期计划）

> 背景：`Remielle Blancheplume and Noireplume full ver 4.3` 是一个 GIMI(DX11) 风格 Mod。
> DX12 解析探针（build/dx12_ini_probe）实测：语法解析已 100% 覆盖（0 未识别）。
> 但有三类 **运行时能力** 差距。本文档给出分阶段实施计划，每阶段以
> 解析探针 + `build_release_timing_x64.ps1` 全量构建为验收门槛。

## 差距总览

| # | 差距 | 本 Mod 中的用法 | DX11 机制 | 状态 |
|---|------|-----------------|-----------|------|
| 1 | `post xNN = expr` 延迟常量赋值 | `[CommandListOutfitMenu]` 保存/恢复 x87..w88 | `post <任意命令>` 路由到 post 命令表 | ✅ 已修复（2026-09） |
| 2 | TextureOverride 内 if/else 包裹的 `this=`/`ps-tN=` 条件绑定 | `[TextureOverrideReplace420e2418]`/`baf9e1be`/`fe9fc31a` | 绑定指令不在 whitelist，按运行时命令 + 分支求值 | ⏳ 阶段 1 |
| 3 | CustomShader 自定义绘制执行栈 | `[CustomShaderElement]`（vs/ps=.hlsl、blend/cull/topology、o0=set_viewport bb、Draw=4,0） | CustomShader 编译缓存 + ini-params 常量缓冲 + RTV/视口绑定 + 注入 Draw | ⏳ 阶段 2 |
| 4 | resource 引用/别名（ref 指令、运行时保存游戏纹理到占位自定义资源） | `ResourceHeadpieceSavedT0 = ref ps-t0`、`ps-t0 = ref ResourceHeadpieceSavedT0`、`Resource\ZZMI\Diffuse = ref ...` | CustomResource 惰性引用 + ResourceCopyOperation | ⏳ 阶段 3 |

外置依赖（`CommandList\ZZMI\*`、`CommandList\RabbitFX\*`、`CommandListSkinTexture` 来自其他 ini）
**不在**本计划内。

## 设计原则（DX12 化，非照搬）

1. **两阶段模型**：DX11 在即时上下文里"边解释边执行"；DX12 录制在命令列表上，
   必须区分 **装载期静态规划**（匹配、预加载、PSO 缓存所需）与 **录制期运行时求值**
   （VM 分支 + action 流）。静态捕获只允许捕获"无条件、与顺序无关"的事实；
   任何处于控制流内的指令一律进运行时 action 流。
2. **xNN 常量已经是 VM 全局**（`AssignSymbol`），CustomShader 阶段只需要在
   注入 Draw 前把它们打包成 GPU 常量缓冲（每帧一份、可复写），而不是另起一套。
3. **自定义绘制 = 在游戏命令列表上临时替换状态**：必须保存/恢复游戏原 PSO 相关状态
   （RTV、视口、图元拓扑、混合/光栅状态、描述符绑定），并在结束后还原，
   与 DX12CommandListStateCapture 思路一致。
4. 不产生屎山：所有新增类型放头文件顶层（禁止函数内 struct/class），C++20。

---

## 阶段 1：TextureOverride 条件绑定保真（issue #2）

### 现状问题
`DX12Config.h` `MigotoTextureOverrideParser::ParseSections` 把
`this`/`ib`/`vbN`/`ps-tN`/`vs-tN` **无条件**收进静态字段：

- `[TextureOverrideReplace420e2418]` 的 `if $render_form==0 ... this=Form1 else this=Form2`
  → 静态字段只留下 Form2，且运行时永远用 Form2。
- `fe9fc31a` 里 if 内的 `ps-t0 = ref ResourceHeadpieceSavedT0` 同理。
- `this` 更严重：连 action 都不存在（ParseTarget 不识别 "this"），条件分支根本无法表达。

### 修改方案
1. **解析器（DX12Config.h）**：TextureOverride/ShaderOverride 段遍历时维护分支深度
   （`if` +1 / `endif` -1，像 `CollectTopLevelPersistNames` 那样）。
   - 深度 0 的绑定指令：维持静态捕获（快路径）+ 原有 action 双轨；
   - 深度 >0 的 `ib`/`vbN`/`ps-tN`/`vs-tN`/`this`：
     - 不再写静态字段；
     - `ib/vbN/ps-tN/vs-tN` → 走通用 `ParseActionFromEntry`（SetIndexBuffer/
       SetVertexBuffer/SetShaderResource action），执行器已支持且受 `mVm.Active()` 门控；
     - `this` → 新增 `CommandListActionKind::SetThisResource`，值 = 分支选中的资源；
     - `this` 出现的段置 `hasConditionalThisReplacement=true`（匹配武装标记）。
2. **匹配层保持**：`hash + 无条件 this` 的语义仍需参与"按 SRV 内容哈希匹配"，
   所以静态侧保留 **匹配标记** `hasConditionalThisReplacement`（不再存资源名），
   保证 hash 匹配照旧武装；替换动作交给运行时。
3. **执行器（DX12CommandListExecutor.h）**：`SetThisResource` 在 RunActions 中，
   受分支门控，通过当前 match 的 `thisSrvBinding`（槽位/阶段）把替换目标解析成
   具体描述符目标，复用 `ApplySetDescriptorResourceAction` 的下半段
   （BuildResolvedDescriptorBindingsForResourceOverride）。
4. `ps-tN`/`ib`/`vbN` action 的执行器路径已存在，验证嵌套时分支门控生效即可。

### 阶段1 实施状态（2026-09 首轮完成）
- [x] `CommandListActionKind::SetThisResource`（解析器+执行器+分类函数同步）；
- [x] `TextureOverrideConfig::hasConditionalThisReplacement` 匹配武装标记；
- [x] TextureOverride 解析器分支深度感知（顶层静态快路径不变 / 嵌套进 action 流）；
- [x] 执行器 `ApplySetThisResourceAction`（AddThisRequestFromMatch + QueueRequests，
      与静态 ApplyConfig 同机制，无 this 目标时降级为空操作 + 诊断日志）；
- [x] 效果/武装分类函数覆盖新 kind（TextureOverrideActionsHaveRuntimeEffect /
      CommandListActionHasPhaseRuntimeEffect / TextureOverridesHaveDrawSrvReplacement）；
- [x] 探针断言：420e2418(2×SetThisResource)/baf9e1be(2×)/D8804FC4(顶层静态保持)/
      fe9fc31a(嵌套 ps-tN 全部进 VM action) 全 PASS；0 未识别行；
- [x] `build_release_timing_x64.ps1` 全量构建通过。
- [ ] **游戏内验证（待运行环境）**：头饰 Form 切换（420e2418）、脸皮条件换肤（baf9e1be）、
      fe9fc31a 发饰变色重绘（依赖阶段3 的 ref 别名语义，此处仅确认分支动作不产生副作用）。
- [ ] 已知边界：cs-tN/srvN（pre-skin 槽）仍无条件静态捕获（未在 if 内使用场景），
      若后续 Mod 在控制流内使用再扩展；`match_*`/`hash`/`handling` 按段级语义处理不变。

### 验收
- 探针：嵌套 `this=` 出现在 actions（kind=SetThisResource），顶层仍静态；
- 全量构建通过；
- （游戏内验证放到具备运行环境后，阶段 1 交付以模型与构建正确为准，
  记录待验证项：Genshin/ZZMI 场景下 Form 切换、头饰换肤条件。）

## 阶段 2：CustomShader 执行栈（issue #3）

### 子步骤（建议按依赖顺序）
2.1 [CustomShader]/[CustomShaderX] 段的语义装载：vs/ps 文件路径、blend/cull/topology/
    o0=set_viewport bb 渲染状态、命令表与 xNN 常量边界——产出"编译单元描述"。
    （解析已通，这里是运行时模型：编译一个 vs/ps 组合 = 一个 PSO 键。）
2.2 HLSL 运行时编译（复用 d3dcompiler，参考 DX12ShaderDump 装载方式），
    输出 DXIL blob + 反射（输入布局/根参数需求）。
2.3 自定义根签名 + ini-params 常量缓冲设计：
    - 固定布局（每个 CustomShader 一个缓存 PSO）：cbuffer 放 x0..w(N)
      （DX11 语义：x87 写入 = 自定义着色器常量空间）；
    - 常量上传：DX12 每帧环形/复用上传缓冲，录制点把 VM 全局 xNN 打包；
    - ps-tN 纹理 → DX12LoadedResource 的 SRV（已有装载/堆基础设施）。
2.4 录制期注入 Draw：保存游戏状态（RTV/视口/topology/描述符绑定），
    设置 o0 = 交换链后缓冲（bb）RTV + 全屏视口，绑定 PSO/常量/SRV，Draw，
    再还原——参考 DX12CommandListStateCapture / DX12NativeOverlay 已有代码。
2.5 Present 路径接线：`[Present]` 命令表的 Draw 动作在
    `DX12ModPreparePresentReplacement` 链上真正执行。
2.6 内置命令 `BuiltInCommandListUnbindAllRenderTargets`（DX11 语义：清空 RTV 槽）。
    - 验证门槛：语法探针 + 构建 + 记录游戏内验证项（菜单 UI 渲染、点击热区）。

### 阶段2 实施状态（关键调研结论 + 第2轮进度）

**Mod 契约（由 draw_2d.hlsl 与 DX11 运行码交叉确认）**
- 自定义着色器通过 **t120 的 1D float4 纹理（IniParams）** 读取 xNN 常量：
  `x87/y87/z87/w87 → IniParams[87].xyzw`，即 x 寄存器 i 是第 i 个 float4 的第 0..3 分量。
  这决定了 DX12 必须提供的是"寄存器→float4 数组"映射（VM 全局 xNN 打包），
  而不是传统 cbuffer 偏移重排。UI 贴图经 ps-t100 等 SRV 槽绑定。
- DX11 执行（RunCustomShaderCommand::run）：进入元素时保存游戏 VS/PS、OM/RS 状态、
  视口、图元拓扑、采样器；应用自定义（编译好的 stage 文件 + blend/cull/topology
  覆盖 + o0 绑后缓冲 + 视口），跑元素命令体（含 Draw），最后全量还原。
- 状态文本语义（IniHandler 精读）：
  - `blend = op src dst`（如 `ADD SRC_ALPHA INV_SRC_ALPHA`）：BlendEnable=true，
    alpha 默认 ADD ONE ZERO；`blend = disable` 关闭；另可 `alpha = op src dst` 分通道；
    写掩码默认 ALL；
  - `cull = NONE/FRONT/BACK`；`fill = SOLID/WIREFRAME`；`front = Clockwise/...`；
  - `topology = TRIANGLE_STRIP/...`（D3D11 名字去前缀，含 patch N_CONTROL_POINT）。
- 缺省键的 DX11 语义是"与游戏当前状态 merge"；DX12 PSO 无法逐帧 merge，
  采用文档化替代默认值（在 2.3 PSO 装配时固定），游戏内验证兜底。

**第2轮交付（2.1 完成）**
- [x] 新增 `ModRuntime/DX12CustomShaderElement.h`：纯装载期模型
      `DX12CustomShaderElementSpec`（vs/hs/ds/gs/ps/cs 文件槽、blend/cull/fill/front/
      topology/sample_mask、oN 后缓冲目标标志、动作统计）+ 文本→枚举解析
      （token 表与 DX11 IniHandler 一致）；由同一份 CommandListConfig action 流重建，
      无第二套解析器；
- [x] `ParseActionFromEntry` 补 hs/ds/gs（此前 vs/ps/cs 之外的 stage 键被静默丢弃）；
- [x] 探针断言全 PASS：CustomShaderElement 注册、vs/ps=.\\res/draw_2d.hlsl、
      blend=ADD SRC_ALPHA INV_SRC_ALPHA、cull=none、topology=triangle_strip、
      o0=set_viewport bb（hasBackbufferTarget）、Draw 存在；
- [x] 头纳入 DX12ModRuntimeInternal.h 主 TU + 全量构建（待本项跑完确认）；
**第3轮交付（2.2 主体 + 2.3 设备无关部分完成）**
- [x] 新增 `ModRuntime/DX12CustomShaderCompile.h`：双后端 SM6 编译单元
      （DXC 动态加载优先，dxcompiler/dxil 进程内单次常驻装载、同目录配对；
      d3dcompiler_47+dxil 兜底）；字节码统一句柄 `DX12CustomShaderBytecode`
      （兼容 ID3DBlob/IDxcBlob）；路径解析（绝对→ini 目录→装载根目录）；
      按 文件路径+profile 缓存 + 修改时间失效 + 重载清缓存；
- [x] 新增 `ModRuntime/DX12CustomShaderRegisters.h`：xNN 寄存器名解析
      （x87→slot87.c0…）与 float4 打包纯工具（t120 IniParams 契约）；
- [x] 新增 `ModRuntime/DX12CustomShaderPipeline.h`：共享根签名序列化
      （t0..t127 SRV 表 + s0 静态采样器，D3D12SerializeRootSignature 动态解析），
      元素 spec→D3D12 blend/rasterizer/topology/PSO desc 映射；
- [x] 真实着色器编译验证：dxc.exe（CLI）对 draw_2d.hlsl 的 vs/ps 均 exit 0，
      字节码 4052/7536 字节；探针寄存器/打包/根签名/PSO 映射断言全 PASS；
- [x] 全量构建通过（dxcapi 来自 Windows SDK 26100）；
- [ ] **部署约束（记录）**：进程内 SM6 编译需要官方 dxcompiler/dxil（或
      支持复用的发行版）随 d3d12.dll 部署；本机 VulkanSDK 自包含版在进程内
      二次编译会 fail-fast（0xC0000409）→ 部署阶段把官方 DXC 发行文件放进
      ZZMIDX12/游戏目录即可（编译单元在首次绘制时按 (文件,stage) 缓存一次）；
**第4轮交付（2.3 设备侧 + 装载 registry 接线）**
- [x] 新增 `ModRuntime/DX12CustomShaderDevice.h`：
  - `DX12IniParamsTexture`：IniParams 1D 纹理（512×float4 R32G32B32A32_FLOAT，t120 契约），
    默认堆纹理 + CPU 专用 SRV + 4 槽 8KB 上传环（按 presentCount 取模）；
    `UpdateAndUpload` 在游戏命令列表上记录 CopyTextureRegion 并做
    COPY_DEST→PS_RES 状态转换（含跨帧 barrier 记账），无需每帧 GPU 同步；
  - `DX12CustomShaderPipelineCache`：设备级单例缓存——共享根签名
    （元素共用）+ 按 (section, RTV 格式) 的 PSO，首次使用才编译 stage 文件
    （复用 2.2 编译缓存），reload 时 ClearAll；
- [x] 装载接线：registry（`gCustomShaderElements`）在每次装载时由
      CommandListMap 重建并发布（与 gCommandLists 同锁同代际），
      TIMING 构建在 d3d12_log.txt 输出 `DX12CustomShaderDiag event=element_registered`
      （stage 文件/状态覆盖/bb 目标/命令体统计——游戏内可直接验证元素发现）；
- [x] 全量构建 + 增量 ninja 通过；
**第11轮交付（规格审计修复）**
- [x] IniParams 纹理读态覆盖 VS+PS 双阶段
      （`PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE`，上传/读取 barrier 同步用）；
- [x] bb 格式推导：TYPELESS 后缓冲映射 typed UNORM/FLOAT 视图格式
      （R8G8B8A8/B8G8R8A8/B8G8R8X8/R10G10B10A2/R16G16B16A16/R16G16/R16），
      RTV 与 PSO 均用 typed 格式（否则 CreateRenderTargetView 校验失败）；
- [x] 全量 clean 构建通过（38/38，d3d12.dll 部署）；
- [ ] 待办：游戏会话验证（菜单 UI、bb 路径、贴图）、条件/多 Draw 元素体、
      2.5 内置命令、阶段3 ref/别名语义、dxc 部署。

## 阶段 3：resource 引用/别名语义（issue #4）

3.1 `ResourceAssignment`（`ResourceX = ref <资源或槽>`）运行时语义：
    DX12 用"别名记录"实现——把当前绑定（槽+哈希+视图）记到名字 X 的别名槽；
3.2 绑定指令值侧 `ref <别名>` / `ref <资源>`：解析为别名槽或加载资源；
3.3 `ib = null` 解绑、占位空资源（`[ResourceX]` 无 filename）语义对齐；
3.4 验证：解析探针 + 构建 + 游戏内验证项。

### 阶段3 实施状态
- [x] 新增 `ModRuntime/DX12ResourceAliasStore.h`：线程安全名称级别名仓库
      （键大小写归一、链解析 ≤16 跳、环/自别名回退原查询、原子门控零开销路径，
      纯 `ResolveChain` 供外部表复用）；`ref` 值分类器（Resource 前缀=别名 /
      ps-tN=捕获模式暂不支持并 TIMING 记录一次）；
- [x] executor 两处动作流执行 `ResourceAssignment`：别名式注册 + 诊断日志
      （`DX12ResourceAliasDiag alias_added/unsupported_copy`）；
- [x] 加载解析**唯一咽喉** `EnsureLoadedDrawTextureSrv` 入口做别名解析
      （门控空别名零开销；自定义元素重放与 DrawSrv 补丁路径一并受益）；
- [x] `ib = null` / `vbN = null` 显式解绑语义：解析器识别 null（不产生
      `Resourcenull` 垃圾静态/动作），置 `explicitNullBindings` 供分类诊断；
- [x] reload 时随配置全局清空；
- [x] 独立测试全 PASS：别名链 12 项 + null 绑定 7 项（微测工程
      build/dx12_alias_test：alias_test/null_test）；
- [x] 全量 clean 构建通过（38/38，d3d12.dll 部署）；
- [ ] 待办：捕获模式（`X = ref ps-t0` 现场保存/恢复，需绘制期描述符捕获）、
      占位空资源对齐、游戏内验证。

## 贯穿性注意事项
- 探针工程在 `build/` 下会被发布脚本清空；需要复用时从 git 或备份恢复，
  或者迁移到非 build 目录（待定，避免污染仓库）。
- 所有新增 action/静态字段必须先过探针（识别/分类断言），再动执行器。
- 每阶段提交一次，构建一次，日志留档。

## 游戏内验证指引（部署后）
构建产物自动部署到 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\`（d3d12.dll + Run.exe）。
用该目录启动目标游戏后检查 `d3d12_log.txt` 中的诊断标记：

| 阶段 | 期望日志（DX12CustomShaderDiag / DX12KeyDiag / DX12ResourceAliasDiag / DX12SetThisResourceDiag） |
|---|---|
| 装载 | `element_registered`（CustomShaderElement 被发现） |
| 阶段1 | `SetThisResourceDiag applied`（条件 this= 命中）、`element_deferred recorded=1` |
| 阶段2 | `element_drawn`（UI 元素真实绘制）、或 `replay_skip reason=…`（逐项定位） |
| 阶段3 | `alias_added`（ref 别名注册）、`capture_placeholder_unloaded`（占位空资源被触碰） |

注意：进程内 SM6 编译需要官方 dxcompiler/dxil 与 d3d12.dll 同目录（见第3轮记录）；
缺失时元素管线会以 `pipeline_unavailable` 跳过并在日志注明。

## 收尾状态（截至目标第12轮）
- 阶段0/1：完成（解析 + 模型 + 构建 + 探针）。
- 阶段2：编译单元/寄存器/根签名/PSO 装配/IniParams 纹理与上传/表堆/表面缓存/
  记录-重放主链均已实现并通过全量构建；待游戏内验证。
- 阶段3：别名仓库 + ref 别名式注册/解析 + null 解绑 + 占位空资源降级完成；
  捕获模式（`X = ref ps-tN` 现场保存/恢复）需绘制期描述符捕获，依赖游戏环境联调。
- 剩余硬前置：游戏会话（本机无可用目标 exe/启动配置）。

## 审核修复记录（第13轮，只读审核 + 即时修复）
- 修复 Blocker×2（DXC blob 双重 Release；replacement 拷贝/移动丢 CustomElementRecord）、
  Major×4（重载不清自定义着色器缓存→代际惰性清理；patch 拓扑后缀 off-by-one；
  表堆 CPU 写/GPU 读竞态→4 槽轮换；重放状态泄漏→影子跟踪+重放后还原）与一批 Minor。
- 更正文档失实：`ExecutePresent` 并非"提交前列表尾"（在 Present 后首个被拦截 Draw 触发），
  重放后必须还原游戏 OM/视口等状态——已实现还原。

## Mod 适配补齐记录（第14轮，以 Remielle 4.3 为对账目标）
- 对该 Mod 做"特性使用 vs 已实现能力"对账：解析层 0 未识别行、VM 表达式（&&/||/cursor/time）、
  条件绑定、IA 替换、元素记录-重放均覆盖；实质缺口三项已补齐：
  1. **捕获模式 `X = ref ps-tN`**：新增 `DX12DrawTextureCaptureUtils`，绘制期把游戏当前绑定
     纹理以 **DX11 ref 语义（活引用：AddRef + 在游戏资源上按原 SRV desc 建自有 SRV）** 注册进
     `gLoadedResources`；零 GPU 拷贝、零 barrier、零命令列表介入，重捕获经 Retire 延迟销毁；
     执行器 `RegisterResourceAliasFromAction` 的非别名值改走捕获路径（解析层此前仅记录动作）。
  2. **`ref ` 前缀绑定修复**：`ps-tN = ref ResourceX` / `this = ref ResourceX` 的值此前带前缀
     直接查表必失败；两处加载咽喉统一 `BindingNameWithoutRefPrefix` 剥离，解析层
     `ResolveResourceReference` 支持 ref 前缀下的命名空间解析，`PreserveResourceAssignmentName`
     恢复 ResourceAssignment 键的原始大小写（gLoadedResources 大小写敏感）。
  3. **DXC 部署策略**：构建脚本仅部署 `third_party/dxc/` 的官方 dxcompiler/dxil 对；
     VulkanSDK 自包含 dxcompiler 同进程二次编译 fail-fast(0xC0000409) 会崩溃游戏进程，
     永不自动部署；缺失时编译优雅降级为 logged no-op（元素跳过、游戏稳定）。
- `run = BuiltInCommandListUnbindAllRenderTargets`（元素体内）：本架构重放始终自建 bb RTV，
  解析为缺失节引用即语义等价的空操作，无需额外实现；`CommandList\ZZMI\*`/`CommandList\RabbitFX\*`/
  `CommandListSkinTexture` 属运行环境（ZZMI 基础 ini 与其他 Mod）提供，缺失时 run 优雅空转。

## 复审修复记录（第15轮，全量 diff 复审）
- **帧内多元素竞态（Major）**：表堆轮换与 IniParams 上传环原本只按 present%4 选槽——同帧多个
  元素（该 Mod 菜单每帧最多十余个）共享槽位，而描述符写/上传 memcpy 在提交前即生效，GPU 执行时
  所有元素会读到最末元素的表/参数。修复：帧槽内按元素分配独立区域（表堆 64 区×128 描述符、
  上传 64 区×8KB），元素序号由 `AcquireElementRegion` 统一发放，超容量记日志跳过。
- **编译失败负缓存**：无 DXC 后端时此前每元素每帧重试两次编译；失败结果现按 (path,mtime)
  负缓存，重载时随 ClearCache 复位。
- 陈旧注释修正（ExecutePresent 调用点"无需状态还原"）。
- 复审确认无问题：重放恢复句柄展开语义、包装层 `mRuntimeState.device` 构造期就绪、
  执行器按事件栈构造（mPixelShaderResourceNames 无跨帧残留）、别名/捕获大小写一致性。
