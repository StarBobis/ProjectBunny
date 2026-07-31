# DX11 既有架构向 DX12 的迁移记录

日期：2026-07-31

## 1. 文档目的

本文只记录已经能在本仓库 DX11 实现中找到明确依据、并值得迁移或继续贯彻到 DX12 的架构原则。它不是泛化的 DX12 优化清单，也不把尚未验证的建议描述成 DX11 已有能力。

本文覆盖四类既有架构：

1. 按功能开关裁剪热路径，并隔离普通 Mod、Hunting 和 Frame Analysis。
2. 在绑定 API 发生时保存 resident state，并在无相关配置时提前拒绝工作。
3. 在资源写入路径更新、传播或污染资源 Hash，而不是在每个 Draw 上重新读取资源内容。
4. 对昂贵的区域 Hash 使用分层缓存，并在资源变化和帧边界进行精确失效。

以下方案不属于“DX11 已有架构”，因此不作为本文迁移目标：

- 完整的 Bound-state Compiled Draw Plan。DX11 只有绑定状态缓存和提前拒绝，没有缓存最终 Draw decision。
- DX12 descriptor table immutable snapshot。DX11 没有 descriptor heap/table 语义。
- 使用 GPU 可见控制缓冲即时改变任意已录制 command list 的行为。DX11 只有 IniParams 等局部先例。
- 继续增加 descriptor shard/slot lock。当前 DX12 已经有 heap shard state，是否拆除剩余全局锁必须先测量。

## 2. 总体迁移原则

DX11 的机制不能逐行照搬。DX11 immediate context 的绑定和 Draw 基本按调用顺序进入同一命令流；DX12 command list 可以多线程录制、提前 Close、延后提交和重复执行。因此迁移时必须保留以下 DX12 边界：

- 状态属于 command list，不能只维护一份进程全局 current state。
- Reset 必须开始新的状态世代；ExecuteBundle 必须合并或保守失效调用方状态。
- 资源内容和资源对象生命周期是两个不同的 generation。
- 已经 Close 的 command list 不能被 Present 时的新配置追溯修改。
- 普通 Mod 所需的 descriptor 当前值不能与 Capture 所需的历史事件一起关闭。
- 任何缓存命中都必须能由 reload、资源写入、descriptor 覆盖和对象释放可靠失效。

## 3. DX11 架构一：按功能裁剪热路径并隔离诊断模式

### 3.1 DX11 现有证据

DX11 在创建 Context 时区分轻量 `HackerContext` 和功能更重的 `FrameAnalysisContext`：

- `src/DirectX11/HackerContext.cpp:30` 的 `HackerContextFactory` 根据 Hunting 和 debug log 决定类型。
- 文件注释明确说明 `FrameAnalysisContext` 在 CPU-bound 游戏中有额外性能成本，并且该选择需要重启游戏才能改变。
- `src/DirectX11/HackerContext.cpp:775` 的 `BeforeDraw` 只在 Hunting 开启时执行 visited shader、IB/VB、选择、统计和相关锁操作。
- `src/DirectX11/HackerContext.cpp:920` 在 `fix_enabled` 关闭时跳过正常 Mod 的 Draw override 工作。
- `src/DirectX11/HackerContext.cpp:1486` 的 IA 绑定只在 Hunting 或 region hash 功能需要时计算和保存额外 Hash/绑定信息。

这形成了两个层面的裁剪：

1. 对象级裁剪：普通运行不创建完整 Frame Analysis wrapper。
2. 调用级裁剪：Draw 和绑定 API 依据功能位跳过不需要的状态、Hash、集合和锁。

### 3.2 DX12 当前基础

DX12 已有可继续利用的基础：

- `DX12HotPathState::SourceState` 汇总 Hunting、Frame Analysis、Shader Dump 和各种 Mod 能力。
- `HookWorkFlags`、`skipAll`、`skipBindings` 和 `skipGraphicsBindings` 提供热路径快速判断。
- `NeedsBindingEventRecording()` 已把完整 binding event 记录限制在 Frame Analysis/Shader Dump。
- Draw/Dispatch hook 已有 resident fast reject 和 feature mask。

当前差距不是“完全没有模式裁剪”，而是普通 Mod、Hunting 和 Capture 仍共享部分 descriptor、resource、wrapper 与生命周期设施，且各 API 是否必须记录的策略分散在多个模块。

### 3.3 详细修改步骤

#### 步骤 1：建立统一的运行模式能力矩阵

在 `DX12HotPathState::SourceState` 附近定义明确的 capability，而不是继续增加互相重叠的布尔判断。至少区分：

| 能力 | 普通 Mod | Hunting | Frame Analysis/Shader Dump |
|---|---:|---:|---:|
| 当前 descriptor 值 | 按 Mod 依赖启用 | 启用 | 启用 |
| descriptor 历史/epoch | 关闭 | 按浏览需求 | 启用 |
| command-list resident state | 按 Mod 依赖 | 启用 | 启用 |
| binding event ledger | 关闭 | 默认关闭 | 启用 |
| visited shader/resource 集合 | 关闭 | 启用 | 按导出需求 |
| 全量资源 metadata | 按匹配依赖 | 启用 | 启用 |
| Draw 最终匹配 | 启用 | 启用 | 启用并记录 |

能力位应回答“某类数据是否需要维护”，而不仅是“某个 UI 模式是否开启”。例如普通 Mod 的 descriptor replacement 仍需要当前 descriptor shadow，不能因为 Capture 关闭就一并禁用。

#### 步骤 2：给所有高频入口指定唯一策略函数

为下列入口分别定义一个只读取已发布 flags 的内联策略函数：

- `CreateConstantBufferView/CreateShaderResourceView/CreateUnorderedAccessView`
- `CopyDescriptors/CopyDescriptorsSimple`
- `SetDescriptorHeaps` 和 graphics/compute root table
- IA vertex/index buffer 绑定
- Draw/DrawIndexed/Dispatch
- resource Map/Unmap/Copy/Resolve/UAV write 边界

高频入口不得临时读取配置容器、获取配置锁或重新推导整个模式。Present/reload 路径负责构建 flags，录制路径只消费 flags。

#### 步骤 3：拆分“当前值”和“历史事件”所有权

将 descriptor/binding 数据明确分成：

- Current state：普通 Mod 匹配所需的最新值，允许覆盖旧值。
- Historical state：Capture/Shader Dump 所需的 immutable snapshot、epoch 和 event sequence。

普通 Mod 路径只更新 Current state。只有历史能力位开启时才创建 event、增加 capture 引用、进入 worker queue 或维护 epoch history。

#### 步骤 4：把诊断专用容器改成按需生命周期

Hunting/Capture 开始时创建或清空 visited/event/history 容器；结束时停止生产新事件，等待已有 sequence 完成，再释放历史数据。不能在模式关闭后继续维持无界历史，也不能在 worker 仍引用事件时直接清空。

#### 步骤 5：验证关闭模式的零工作路径

增加 Release Timing 计数，分别记录：

- 普通 Mod 下创建的 binding events 数量，目标为 0。
- 普通 Mod 下 descriptor history/epoch 写入数量，目标为 0。
- Hunting 关闭时 visited 集合写入数量，目标为 0。
- 无 descriptor 类 Mod 时 descriptor record fast-skip 数量和误入慢路径数量。

### 3.4 正确性测试

- 普通 TextureOverride、ShaderOverride、IB/VB replacement 不得因关闭 Capture 数据而失效。
- 运行中开启/关闭 Hunting 后，旧 visited 数据不能混入新 session。
- Frame Analysis 开始的第一帧必须具有完整事件，不得因为 flags 在 Present 后发布而漏掉开头。
- Capture 结束必须 drain 到目标 sequence，不能通过丢事件来缩短等待。
- 多 swapchain 和 DLSS Frame Generation 只允许 primary game swapchain 推进 Mod/Capture epoch。

## 4. DX11 架构二：绑定时保存状态并按相关性提前拒绝

### 4.1 DX11 现有证据

DX11 并非在 Draw 时通过 `Get*` API 重建全部绑定状态，而是在绑定调用时保存必要信息：

- `src/DirectX11/HackerContext.cpp:2256` 的 `SetShader` 总是保存当前 shader handle。
- 只有存在 ShaderOverride、shader regex 或 Hunting 时才查询 shader Hash；注释明确指出该查询有可测 CPU 成本。
- `src/DirectX11/HackerContext.cpp:1472` 的 `IASetVertexBuffers` 在相关功能开启时保存 VB Hash、buffer、offset 和 stride。
- `src/DirectX11/HackerContext.cpp:3049` 的 `IASetIndexBuffer` 保存当前 IB Hash 和原始绑定，供后续 Draw 使用。
- `BeforeDraw` 在 `fix_enabled` 关闭时提前退出；ShaderOverride map 为空时不会遍历 shader override。

DX11 仍会在 `BeforeDraw` 查找 ShaderOverride 和运行命令，因此这里迁移的是“resident state + relevance gating”，不是声称 DX11 已有完整 final decision cache。

### 4.2 DX12 当前基础

DX12 的 `DX12SimulateContext` 已保存 PSO、root signature、descriptor heaps/tables、IA views 和多个 revision。当前主要问题是命中 Mod 候选的 Draw 仍可能经过多层 dependency key、Hash、candidate 和 command plan 判断。

### 4.3 详细修改步骤

#### 步骤 1：列出每类 Mod 的精确状态依赖

为每个已编译 Mod plan 生成 dependency mask，例如：

- Shader-only：PSO + reload generation。
- IB replacement：PSO（如果有 match 条件）+ IB view + resource content token + Draw index 参数。
- 指定 VB slot：目标 slot view + resource token + stride/offset + Draw vertex 参数。
- descriptor slot replacement：root signature + heap identity + root table + slot identity + resource/descriptor generation。
- PreSkin：compute PSO + 精确 root parameters/ranges + 输入 tokens + 输出 UAV。

没有被任何 plan 引用的状态变化不得使所有 Draw decision 失效。

#### 步骤 2：在绑定 API 更新 resident state 和窄 revision

各 setter 只更新自己负责的字段：

- `SetPipelineState` 更新 PSO revision。
- root signature 更新对应 graphics/compute layout revision，并使受影响 root state 失效。
- `SetDescriptorHeaps` 更新 heap identity revision。
- root descriptor table 只更新对应 root parameter revision。
- IA setter 只更新变化的 slot revision。

值未变化时不得推进 revision。比较时使用 canonical/native identity，避免 wrapper pointer 差异造成假变化。

#### 步骤 3：在状态变化时完成便宜的相关性过滤

绑定 API 不应运行完整命令解释器，但可以完成稳定、便宜的过滤：

- 当前 PSO 是否存在任何 ShaderOverride/PreSkin/descriptor/IA plan。
- 当前 root signature 是否包含 plan 需要的 parameter/range。
- 当前 IA slot 是否属于配置目标。
- 当前 heap/table 是否可能覆盖目标 descriptor slot。

过滤结果写入 command-list resident state。Draw 先检查该结果，无候选时直接转发 original Draw。

#### 步骤 4：逐步引入 revision 命中快路径

这是对 DX11 resident-state 思路的 DX12 适配，不等同于 DX11 已有实现：

1. 首先缓存“确定无候选”的 negative result。
2. 再缓存只依赖 PSO 的 shader decision。
3. 再覆盖单个 IB/VB slot 的精确 token decision。
4. 最后才考虑 descriptor 和混合 command plan。

每增加一类缓存，都必须保存完整 dependency revision 集合；任何无法证明完整的情况回退现有解释路径。

#### 步骤 5：处理 DX12 特有边界

- `Reset`：清空 resident state，并推进 command-list state generation。
- `ExecuteBundle`：解析 bundle 导出的最终状态，无法解析时使相关 revision 保守失效。
- reload：推进 reload generation，不扫描全部 command list。
- command list 重复 Execute：decision 必须只依赖录制时已经固定的状态和安全的资源 publication。
- 并行录制：每个 command list 独立保存状态，不依赖“当前全局 PSO”。

### 4.4 性能验收

- 统计 Draw 总数、无候选 fast-forward、resident negative hit、完整 fallback 次数。
- 分别测量 `DrawHook calls/frame`、`us/call` 和父级 CPU frame time。
- 目标不是只让某个 helper 下降，而是减少 Draw 进入完整 dependency/matching 路径的比例。
- 使用相同场景、相同配置、相同帧时间 cohort 比较；至少采集数百帧。

## 5. DX11 架构三：在资源写入路径维护内容身份

### 5.1 DX11 现有证据

DX11 把资源 Hash 作为资源 metadata 保存，普通查询直接返回已存结果：

- `src/DirectX11/ResourceHash.cpp:844` 的 `GetResourceHash` 读取 `ResourceHandleInfo::hash`，不会在每个 Draw 上重新 Hash 整个资源。
- `src/DirectX11/HackerContext.cpp:1381` 的 Unmap 路径通过 CPU 数据更新资源内容缓存和 Hash。
- `src/DirectX11/HackerContext.cpp:1889` 的 `CopySubresourceRegion` 标记目标污染，并在满足条件时传播或更新 Hash。
- `src/DirectX11/HackerContext.cpp:1912` 的 `CopyResource` 传播源资源 Hash，并清理目标 region cache。
- `src/DirectX11/ResourceHash.cpp:1178` 的 `PropagateResourceHash` 在源、目标数据 Hash 不同时才更新目标 Hash。
- `MapTrackResourceHashUpdate` 根据 Hunting 和 `track_texture_updates` 决定是否追踪写入。

DX11 的实现并不完美，也不是所有 GPU 写入都能得到精确新 Hash；但架构原则明确：写入路径负责发布资源内容变化，读取/匹配路径消费已经发布的身份。

### 5.2 DX12 迁移目标

DX12 不应在每个 Draw 上重新构造完整 IA/resource dependency key。每个资源应公开稳定 publication：

```text
ResourcePublication {
    canonical resource identity
    lifetime generation
    content generation
    optional known hash / hash state
}
```

绑定状态保存 publication pointer/token 和当时观察到的 generations；Draw 通过少量比较判断缓存是否仍有效。

### 5.3 详细修改步骤

#### 步骤 1：统一资源 publication 所有权

为每个 canonical resource 建立地址稳定的 publication。资源从 registry 移除后，只要任何 command-list cache 仍可能读取 publication，其内存就不能立即复用。

明确区分：

- `lifetimeGeneration`：对象释放、地址复用、placed resource 重建时变化。
- `contentGeneration`：同一资源内容可能变化时推进。
- `metadataGeneration`：desc、GPUVA 映射或兼容 metadata 变化时推进；如果对象语义保证不变，可合并进 lifetime。

#### 步骤 2：建立完整写入 API 清单

逐项审计并标记 content generation：

- CPU `Map/Unmap`，包括 persistent mapping 和写入范围未知的情况。
- `CopyBufferRegion`、`CopyTextureRegion`、`CopyResource`。
- `ResolveSubresource`/`ResolveSubresourceRegion`。
- `ClearUnorderedAccessView*`、`ClearRenderTargetView`、`ClearDepthStencilView`。
- UAV write dispatch/draw。
- Render target/depth output draw。
- Raytracing/Work Graph 等可写资源路径（如果支持相应接口）。
- 内部 replacement upload/copy/resize。
- aliasing barrier 和 placed resource 重新解释。

无法证明写入范围或结果时，宁可推进整个资源 generation，也不能保留错误的 exact-negative cache。

#### 步骤 3：为 Copy 建立传播规则

DX11 可以直接传播部分 Hash；DX12 应区分：

- 完整资源 Copy 且源 Hash 已知：目标可继承已知内容身份或 Hash，并推进 generation。
- 部分 Copy：目标推进 generation；只在已有可靠 range model 时更新局部缓存。
- GPU 写入结果未知：Hash 状态改为 dirty/unknown，不在 Draw 中同步等待 GPU 重新计算。
- 内部已知上传：可直接发布预先计算的 Hash。

#### 步骤 4：将 Draw dependency 改成 generation 比较

IA/descriptor 绑定解析出资源后，保存：

- publication identity。
- lifetime generation。
- content generation。
- view offset/size/stride/format。

Draw 命中缓存时先比较 generations。全部一致才复用 Hash/match decision；不一致时进入现有精确解析路径并刷新缓存。

#### 步骤 5：处理 GPU 写入完成时序

content generation 表示“逻辑内容已经被命令流安排改变”，不要求 CPU 等待 GPU 完成。录制 Copy/Draw/Dispatch 时即可推进录制侧 token，但必须保证同一 command list 内后续 Draw 看见正确的顺序版本。

禁止为了得到新 Hash 在普通 Mod Draw 路径执行 fence wait 或 readback。需要真实 GPU 结果的功能只能异步计算，或者在 Capture/Hunting 模式显式接受等待成本。

### 5.4 正确性测试

- 同一 resource pointer 释放后重用不能命中旧缓存。
- 完整 Copy 后目标匹配源 TextureOverride。
- 部分 Copy 后旧 negative/positive result 必须失效。
- persistent mapped upload buffer 多次写入不能永远保持旧 generation。
- UAV 写入后作为 IB/VB/SRV 使用时必须失效旧内容 decision。
- 同一 command list 内“写入后读取”和跨 queue fence 后读取都必须按顺序观察 generation。
- 内部 replacement 操作不能递归污染游戏资源 publication。

## 6. DX11 架构四：分层区域 Hash 缓存和精确失效

### 6.1 DX11 现有证据

DX11 对共享大 Buffer 的 region hash 使用多层结构：

- L1：`ResourceHandleInfo` 中的 CPU-side buffer snapshot。
- L2：每资源、按 offset/size 索引的 `RegionHashesCache`。
- L3：每帧全局的 `region_hashes_global_cache`，避免在同一帧重复查找大资源表。
- `src/DirectX11/ResourceHash.cpp:2153` 的 `GetRegionHash` 先无锁查询 L3，再查询每资源 L2，最后才计算 CRC。
- `src/DirectX11/ResourceHash.cpp:1981` 在写入区域时只失效受影响 pages。
- `src/DirectX11/HackerDXGI.cpp:568` 在 Present 边界清理每帧 L3 cache。

该架构的重点不是固定的“三层”命名，而是：最热查询使用小而连续的近端缓存，昂贵 metadata 查询退到后层，写入负责精确失效。

### 6.2 DX12 详细修改步骤

#### 步骤 1：先统一 key 和失效源

DX12 IA/descriptor/resource cache key 至少包含：

- canonical resource/publication identity。
- lifetime/content generation。
- view offset、size、stride、format。
- 与结果有关的 Draw 参数子集。
- reload generation。

不要先扩充 cache ways；key 不完整时，容量越大只会扩大错误命中窗口。

#### 步骤 2：保留小型 command-list resident cache

最热 L1 应放在 `DX12SimulateContext` 或紧邻 command-list state：

- 当前 PSO decision。
- 当前 IB exact dependency/match。
- 配置真正引用的 VB slots。
- 当前 root table/descriptor 的解析结果。

该层不使用进程全局锁，Reset 时整体失效。

#### 步骤 3：使用稳定 publication 作为共享 L2

跨 command list 可共享的内容只放在资源 publication 或 immutable compiled configuration 中，例如已知资源 Hash、PSO static decision、root signature layout。不得共享调用方特有的 root table 和 Draw 参数状态。

#### 步骤 4：谨慎使用每帧 L3

只有日志证明多个 command list 在同一帧重复查询相同 exact key 时，才增加 per-frame/TLS cache。帧边界由 primary game Present 推进；Frame Generation 的附属 Present 不能提前清空。

多线程录制时优先 TLS/per-worker cache，避免复刻 DX11 单 immediate context 下可接受、在 DX12 下会串行化的全局 critical section。

#### 步骤 5：按依赖失效而不是全表清空

- resource write：content generation 自然使相关 entry 失效。
- resource release：lifetime generation 失效。
- descriptor overwrite/copy：slot/table revision 失效。
- reload：reload generation 失效。
- Reset：command-list L1 失效。
- Present：只清理明确为 per-frame 的 L3，不清理可跨帧的资源 publication。

### 6.3 性能与容量验收

每层记录 hit、miss、eviction、fallback 和锁等待时间。新增缓存必须同时证明：

- 父级 Draw/Dispatch CPU 时间下降。
- miss 路径没有因为更复杂的 key 和维护成本回退。
- 内存容量随资源/command list 生命周期有界。
- 多线程录制时没有新增共享写热点。

## 7. 推荐实施顺序

### 阶段 A：只测量，不改变语义

1. 建立普通 Mod、Hunting、Capture capability 矩阵。
2. 给 descriptor current/history、binding events、visited 集合增加独立计数。
3. 给资源 publication 和 Draw dependency cache 增加 hit/miss/invalid reason。
4. 采集普通 Mod、Hunting、Capture 三套基线。

完成条件：能够回答普通 Mod 每帧究竟维护了多少诊断专用数据，以及 Draw fallback 分别由哪个 generation/revision 触发。

### 阶段 B：迁移 DX11 的模式隔离

1. 关闭普通 Mod 下的 binding history/event ledger。
2. 关闭 Hunting 未启用时的 visited 数据生产。
3. 保留普通 Mod 实际需要的 descriptor current shadow。
4. 完成动态启停和 capture session 回归。

完成条件：功能矩阵通过，普通 Mod 的诊断事件计数为 0，CPU 父级路径有可测下降。

### 阶段 C：完善资源写入侧 publication

1. 固化 lifetime/content generation 语义。
2. 审计全部写入 API。
3. 先覆盖 IA buffer，再覆盖 descriptor 指向的 buffer/texture。
4. 将 exact-negative cache 失效改为 generation 驱动。

完成条件：不进行普通路径 GPU readback/fence wait，资源更新回归全部通过，Draw dependency build 次数下降。

### 阶段 D：绑定状态相关性和 revision 快路径

1. 为 compiled config 生成 dependency masks。
2. setter 更新窄 revision 和 candidate state。
3. 先缓存无候选结果，再逐类缓存简单 positive decision。
4. Reset/Bundle/reload 使用 generation 统一失效。

完成条件：大多数无关 Draw 只执行 flags + resident candidate 检查；复杂 fallback 保留且正确。

### 阶段 E：基于数据决定缓存结构

1. 检查 exact-key 重复率和跨 command-list 共享率。
2. 只为高命中工作负载增加 TLS/per-frame cache。
3. 不默认增加 descriptor slot locks、memo ways 或 per-slot atomic。
4. 对 descriptor table snapshot 单独做原型和回归，不与资源 generation 改动混合。

## 8. 验收方法

### 8.1 功能矩阵

至少覆盖：

- ShaderOverride replace/skip/pre/post。
- TextureOverride resource Hash、descriptor Hash 和显式 shader slot。
- IB/VB full Hash 与 region Hash。
- Map/Unmap、完整 Copy、部分 Copy、UAV 写入后的匹配。
- PreSkin 输入、输出 resize、恢复和延迟回收。
- Reset、ExecuteBundle、command list 重复提交。
- Hunting 浏览、Frame Analysis、Shader Dump。
- reload 和 UI 动态切换。
- 多 swapchain、DLSS Frame Generation。

### 8.2 性能指标

必须同时记录：

- CPU frame wall time 和各录制线程 CPU time。
- Draw/Dispatch hook calls、total time、us/call。
- descriptor create/copy record time。
- resource publication 更新次数。
- cache hit/miss/fallback/eviction。
- SRW lock/critical section 等待。
- ExecuteCommandLists 时间和 GPU queue idle gap。
- Present wait 与 frame-latency wait。

### 8.3 判定原则

- 不用 Capture/Hunting 帧代表普通 Mod 性能。
- 不把游戏原始 D3D12 API、GPU execution 或正常 Present wait 算作 Mod CPU 优化收益。
- 不只比较子函数；父级 Draw、descriptor copy 和整帧必须同步下降。
- 不通过 CPU/GPU fence 等待换取缓存正确性。
- 任何性能提升都必须在功能矩阵、对象生命周期和多线程录制场景下成立。

## 9. 归属总结

可明确归因于 DX11 既有架构的内容：

1. 普通 Context 与 Frame Analysis Context 的对象级隔离。
2. Hunting/统计/visited 工作受功能开关保护。
3. 在 shader、IA 等绑定 API 保存当前状态，并在无相关配置时跳过 Hash/lookup。
4. 资源创建或写入时维护 Hash，Copy 时传播或污染目标身份。
5. 普通匹配读取已经保存的资源 Hash，而不是每 Draw 重读整个资源。
6. Region Hash 使用近端缓存、每资源缓存和每帧缓存，并按写入范围失效。
7. 在 primary Present 边界集中执行帧操作和清理真正的 per-frame cache。

DX12 迁移中新增但不是 DX11 原样已有的内容：

1. 每 command-list resident state 和 revision，而不是单 immediate context current state。
2. lifetime/content/reload/descriptor generations 的组合失效。
3. Reset、ExecuteBundle、多线程录制和 command list 重复提交边界。
4. 当前 descriptor shadow 与 Capture 历史的拆分。
5. 用 generation 比较逐步形成 negative/positive decision 快路径。

这一区分必须保留：前一组是已经被 DX11 实践验证的架构原则，后一组是把这些原则正确适配到 DX12 显式、多线程、可提前录制模型所必需的工程工作。
