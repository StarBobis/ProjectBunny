# DX12 Frame Latency Analysis Notes

本文档记录 DX12 帧延迟问题的阶段性结论。随着后续讨论、日志分析和代码验证，结论会持续修正。

## 当前问题边界

- 目标问题：DX12 部分出现约 500ms 级别帧延迟，主要时间在 CPU 帧间隔中。
- 不作为主要优化目标：
  - F8 Dump 路径，因为正常游玩 Mod 不触发。
  - 窗口/Overlay 绘制，因为约 0.5ms 到 2ms，收益相对小。
  - 日志性能，因为日志是排查必须项，Release 下不产生日志。
  - GPU 负载，因为实际 GPU 渲染约 0.6ms。
  - 内容 Hash 机制本身，因为内容 Hash 是 Mod 生效必要条件。

## Mod 生效所需的最小信息

正常 Mod 生效不是完全不需要状态维护，而是需要维护一个最小闭环：

- 当前 draw/dispatch 的身份：
  - PSO / shader identity。
  - VS / PS / CS 等 shader hash 或等价标识。
- 当前绑定资源的可识别身份：
  - descriptor 类型，例如 CBV / SRV / UAV / RTV / DSV。
  - slot / space / range / root parameter 信息。
  - resource pointer 或 canonical resource。
  - resource desc。
  - 必要时的内容 hash。
- IA 输入信息：
  - index buffer view。
  - vertex buffer view。
  - stride / format / offset / size。
  - draw 参数。
  - 对应 buffer 内容 hash 或区域 hash。
- override 决策结果：
  - skip draw / dispatch。
  - 替换 IA buffer。
  - 替换 SRV / CBV / UAV。
  - present replacement。
  - pre-skin UAV 相关处理。
  - shader override。
- 可恢复的原始状态：
  - 原始 descriptor / root descriptor。
  - 原始 IA binding。
  - 原始 PSO / root signature 相关状态。
  - command list 当前状态。

最小闭环可以概括为：

```text
PSO/shader identity
+ 当前 draw/dispatch 绑定快照
+ 必要资源内容 hash
+ override decision
+ 状态恢复信息
```

## DX11 与 DX12 的差异

DX11 不是不需要这些信息，而是 D3D11 runtime / driver 已经维护了大量当前 context 状态。

DX11 中，wrapper 可以围绕 immediate context 读取或镜像：

- 当前 shader。
- 当前 SRV / CBV / UAV / RTV。
- 当前 vertex/index buffer。
- 当前 input layout / topology。

DX12 中，command list 录制时没有 DX11 风格可查询 context：

- descriptor table 传的是 GPU descriptor handle，不是资源对象。
- root descriptor 可能只是 GPU virtual address。
- IA buffer view 也是 GPU virtual address。
- PSO 把 shader/input layout 等信息打包，运行时查询能力弱。
- command list 可以录制、关闭、提交、复用，wrapper 必须自己维护影子上下文。

因此 DX12 需要维护影子状态，但不代表必须在正常 Mod 热路径维护 Dump/Hunting 级别的完整世界模型。

## 当前维护路径

### 帧边界

入口：

- `DXGIHookRuntime::Present`
- `DXGIHookRuntime::Present1`

涉及：

- `ConsumeUiActionsForFrame`
- `DX12Profiling::BeginFrame`
- `DX12Profiling::EndFrame`
- `DX12HuntOnPresent`
- `DX12ModBeginFrame`
- `DX12HotPathState::Update`
- `CompleteCaptureOrBeginNext`

维护成本：

- 每帧刷新 hot path flags。
- 处理 Mod runtime 状态。
- 混有 capture / hunt / profiling bookkeeping。

### ExecuteCommandLists 提交

入口：

- `DX12WrapperRuntimeUtils::ExecuteCommandLists`
- `DX12CommandQueueExecutionFlow::Execute`

涉及：

- `UnwrapCommandList`
- `DX12HotPathState::RebuildHookWorkFlags`
- `DX12ModNotifyCommandListsSubmitted`
- `RegisterSubmittedCommandListDirect`
- `DX12CommandListLifecycle::Register`
- `DX12HuntPublishCommandList`
- `DX12BindingTracker::SubmitCommandList`

维护成本：

- 每次提交都可能执行 unwrap、flags 刷新、mod 通知、lifecycle 注册。
- `RegisterSubmittedCommandListDirect` 会 `QueryInterface`。
- capture session 下还会复制/提交 binding events。

### Command List 当前状态

入口：

- `SetPipelineState`
- `SetGraphicsRootSignature`
- `SetComputeRootSignature`
- `SetGraphicsRootDescriptorTable`
- `SetComputeRootDescriptorTable`
- `IASetIndexBuffer`
- `IASetVertexBuffers`
- `ResourceBarrier`
- `ExecuteIndirect`

涉及：

- `DX12CommandListRuntime::RememberIndexBuffer`
- `DX12CommandListRuntime::RememberVertexBuffers`
- `DX12CommandListRuntime::BumpIaEpoch`
- `DX12CommandListRuntime::BumpIaContentEpoch`
- `DX12CommandListStateCapture`

维护成本：

- 每次绑定 API 都要查 runtime state、比较变化、更新 IA/PSO/root 状态。
- 这些状态对 Mod 必要，但目前部分路径还会同时服务 BindingTracker、capture、hunt、pre-skin。

### Draw / Dispatch 决策

入口：

- `DX12DrawHookFlow::ExecuteHook`
- `DX12DispatchHookFlow::Execute`

涉及：

- `DX12CommandListRuntime::EnsureDrawDecision`
- `DX12CommandListRuntime::BuildDrawDecision`
- `DX12ModCompileDrawDecision`
- `DX12CommandListRuntime::BuildDispatchDecision`

维护成本：

- 每个 draw/dispatch 判断 flags、PSO、IA hash、descriptor match、pre-skin probe、shader override skip。
- 已有 fast reject 和 decision cache。
- 如果上游 epoch/generation 频繁变化，缓存价值会下降。

### IA Hash / 替换执行

入口：

- `DX12IaReplacementExecutor::HandleDrawOverrides`

涉及：

- `BuildIaHashStateCacheKeyForTargetsCached`
- `BuildIaHashStateForTargets`
- `DX12IaHashStateCacheUtils::TryLoad`
- `DX12IaHashStateCacheUtils::Store`
- `DX12DirectIaFastRejectCacheUtils`
- `DX12DirectIaProbeCacheUtils`
- `DX12ModHasAnyDirectIaTextureOverrideCandidate`
- `DX12IaReplacementBindingExecutor::Apply`
- `DX12IaReplacementBindingExecutor::Restore`
- `DX12ModApplyDrawSrvReplacement`
- `DX12ModRestoreDrawSrvReplacement`

维护成本：

- GPU VA 解析。
- buffer 内容 hash。
- cache key 构建。
- 多个小缓存查找。
- 替换后恢复状态。

### Resource / Descriptor 全局索引

入口：

- `DX12ResourceTrackerHookRuntime::RecordCreatedResource`
- descriptor create/copy 相关 hooks。
- resource release tracker。

涉及：

- `RecordResource`
- `CanonicalResource`
- `FillResourcePointerInfo`
- `CaptureDescriptorResourcePointer`
- `ResolveBufferByGpuVa`
- `InvalidateResourceLocked`
- `InvalidateResourceRangeChangeLocked`
- `DX12BufferResolveCacheUtils::PurgeRangeLocked`

维护成本：

- 维护 resource pointer / canonical pointer / desc / GPU VA range / resolve cache。
- release 时清理 hash cache、GPU VA index、resolve cache generation。
- 当前日志中 liveResources 约 18k，并且同一帧附近有大量 `DX12ResourceInvalidated` / `BufferResolveCacheInvalidated`，这很可能是 500ms 级 CPU 延迟的主要疑点。

## 第二层成本漏进第一层的疑点

这里的“第一层”指正常 Mod 生效热路径，“第二层”指 Dump / Hunting / 诊断 / 全量 frame binding 等扩展状态。

### 1. Resource lifecycle 全量维护

当前 `ShouldRecordCreatedResource` 在非 Dump/Hunting 情况下仍会记录 buffer 和 texture。

可能导致：

- `gResources`
- `gResourceByPtr`
- `gBufferResourceByGpuVa`
- release tracker
- `InvalidateResourceLocked`
- `DX12InvalidateTextureHash`
- `DX12InvalidateBufferContent`
- `InvalidateResourceRangeChangeLocked`
- `PurgeRangeLocked`

这些路径接近完整资源世界模型。它们对 IA/model/texture replacement 可能必要，但对纯 shader bytecode override 不必要。

### 2. Descriptor 创建/复制维护偏宽

`CopyDescriptors` 已有 `NeedsDescriptorTracking()` guard，这是好的。

但部分 descriptor create wrapper 仍会先 unwrap / prepare / enter record 阶段。即使内部再判断，也可能已经付出一部分成本。

可以考虑在 wrapper 层更早用变量快跳。

### 3. Active shader override 与 IA state tracking 耦合偏宽

`IASetIndexBuffer` / `IASetVertexBuffers` 路径中，只要 `DX12ModHasActiveShaderOverrides()` 为真，就可能维护 IA state。

但很多 shader override 只需要 shader bytecode 替换或 PSO 决策，不需要 IA hash / 模型替换 / texture override。

应考虑拆出更精确变量，例如：

- `DX12ModNeedsIaStateTracking()`
- `DX12ModNeedsGpuVaResolveTracking()`

### 4. ExecuteCommandLists 多系统广播

提交路径中同时涉及 lifecycle、hunt publish、binding submit。

`DX12BindingTracker::SubmitCommandList` 内部在没有 capture session 时会 return，但调用前仍可能发生 `QueryInterface` 和其他系统广播。

可以考虑将正常 Mod、Hunting、Capture 的提交流程进一步拆开。

### 5. Resource release invalidation 过全局

resource release 时清理 texture hash、buffer content、GPU VA index、resolve cache generation。

这对模型/贴图替换可能必要，但对不需要资源内容和 GPU VA resolve 的 Mod 类型不必要。

## 安全优化原则

不能简单写成：

```cpp
if (!Dump && !Hunting)
    skip_tracking;
```

因为正常 Mod 模式仍可能需要模型和贴图替换。

安全边界应当是：

```cpp
if (!Dump && !Hunting && !ModNeedsThisExactTracking)
    skip_this_tracking;
```

也就是说，开关必须由实际 ini / runtime Mod 能力驱动，而不是只看是否处于 Dump/Hunting。

## 建议新增或细化的变量

建议在现有 `DX12HotPathState` / `DX12ResourceTrackingPolicyUtils` 基础上继续拆分：

- `needsPsoStateTracking`
- `needsIaStateTracking`
- `needsDescriptorShadowTracking`
- `needsGpuVaResolveTracking`
- `needsTextureContentTracking`

保守映射：

- 只有 shader bytecode override：
  - 需要 PSO/shader tracking。
  - 不需要 IA hash / GPU VA resolve / descriptor shadow。
- 存在模型或 IA buffer override：
  - 需要 IA state tracking。
  - 需要 GPU VA resolve。
  - 需要 buffer content hash invalidation。
- 存在 texture override / SRV replacement：
  - 需要 descriptor shadow tracking。
  - 需要 resource identity。
  - 需要 texture content hash invalidation。
- 存在 shader-triggered texture override：
  - 需要 PSO/shader tracking。
  - 需要 descriptor tracking。
- 存在 pre-skin：
  - 需要 compute binding / UAV tracking。
- Dump / Hunting / Shader Dump：
  - 全部打开。

## 当前结论

DX12 的状态维护不是可以整体移除的问题。真正的问题是：正常 Mod 热路径可能维护了接近 Dump/Hunting 的完整世界模型。

当前最可疑的主热点是 resource lifecycle / GPU VA resolve cache / resource invalidation 风暴。日志已观察到 liveResources 约 18k，并在同一帧附近出现大量资源失效与 BufferResolveCache invalidation。

预期优化方向不是删除 Hash 或删除 Mod 必要信息，而是：

- 让纯 shader override 不承担模型/贴图替换的状态维护成本。
- 让无 texture override 的配置不维护 descriptor shadow。
- 让无 IA/model override 的配置不维护 GPU VA resolve 与 IA hash 所需索引。
- Dump/Hunting 打开时恢复完整维护。

粗略预期：如果能把 resource invalidation / descriptor shadow / IA tracking 按 Mod 能力精确收缩，正常 Mod 模式 CPU 热点可能从数百 ms 级下降一个数量级。该数字目前是方向性估计，需后续用日志验证。

### 2026-07-13 下一步方向：IA resolve 贴近 wrapper current state

submit 阶段改为 wrapper direct-state 后，下一步应把资源缓存也尽量贴近 wrapper 当前状态。

当前 IA state 只保存 VB/IB view，draw 构建 IA hash key 时仍通过 GPU VA 走全局 `DX12ResolveBufferResourceByGpuVa`。这会让大量 draw 重复执行 GPU VA -> resource summary 的全局查找。

计划改法：

- `DX12ActiveIaState` 增加 index/vertex buffer 的 resolved summary 缓存。
- `IASetIndexBuffer` / `IASetVertexBuffers` 更新 view 时，同步尝试解析一次 resource summary。
- draw 构建 IA hash key 时，若 cached view 与当前 view 一致且 summary 带 `resourceGeneration`，直接复用缓存。
- 如果缓存不存在或不匹配，保留原 `DX12ResolveBufferResourceByGpuVa` 路径。

安全边界：

- 不移除内容 hash。
- 不改变模型替换匹配逻辑。
- 不把 unresolved 结果当作 resolved 使用。
- resource release / GPU VA reuse 仍依赖 `resourceGeneration`、content generation 和原 fallback 路径保证保守失效。

预期效果：

- 大量 draw 共用相同 VB/IB 时，IA resolve 查找从 draw 次数级下降到 IASet 次数级。
- 预计 IA resolve/hash key 准备阶段降低 30% 到 70%。
- 总帧延迟预计提升 5% 到 20%，实际取决于当前帧中 IA resolve 占比。

## 明显设计问题

以下是当前阶段认为比较明显的设计错误或设计债。

### 1. 正常 Mod 状态模型和 Dump/Hunting 世界模型没有硬隔离

当前代码已经在构建 DX12 的 DX11-style 可查询状态模型，但 `CurrentState`、`Resolve`、`History/Export` 三层边界不够硬。

理想分层：

- CurrentState：正常 Mod 热路径必备，只维护当前 command list 当前状态。
- Resolve：只有当前 Mod 功能需要时，才解析 descriptor handle / GPU VA 到 resource/hash。
- History/Export：只有 Dump/Hunting/Capture 才记录全量历史事件和导出数据。

当前问题：第二层、第三层的成本会漏到第一层，导致正常 Mod 模式承担接近 Dump/Hunting 的维护量。

### 2. 用过宽的功能标志驱动过重的状态维护

典型例子是 `DX12ModHasActiveShaderOverrides()` 会影响 IA state tracking。

这把“有 shader override”扩大解释成“可能需要 IA/model/texture 相关信息”。但纯 shader bytecode 替换并不需要 IA hash、GPU VA resolve 或 descriptor shadow。

正确方向：按能力拆成更窄的变量，例如：

- `DX12ModNeedsPsoStateTracking()`
- `DX12ModNeedsIaStateTracking()`
- `DX12ModNeedsDescriptorShadowTracking()`
- `DX12ModNeedsGpuVaResolveTracking()`
- `DX12ModNeedsTextureContentTracking()`

### 3. Resource tracker 以全局资源生命周期为中心，而不是以 Mod 查询需求为中心

现在资源创建和释放会维护 `gResources`、`gResourceByPtr`、`gBufferResourceByGpuVa`、release tracker、hash invalidation、resolve cache invalidation 等全局结构。

这对 Dump/Hunting 和部分模型/贴图替换有价值，但对纯 shader override 是明显过度维护。

设计问题不在于有 resource tracker，而在于它缺少“当前 Mod 是否真的需要 GPU VA/resource/hash resolve”的硬开关。

### 4. Release invalidation 是同步、全局、强传播的

resource release 时会触发多类清理：

- resource 表清理。
- GPU VA index 清理。
- texture hash invalidation。
- buffer content invalidation。
- resolve cache range purge。
- generation bump。

如果一帧内大量资源释放，这会形成资源失效风暴。日志中 liveResources 约 18k，并出现大量 `DX12ResourceInvalidated` / `BufferResolveCacheInvalidated`，说明这里很可能是 CPU 帧延迟主矛盾。

设计问题：释放事件被当成必须立即同步更新所有索引，而不是按当前启用的 Mod 能力做最小失效。

### 5. BindingTracker 的职责偏向 Dump/Hunting，但入口靠近正常热路径

`DX12BindingTracker` 更像 frame binding ledger / capture export 层。正常 Mod 需要的是当前状态，不是全量历史事件。

目前虽有 capture session guard，但 `ExecuteCommandLists` 和 draw 相关路径仍会接触 binding tracker 的判断或入口。

设计问题：History/Export 层没有完全下沉到 Dump/Hunting 激活后再接管。

### 6. ExecuteCommandLists 提交路径像总线广播

提交 command list 时同时处理：

- original submit。
- Mod notify。
- lifecycle register。
- hunt publish。
- binding submit。

这些动作属于不同功能层，却在同一提交路径串行出现。

设计问题：提交路径缺少按功能能力拆分的 fast path。正常 Mod 提交应尽量只做必要 lifecycle 和 Mod notify。

补充：代码并不是只有纯 Wrapper 模型。`GraphicsCommandListWrapper` 构造时会解析 pre-hook original function pointers，并注释说明这是为了避免 wrapper callback 再走 hooked vtable 造成二次拦截。这说明架构同时兼容 wrapper 入口和 vtable hook/original callback 入口。

`ExecuteCommandLists` 的 runtime helper 为了复用两套入口，把 command list 参数退化成 `ID3D12CommandList*`，提交后又通过 `QueryInterface(ID3D12GraphicsCommandList)` 恢复类型，再执行 lifecycle / hunt / binding submit。这属于典型的“早期已知身份，热路径晚期重新发现身份”的设计问题。

更理想的方向：

- wrapper 创建 command list 时就记录 native graphics command list identity。
- command list wrapper 持有 stable runtime/lifecycle state。
- submit 路径直接使用已知 summary/state，不再每次 QI。
- hook fallback 路径保留通用 QI，但 wrapper fast path 不走通用慢路径。

### ExecuteCommandLists 对齐 DX11 的具体改法

目标：把 DX12 submit 路径从“提交后重新 QI 发现 command list 身份”，改成 DX11 风格的“native pointer 直接查到 wrapper/runtime state”。

建议分阶段做，避免影响 Mod 生效。

#### 阶段 1：补一个 command list submit registry

在 command list wrapper 创建时注册：

```text
native ID3D12GraphicsCommandList* -> SubmitRecord
```

`SubmitRecord` 至少包含：

- native graphics command list pointer。
- `DX12CommandListRuntimeState*`。
- command list type。
- 是否来自 wrapper fast path。
- lifecycle 是否已注册。

`GraphicsCommandListWrapper` 构造时已经有 `mCommandList` 和 `mRuntimeState`，因此这里不需要 QI。

析构时 unregister。现有 `DX12CommandListRuntime::RegisterDirectState/UnregisterDirectState` 已经接近这个模型，可以优先复用或扩展，而不是另建一套重复 map。

#### 阶段 2：拆分 submit register 函数

当前：

```cpp
RegisterSubmittedCommandListDirect(IUnknown *commandList)
```

问题是参数太泛化，导致每次 submit 后都 QI。

建议新增：

```cpp
RegisterSubmittedGraphicsCommandList(
    ID3D12GraphicsCommandList *commandList,
    DX12CommandListRuntimeState *runtimeState)
```

职责：

- 不做 QI。
- lifecycle register 只在需要时做。
- Hunting 开启才 `DX12HuntPublishCommandList`。
- Capture/Dump session 开启才 `DX12BindingTracker::SubmitCommandList`。

旧的 `RegisterSubmittedCommandListDirect(IUnknown*)` 保留作为 hook fallback / unknown native path，内部可以先查 registry，查不到才 QI。

#### 阶段 3：Wrapper queue path 走 fast callback

`CommandQueueWrapper::ExecuteCommandLists` 是 wrapper 已知入口。

这里可以继续 unwrap native command lists 给 driver，但提交通知阶段不要走 `IUnknown* -> QI`。

可选实现方式：

1. 在 unwrap command list 时，同时查 registry 得到 `SubmitRecord*`。
2. `DX12CommandQueueExecutionFlow::Execute` 接收一个更具体的 submit callback。
3. original submit 后遍历 records，走 `RegisterSubmittedGraphicsCommandList`。

如果暂时不想改 `Execute` 签名，可以先在 `RegisterSubmittedCommandListDirect` 里做：

```text
if native pointer exists in SubmitRegistry:
    use cached graphics pointer/runtime state
else:
    fallback QI
```

这属于低侵入第一步，收益不如彻底拆签名，但风险小。

#### 阶段 4：按功能拆 submit 后动作

submit 后动作应拆成三类：

- Normal Mod：只做必要 lifecycle / mod notify。
- Hunting：额外 publish command list。
- Capture/Dump：额外 submit binding events。

不要在正常 Mod submit 路径无条件接触 BindingTracker/Hunt。

#### 阶段 5：Close 时生成 command list summary

后续可在 `CloseCommandList` 时记录轻量 summary：

- 是否包含 draw/dispatch。
- 是否绑定过候选 PSO。
- 是否可能需要 IA hash。
- 是否可能需要 descriptor resolve。
- 是否可能需要 pre-skin。

submit 时直接看 summary 决定是否需要进一步处理。这一步收益大，但改动面也更大，建议晚于 registry/QI 优化。

预期收益：

- submit 路径不再每次 QI command list，预计降低提交阶段 CPU 开销。
- wrapper fast path 和 hook fallback 分离，减少通用慢路径污染正常 Mod。
- 为后续 close-time summary 和按功能 submit 分流打基础。

风险控制：

- hook fallback 保留原逻辑，未知来源 command list 仍可 QI。
- wrapper path 只使用创建时已知的 native graphics command list，不改变 driver submit 内容。
- Hunting/Capture 功能开关打开时仍执行对应 publish/submit。

### 2026-07-13 实施记录：submit 阶段改为 wrapper direct-state

根据测试策略，第一版实现采用更激进的 wrapper-only submit 路径，而不是保守 fallback：

- `DX12CommandListRuntime` 新增 `GetDirectStatePtr`，只查询 `RegisterDirectState` 注册的 wrapper runtime state。
- `DX12WrapperRuntimeUtils::RegisterSubmittedCommandListDirect` 不再 `QueryInterface(ID3D12GraphicsCommandList)`。
- submit 阶段不再重复调用 `DX12CommandListLifecycle::Register`。
- submit 阶段只对 wrapper 已注册 command list 执行后续动作。
- Hunting 开启时才 `DX12HuntPublishCommandList`。
- Capture session 存在时才 `DX12BindingTracker::SubmitCommandList`。

这次改动把 submit 阶段从“晚期重新发现 command list 身份”改成“使用 wrapper 创建期已经注册的 native command list 身份”。

风险：

- 如果仍存在非 wrapper command list 进入 direct queue submit，它将不再通过 QI fallback 参与 Hunt/Capture submit 后处理。
- 正常 wrapper command list 的 driver submit 内容不变。

预期效果：

- `RegisterSubmittedCommandListDirect` 去掉每个 command list 一次 QI。
- submit 阶段去掉重复 lifecycle register。
- Hunting/Capture 未开启时，submit 后处理接近空操作。
- 预计 submit 注册阶段 CPU 开销下降 50% 到 90%；总帧提升取决于当前帧是否以 submit/QI/lifecycle 为热点。

### 7. 缓存设计被频繁 generation/epoch 失效削弱

代码中已有 draw decision、IA hash state、direct probe、fast reject 等缓存。但如果 resource invalidation、IA epoch、resolve generation 变化过于频繁，缓存会被不断打穿。

设计问题：缓存建了很多，但上游失效粒度太粗，导致缓存收益不稳定。

### 8. 缓存结构和缓存语义混在一起

缓存可以设计成 `map` / `unordered_map` / 分片 map，但容器类型只解决查找效率，不解决缓存项是否仍然语义有效。

如果缓存 value 持有 `ID3D12Resource*`、descriptor pointer、heap slot pointer 等对象引用，就必须面对资源释放、地址复用、内容变化、descriptor 被覆盖等风险。

更安全的方向是：

- 缓存 key 使用稳定值，例如 GPU VA range、descriptor slot identity、resource serial、content generation、view desc、draw args。
- 缓存 value 尽量保存拷贝出来的 summary/hash/decision，而不是裸资源指针。
- 资源释放时尽量不遍历清理所有缓存项，而是 bump 小范围 generation 或依赖 key 中的 serial/generation 自然失效。
- Dump/Hunting 需要强一致全量信息时，再启用更重的资源生命周期维护。

也就是说，`map` 可以作为缓存载体，但真正要改的是缓存语义：从“指向活资源的缓存”转成“按稳定身份和 generation 校验的值缓存”。

## 待验证问题

- 当前 ini 实际启用了哪些 Mod 能力：纯 shader、模型替换、贴图替换、pre-skin、present replacement？
- 500ms 帧中 `DX12ResourceInvalidated` 和 `BufferResolveCacheInvalidated` 的数量、总耗时、平均耗时是多少？
- `PurgeRangeLocked` 是否存在 O(cache entries) 或高锁竞争成本？
- `DX12ModNeedsShaderDescriptorTracking()` 是否因为某些宽泛标志长期为 true？
- `DX12ModHasActiveShaderOverrides()` 是否导致了不必要的 IA tracking？
- `ExecuteCommandLists` 中 `RegisterSubmittedCommandListDirect` 的 QI/lifecycle/hunt/binding submit 占比是多少？
- descriptor create/copy 的 wrapper 层提前 fast return 是否能保证不影响 texture override？

### 2026-07-13 实施记录：IA key 构建改为只消费 wrapper 缓存

上一轮 IASet 缓存改动已经证明有效：日志里没有看到 `BufferResolveBackwardScanHit` / `BufferResolveFallbackScanHit`，说明 IA 缓存已经避免了 draw 侧反复 GPU VA 解析。但代码里仍保留了一个第二层成本漏点：`BuildIaHashStateCacheKeyForTargets` 在 wrapper IA summary 未命中时，会在 draw 热路径再次调用 `DX12ResolveBufferResourceByGpuVa`。

本次改动把这条路径改成 DX11-style current state 模型：

- `IASetIndexBuffer` / `IASetVertexBuffers` 负责在绑定变化时解析一次 GPU VA -> resource summary。
- draw 阶段构建 IA hash key 只消费 `DX12ActiveIaState` 中已缓存的 summary。
- 如果绑定时没有解析到资源，draw 阶段不再二次动态解析，而是标记 `usesGlobalFallback`，继续用全局 content/resolve generation 保守失效。
- `DX12IaHashKeyPrefixCacheUtils` 增加 stable prefix 命中路径：当 prefix 不依赖 global fallback 时，cache hit 不再读取全局 generation。

安全边界：

- 不移除 IA 内容 hash，也不改变模型/贴图替换的匹配输入。
- 已解析资源仍然使用 resource generation + content generation 保证资源释放、重用、内容变化后的失效。
- 未解析资源不会被错当成已解析资源；只是退回保守 generation key，最多降低 cache 命中率，不会复用错误 hash。

预计效果：

- `BuildIaHashStateCacheKeyForTargets` 的 draw 热路径 GPU VA resolve 调用降低 100%。
- stable IA prefix cache 命中时，全局 generation 读取降低 100%。
- 对当前观测到的 `build_direct_ia_probe_state` 热点，预计 key 准备阶段开销下降 20% 到 45%；总帧 CPU 延迟预计提升 3% 到 10%，具体取决于当前帧中 IA hash cache miss 与 direct probe miss 占比。

### 2026-07-13 实施记录：descriptor table current-state 下沉到 wrapper runtime

继续按 DX11-style wrapper current state 方向推进。本次把 descriptor heap 和 root descriptor table 的当前绑定身份下沉到 `DX12CommandListRuntimeState`，也就是 `GraphicsCommandListWrapper::mRuntimeState`：

- `SetDescriptorHeaps` 入口无论是否跳过 BindingTracker/capture，都会先更新 wrapper runtime 的当前 CBV/SRV/UAV heap、sampler heap，以及对应 `DX12DescriptorHeapSummary`。
- heap 变化时，wrapper runtime 中的 graphics/compute root table current-state 会失效清空，避免旧 table 绑定跨 heap 复用。
- `SetGraphicsRootDescriptorTable` / `SetComputeRootDescriptorTable` 入口无论是否进入 capture，都会先更新 wrapper runtime 的 root table current-state。
- root table 更新时直接用当前 heap summary 计算 table base index、CPU/GPU start、heap version 等 resolved identity。
- `DX12BindingTracker::SnapshotBindingState` 保留原有 BindingTracker 快照，但会把 wrapper runtime 的 heap/table current-state 合并进去，让现有查询逻辑优先消费 wrapper 入口已经解析好的 table identity。

安全边界：

- 不改变 descriptor 内容查询和贴图替换匹配逻辑；descriptor summary 仍由 descriptor tracker 按 CPU handle/slot/epoch 查询。
- 新增缓存只保存当前绑定身份：heap、heap version、table base index、CPU/GPU start。
- heap 切换会清空 table current-state；descriptor 内容变化仍依赖 descriptor heap version / descriptor epoch 失效。

预计效果：

- root table 绑定阶段的 heap/table 反复解析减少 40% 到 80%。
- 正常 Mod fast path 中，即使 BindingTracker/capture 被跳过，也能维护 DX11-style 当前 descriptor table 状态。
- texture override / pre-skin 前置查询预计降低 5% 到 15%；如果当前帧 descriptor table 解析占比较高，总帧 CPU 延迟预计提升 3% 到 8%。
