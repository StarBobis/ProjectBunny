# DX12 性能问题分析与修复计划

本文档分析 4Dmigoto DX12 端 CPU 开销过高（GPU 饥饿、帧率低于 DX11）的根因，
给出每个问题的代码证据、修复方式、修复原理与可验证的验收标准。

核心结论：

1. 常驻资源索引本身不是错误。DX12 API 不再提供 DX11 的"白送"能力
   （IA 绑定直接给 COM 指针、资源创建时 CPU 可见内容、shader 对象独立），
   因此 GPUVA 到 resource、descriptor 到 resource 的索引是 DX12 的固有税。
2. 真正的错误是把 DX11 的"draw 时查询状态做匹配"模型原样移植到了 DX12。
   DX11 里查询免费，所以 draw 时匹配成立；DX12 里查询结果要靠自费维护的
   影子状态重建，draw 时匹配等于维护税加匹配税的双重成本。
3. DX12 专属的正确路径是：create 时打标签、bind 和 descriptor 写入时偷换、
   draw 时只看一次 PSO 决策。DX12 的所有绑定动作都是显式可拦截的，
   且频率远低于 draw。把匹配从 draw 路径挪到 bind 和 create 路径后，
   绝大多数 draw 的 mod 开销可以降到接近零。

---

## 1. 问题清单（含代码证据）

### 问题 P1：draw 热路径上"缓存叠缓存"，每层缓存的 key 比较本身就是开销

现象：`HandleDrawOverrides`（`src/DirectX12/Hooks/DX12CommandFlow.hpp:3471`）
中每个 draw 要依次经过：

1. PSO decision 缓存（`EnsureDrawDecision`）
2. bound-draw 阴性缓存（`DX12BoundDrawResultCache::Matches`）
3. IA 状态阴性缓存（`IaStateNegativeCache`）
4. direct IA fast-reject 缓存（`DX12DirectIaFastRejectCacheUtils`）
5. descriptor 候选预过滤（`DX12ModHasAnyDirectDescriptorTextureOverrideCandidate`）

证据：`DX12DirectIaFastRejectCacheKey`
（`src/DirectX12/Commands/DX12CommandRuntime.hpp:636`）包含
`std::array<UINT64, 32>` 的 vertexBufferLocations / vertexBufferSizes /
vertexBufferStrides 等字段，一次"快速拒绝"需要比较约 100 个字段
（约 800 字节 memcmp）。即使缓存命中，每个 draw 也要付出构建 key
加上比较大 key 的成本。

根因：这些缓存存在的唯一理由，是"draw 时才知道该 draw 绑了哪些资源"。
这是 DX11 `BeforeDraw` 模型的直接翻译（DX11 端见
`src/DirectX11/HackerContext.cpp:757`，那里查询是免费的指针查找）。
在 DX12 里，"当前绑定状态"不是免费查询，而是自费维护的影子状态，
因此在 draw 时做任何形式的"全状态比较"都是结构性开销，
加更多缓存只是把开销从一种形态换成另一种形态。

### 问题 P2：descriptor 纹理替换在 draw 时解析 SRV 表，而不是在 descriptor 写入时替换

现象：draw 时需要从影子状态解析 `SetGraphicsRootDescriptorTable` 绑定的
descriptor 区域，逐个 SRV 查 texture hash
（`ResolveGraphicsSrvBindings`，`src/DirectX12/Implementation/DX12CommandExecution.inl:557`；
`HashDescriptorBindingResource`，`src/DirectX12/Implementation/DX12IaRuntime.inl:1967`），
命中后还要在 draw 当场上传替换 descriptor 表
（`DX12DrawReplacement.inl` 的 `DX12DrawSrvPatchTable` 机制）。

根因：DX11 里 `PSSetShaderResources` 传的是 view 对象，只能在 draw 前换 view。
DX12 里 descriptor 是调用者自己写进 heap 的字节，`CreateShaderResourceView`
直接给 resource 指针，`CopyDescriptors` 给 src 和 dst——完全可以在写入时
就把 mod 纹理的 descriptor 写进去，draw 路径什么都不用做。
当前实现没有利用这一点。

### 问题 P3：IA buffer（VB/IB）替换在 draw 时做 GPUVA 解析与 hash，而不是 bind 时替换

现象：`IASetVertexBuffers` 和 `IASetIndexBuffer` 只记录 view 到影子状态
（`src/DirectX12/Implementation/DX12BindingState.inl:2879` 起），
注释明确写着 "Resource resolution DEFERRED to draw time"。
draw 时才做 GPUVA 到 resource 解析（`DX12ResolveBufferResourceByGpuVa`，
`src/DirectX12/Implementation/DX12ResourceQueries.inl:163`）
和区域内容 hash（`DX12HashIaBufferContentView`，同文件 1320 行起，
带 L1/L2/L3 缓存）。

根因：同样是 DX11 draw 时匹配模型的移植。bind 调用频率不超过 draw 频率，
且 bind 时 GPUVA 到 hash 是缓存命中的范围查询；在 bind 时命中 mod
就直接把 `D3D12_VERTEX_BUFFER_VIEW` 指向替换 buffer，draw 完全不用参与。

### 问题 P4：常驻索引维护面过大，没有按"是否可能成为 mod 目标"预过滤

现象：Map 写入捕获（`CaptureResourceMapWrites`，
`src/DirectX12/Implementation/DX12ResourceHooks.inl:106`）、
buffer 内容缓存（`gBufferContentByResource`，
`src/DirectX12/Implementation/DX12ResourceQueries.inl:525`）、
descriptor 全量记录（`RecordCopiedDescriptors`，
`src/DirectX12/Implementation/DX12ResourceHooks.inl:625`）
对所有资源一视同仁。

根因：hash 算法必须与 DX11 3Dmigoto 兼容（mod 作者发布的就是 DX11 hash 值），
所以内容捕获机器必须保留；但捕获面可以按 desc（尺寸、stride、格式、维度）
预过滤——mod 配置里的目标是已知的，不可能匹配的 resource 根本不该进索引。

### 问题 P5：热路径上残留 debug 和 telemetry 调用的结构成本

现象：`DX12ResidentMaintenanceTelemetryUtils::Record*`、
`DX12OuterTimingProbeUtils::ApiWallScope` 等散布在每个 hook 里。
其中 `ApiWallScope` 在 release 下是空类
（`src/DirectX12/Mods/DX12ModInterfaces.hpp:894` 的 `#else` 分支），
`DX12_HOTPATH_TIME_*` 同理可空。但 telemetry record 调用是否全部
可编译消除需要逐一核实（见 3.5 验收）。

根因：优化迭代过程中埋点与逻辑交织，缺乏"release 下零成本"的强制保证。

### 问题 P6（不可消除项，需隔离而不是消除）：pre-skin 动态内容

pre-skin 类 mod 的 VB 由 GPU 每帧重算，hash 每帧变，无法 create 时打标签。
`src/DirectX12/Implementation/DX12PreSkinRuntime.inl` 的 UAV 跟踪常驻机制
对这一类 mod 是必需的。问题在于它的成本不应让全部 draw 陪跑——
当前 `DX12HotPathState::FLAG_PRE_SKIN_PROBE` 一旦置位，
dispatch 和 draw 路径都会进入更重的分支。

---

## 2. 修复原理：为什么"挪到 bind 和 create 时"在 DX12 成立

### 2.1 DX11 模型为什么便宜

DX11 3Dmigoto 是"创建时打标签，draw 时只查表"：

- `IASetVertexBuffers` 和 `PSSetShaderResources` 直接给 COM 资源指针，
  bind 时存下，draw 时按指针查 hash，O(1)。
- 资源内容 hash 在创建时算好（初始数据 CPU 可见）或 Map/Unmap 时增量更新，
  之后永久有效。
- shader hash 在 `CreateVertexShader` 时算好。

draw 路径上的 `BeforeDraw` 本质是几个指针查找，几乎免费。

### 2.2 DX12 收回了什么，又给了什么

收回的（必须自费维护索引，这是固有税）：

- IA 视图只有 GPUVA；SRV 只有 descriptor handle。
- DEFAULT heap 内容 CPU 不可读，hash 要靠 upload buffer 捕获加 copy 传播。

白送的（DX12 相对 DX11 的结构性优势，当前没用足）：

- 所有绑定都是显式 API 调用，可拦截：`CreateShaderResourceView` 给 resource
  指针、`CopyDescriptors` 给 src 和 dst、`IASet*` 给完整 view 结构、
  `SetPipelineState` 给 PSO 指针。
- descriptor 是调用者自己写的字节：写入时换成 mod 的 descriptor，游戏无感知。
- PSO 不可变且创建时 shader 已知：PSO 级决策可以一劳永逸（当前已实现）。
- bind 频率不超过 draw 频率：把匹配挪到 bind 和 create，天然减少触发次数。

### 2.3 修复后的目标模型

| 阶段 | 动作 | 成本 |
|---|---|---|
| 资源创建和上传 | 算 hash、打"mod 目标"标签（按 desc 预过滤） | 每资源一次 |
| SRV 写入（CreateSRV / CopyDescriptors） | 资源有标签则直接写 mod 纹理的 descriptor | 每 descriptor 一次查表 |
| IA bind（IASetVB/IB） | GPUVA 到 hash 缓存查询，命中则替换 view 指向 mod buffer | 每 bind 一次缓存查询 |
| SetPipelineState | 换为预编译的 mod 变体 PSO（已实现） | 每 PSO 一次 |
| draw | 只查一次 per-PSO decision；无动态条件的 mod 时直接放行 | 约一次指针解引用 |

draw 路径只保留两类工作：

1. per-PSO decision 查找（已实现）。
2. 带动态条件的 mod（`match_first_index`、按键切换、command list 逻辑、
   pre-skin）才需要 draw 时评估——由 decision 里的标志位 gate，
   无此类 mod 时整段短路。

### 2.4 为什么这是可证实的

每个修复点都有可测量的验收指标（第 3 节逐条给出）：
用现有 `DX12_ENABLE_DETAILED_TIMING` 加 `DX12Profiling`
（`src/DirectX12/Diagnostics/DX12Profiling.hpp`）在修复前后对同一场景采样，
对比每 draw 平均 CPU 时间、每帧 hook 总时间、GPU 空闲占比。
功能正确性用 mod 生效与否加 frame analysis dump 对比验证。

---

## 3. 修复计划（按落地顺序，含验收标准）

### 3.1 第一步：descriptor 写入时 patch（收益最大、风险最低）

改动：

1. 在 texture hash 缓存命中时给 resource 打"mod 替换目标"标签：
   扩展 `DX12CacheTextureHash`（`DX12ResourceQueries.inl:1126`），
   存储 hash 到 replacement resource/descriptor 的解析结果
   （在 `DX12ModState.inl` 的 texture override 索引里查一次并缓存）。
2. hook `CreateShaderResourceView`（`DX12ResourceHooks.inl:900`）：
   若 resource 有静态替换标签，改写调用参数，直接为 mod 纹理创建 SRV
   （保持 view desc 兼容）。
3. hook `CopyDescriptors` 和 `CopyDescriptorsSimple`
   （`DX12ResourceHooks.inl:1023/1055`）：拷贝源 descriptor 指向有标签资源时，
   改为拷贝 mod 纹理的 descriptor。ring heap 游戏每帧重拷 descriptor，
   成本为 O(拷贝次数)，仍比 O(draws 乘解析深度) 低一个量级。
4. 对走新路径的 section，draw 路径的 SRV patch
   （`DX12DrawSrvPatchTable`）与 descriptor 匹配整段短路：
   在 per-PSO decision 中记录"该 PSO 的所有纹理目标均为静态替换"，
   draw 时跳过 `ResolveGraphicsSrvBindings` 和 `HashDescriptorBindingResource`。

适用范围声明（重要）：只有无条件静态替换（hash 命中即换，
无 `match_first_index` 和 command list 动态逻辑）的 TextureOverride 走此路径。
带动态条件的仍走 draw 时匹配。决策在 ini 加载和编译 plan
（`BuildDx12CompiledTextureOverridePlans`，`DX12IaRuntime.inl:877`）时做出。

原理：descriptor heap 是 CPU 写的字节流，游戏通过 `SetDescriptorHeaps`
加 root table 引用其中的槽位。在写入侧替换内容后，GPU 管线天然使用
mod 纹理，draw 时零干预。这是 DX12 独有、DX11 做不到的路径。

验收：

- 功能：静态纹理 mod 生效，截图和 dump 对比与修复前一致；
  动态条件 mod 行为不变。
- 性能：开启 `DX12_ENABLE_DETAILED_TIMING`，纯静态纹理 mod 场景下
  `DrawOverrideTotal` 每 draw 均值下降；`ResolveGraphicsSrvBindings`
  调用次数降为 0（可在 release-timing build 里计数）。
- 回归：ring heap 逐帧重拷场景下 mod 持续生效（跑 5 分钟无闪烁）。

### 3.2 第二步：IA bind 时替换

改动：

1. 在 `DX12BindingTracker::SetIndexBuffer` 和 `SetVertexBuffers`
   （`DX12BindingState.inl:2879/2927`）记录 view 后，增加一次
   GPUVA 到 hash 的缓存查询（复用 `DX12HashIaBufferContentView` 的 L1/L2），
   命中静态几何 mod 时生成替换 view。
2. wrapper 层 `IASetVertexBuffers` 和 `IASetIndexBuffer`
   （`DX12CommandListWrapper.inl:1124/1138`）下发替换后的 view。
3. per-PSO decision 标记"IA 目标全部静态"，draw 时跳过 IA hash 解析与匹配；
   pre-skin 和动态条件目标仍走原 draw 路径。

原理：bind 时 GPUVA 到 hash 是 L1 缓存命中的范围查询（成本远低于 draw 时的
完整 `DX12IaHashState` 构建加多层阴性缓存 key 比较）；替换 view 后，
draw 对 mod 完全无感知。失效由已有的 content generation 和
resolve generation 机制保证（buffer 被重写则 generation bump，
bind 时重新评估）。

验收：

- 功能：VB/IB 替换 mod 生效；buffer 被游戏重写后 mod 不串扰
  （generation 失效生效）。
- 性能：`DX12HashIaBufferContentView` 在 draw 路径的调用次数降为 0；
  bind 路径新增开销用 hot-path timing 验证小于 draw 路径节省的开销。

### 3.3 第三步：draw 路径瘦身

改动：

1. 3.1 和 3.2 落地后，收紧 `HandleDrawOverrides`：decision 标记全静态时，
   在 `DrawPsoEarlyExit` 阶段直接返回，不再构建
   `DX12DirectIaFastRejectCacheKey` 等大 key。
2. 合并或删除冗余阴性缓存层：bound-draw 缓存、IA 阴性缓存、fast-reject 缓存
   三者职责重叠，保留一个 key 最小的（建议保留 bound-draw 缓存，
   key 为 epoch 加 serial 等少数标量）。
3. pre-skin gate 精细化（问题 P6）：`FLAG_PRE_SKIN_PROBE` 只在
   dispatch 路径生效，draw 路径的 preskin 分支移到 decision 标志后面。

原理：缓存的价值等于命中节省的成本减去 key 构建和比较成本。
当 draw 时只剩"是否有动态条件 mod"一个问题时，
标量 key 小缓存严格优于 800 字节 key 的大缓存。

验收：

- 性能：无动态条件 mod 的普通 draw，CPU 开销目标约 100ns 量级
  （flag 检查加 decision 解引用）；用 `DX12OuterTimingProbeUtils` 的
  `ApiDrawIndexedInstanced` bucket 与 `DrawHookTotal` 对比验证。
- 功能：动态条件 mod（match_first_index 等）逐一回归。

### 3.4 第四步：常驻索引维护面收窄

改动：

1. ini 加载时汇总所有 mod 目标的 desc 特征（宽度、高度、格式、stride、
   尺寸集合），构建预过滤器。
2. `ShouldRecordCreatedResource`（`DX12ResourceHooks.inl:414`）、
   `CaptureResourceMapWrites`、`RecordCopiedDescriptors` 接入预过滤：
   不可能匹配任何目标的 resource 和 descriptor 不进索引。
3. buffer 内容缓存（`gBufferContentByResource`）按页和区域惰性化，
   只保留可能成为 IA 或 descriptor 目标的区域。

原理：hash 兼容性要求捕获机器保留，但捕获面正比于内存占用和维护 CPU。
mod 目标是已知闭集，预过滤是无损收窄。

验收：

- 性能：索引内存占用下降（日志打印 `gBufferContentByResource` 等 map 尺寸）；
  每帧 `CaptureResourceMapWrites` 和 `RecordCopiedDescriptors`
  处理字节数下降。
- 功能：全量 mod 回归。

### 3.5 第五步：telemetry 和埋点零成本化审计

改动：

1. 逐一核实 `DX12ResidentMaintenanceTelemetryUtils::Record*` 在
   release 非 timing build 下是否编译消除；不能消除的改为与
   `DX12_HOTPATH_TIME_*` 相同的宏开关模式
   （参考 `src/DirectX12/Hooks/DX12CommandFlow.hpp` 顶部的宏定义模式）。
2. `DX12Profiling::Record*` 同理：确认 `collectCounters` 关闭时是
   单次分支加内联空操作。

验收：release build 反汇编抽查 2 到 3 个最热 hook
（`DrawIndexedInstanced`、`SetGraphicsRootDescriptorTable`），
确认无埋点残留调用；或用 timing build 对比开关前后的 ApiWall 差值。

### 3.6 禁止事项（防止回到错误模型）

- 禁止在 draw 路径新增任何以"全绑定状态"为 key 的缓存。
- 禁止在 draw 路径调用 `DX12ResolveBufferResourceByGpuVa`、
  `DX12HashIaBufferContentView`、`ResolveGraphicsSrvBindings`
  （decision 标记动态条件的除外，且必须 gate 在标志位后）。
- 新增 mod 匹配需求时，默认问："能否在 create、bind、descriptor 写入时完成？"
  只有答案明确为否（动态条件）才允许进 draw 路径。

---

## 4. 预期效果与边界

预期：纯静态 mod（占实际使用的大多数）场景下，draw 路径开销降到
接近 DX11 水平（每 draw 约一次 decision 查找），CPU 帧时间下降，
GPU 饥饿缓解。具体数值以实测为准，不设虚构目标。

消不掉的差距（诚实声明）：

1. ring heap 游戏每帧重拷 descriptor，patch 成本 O(拷贝数)，非零但远小于现状。
2. pre-skin 动态内容必须保留 UAV 跟踪常驻机制（问题 P6），
   这是 DX12 相对 DX11 的硬性劣势区，只能靠隔离（gate）减少波及面。
3. hash 算法必须与 DX11 3Dmigoto 兼容，内容捕获机器必须保留，只能收窄。
4. 常驻索引本身（GPUVA 到 resource、descriptor 到 resource）是 DX12 固有税，
   本计划不消除它，只让 draw 不再查询它。

## 5. 验证方法论

1. 基准场景：选取 (a) 纯静态纹理 mod、(b) 几何替换 mod、
   (c) pre-skin mod、(d) 无 mod 四组场景，同一台机器同一场景各采样 60 秒。
2. 工具：`build_release_timing_x64.ps1` 构建 release-timing 版本，
   开 `DX12_ENABLE_DETAILED_TIMING`；用 `DX12Profiling` 汇总
   `DrawIndexedInstanced`、`DrawOverrideTotal`、`IaDrawOverrideChecks`
   的计数与耗时；外部用 PresentMon 或类似工具记录 GPU busy 占比与帧率。
3. 对照：修复前先在 docs 留一份基线数据（每 draw 均值、每帧 hook 总时长、
   FPS、GPU busy 百分比），每一步修复后更新同一张表。
4. 正确性：frame analysis dump（`DX12FrameAnalysis`）在修复前后
   对同一场景抓取，对比 mod 目标 draw 的最终绑定结果一致。
