# DX12 替换与着色器：ShaderOverride、TextureOverride、PSO 与 CustomShader

本文面向 DX12 DLL 的使用者、Mod 作者与本仓库维护者，说明 `[ShaderOverride]`、`[TextureOverride]`、`[ShaderRegex]`、`[CustomShader]` 在 **当前源码** 中是如何被解析、注册、匹配和消费的。

- 用户侧的精简操作说明见 [`usage/dx12/shaders.md`](../usage/dx12/shaders.md)、[`usage/dx12/texture-overrides.md`](../usage/dx12/texture-overrides.md)、[`usage/dx12/custom-shaders.md`](../usage/dx12/custom-shaders.md)、[`usage/dx12/overrides.md`](../usage/dx12/overrides.md)。
- INI 语言本身的扩展语义见 [`ini-language-extensions/README.md`](ini-language-extensions/README.md) 与 [`ini-language-extensions/command-lists/README.md`](ini-language-extensions/command-lists/README.md)（该目录描述的是 DX11 侧的完整语言，DX12 只实现了其中一部分，见第 10 节）。
- 崩溃现场的排查入口见 [`DX12-崩溃自诊断.md`](DX12-崩溃自诊断.md)。

**证据基线**：本文所有结论均来自本仓库当前工作树的源码，文中给出 `文件:行号` 以便复核。凡是"游戏内实测"才能确认的结论，本文一律标注为待验证，不写成已验证行为。

> 约定：本文保留源码中的标识符原样（`ShaderOverrideConfig`、`DX12ModReplaceShaderBytecode`、`gReloadGeneration` 等），不做翻译；中文只用于解释语义。

---

## 1. 从 INI 到 GPU 的链路

### 1.1 加载、重载与"代际"（generation）

DX12 的 Mod 配置由 `MigotoIniLoader::LoadWithIncludes` 统一加载（`src/DirectX12/Config/DX12ConfigLoader.h:406`）：

| 行为 | 说明 |
| --- | --- |
| 根 INI | `d3dx.ini`。查找顺序为"当前工作目录 → DLL 所在目录"（`src/DirectX12/Config/DX12ConfigLoader.h:562`）；DX12 代理进程在 `src/DirectX12/Proxy/D3D12ProcessGate.h:130-131` 处按 DLL 目录拼接 `d3dx.ini`。 |
| `[Include] include` | 相对**包含它的那个 INI 所在目录**解析，递归加载（`DX12ConfigLoader.h:284`）。 |
| `[Include] include_recursive` | 目录递归加载 `.ini`；目录内 `.ini` 按文件名小写排序，目录也按小写排序，**文件先于子目录**（`DX12ConfigLoader.h:222`）。 |
| `[Include] exclude_recursive` | 排除名；`disabled` 前缀的名字总是被排除（`DX12ConfigLoader.h:91`）。 |
| `[Include] user_config` | 持久化文件，默认 `d3dx_user.ini`，**最后加载**，因此它的值覆盖其它 INI（`DX12ConfigLoader.h:346`、`DX12ConfigLoader.h:436`）。 |

加载成功后由 `DX12ModRuntimeLoad` 解析出各章节（`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:1265`）：

```cpp
Bunny::MigotoShaderOverrideParser::ParseSections(iniLoad.document, &shaderOverrides);
Bunny::MigotoShaderRegexParser::ParseSections(iniLoad.document, &shaderRegexes);
Bunny::MigotoTextureOverrideParser::ParseSections(iniLoad.document, &textureOverrides);
Bunny::MigotoResourceParser::ParseSections(iniLoad.document, &resources);
Bunny::MigotoCommandListParser::ParseSections(iniLoad.document, &commandLists);
```

**重载代际**是 DX12 的核心失效机制：

- 每次加载成功 `++gReloadGeneration`（`DX12ModRuntimeLoad.cpp:1538`），并把 `gShaderOverrideNegativeCacheGeneration` 同步为新代际（`DX12ModRuntimeLoad.cpp:1539`）。
- 重载会清空 `gShaderOverridePsoMatchCache`、把已记录 PSO 的 `replacementGeneration` / `skipGeneration` 归零（`DX12ModRuntimeLoad.cpp:1503`、`DX12ModRuntimeLoad.cpp:1560`）。
- `F10` 触发 `DX12ModRuntimeReload` → 先保存持久化设置再重新加载，因此 F10 是"保存 + 重载"（`DX12ModRuntimeLoad.cpp:1625`）。
- **首次加载是延迟的**：启动时只"预约"路径（`DX12ModScheduleInitialLoad`，`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:246`），真正加载要等到 `present > 2` 且距预约已超过 `kDeferredInitialModLoadDelayMs = 2000`（常量定义在 `src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h:63`，判定处 `src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:277`）。这意味着**进程刚起来的前几帧不会有任何 Mod 生效**。

### 1.2 章节前缀与命名空间

节名匹配一律是**前缀匹配、忽略大小写**（`MigotoShaderOverrideParserText::StartsWithI`，`src/DirectX12/Config/DX12Config.h:1087`）。因此 `[ShaderOverrideFoo]`、`[shaderoverridefoo]` 都会进入同一类解析器。

前缀型节（`TextureOverride`、`CommandList`、`ShaderOverride`、`CustomShader`、`ShaderRegex`、`BuiltInCommandList`、`BuiltInCustomShader`、`Pool`、`Resource`、`Key`…）在命名空间化的 INI 中会被改名为 `前缀\命名空间\原名`（`DX12Config.h:121`、`DX12Config.h:190`）；**精确匹配**的节（`[Present]`、`[Constants]`、`[System]`、`[Logging]`、`[Hunting]`、`[Device]`、`[Rendering]`、`[Loader]`、`[Profile]`、`[Stereo]`、`[ConvergenceMap]`）是全局共享的，不会被命名空间改名（`DX12Config.h:137`、`DX12Config.h:157`）。

这一点很重要：`[Present]` 里的 `run = CustomShaderX` 与 `[CustomShaderX]` 处在不同命名空间时，引用会被解析成同命名空间的版本（`ResolveNamespacedSectionReference`，`DX12Config.h:206`）。

### 1.3 热路径开关

解析完成后运行时只打开确实需要的开关（`DX12ModGetHotPathFlags`，`src/DirectX12/ModRuntime/DX12ShaderOverrideRuntime.cpp:170`）：

| 标志 | 打开条件 | 置位位置 |
| --- | --- | --- |
| `DX12_MOD_HOT_SHADER_OVERRIDES` | 存在 `[ShaderOverride*]`（`gHasShaderOverrides`）**或**存在"可执行的无 Pattern ShaderRegex 组"（`gHasShaderRegexes`）。 | `DX12ShaderOverrideRuntime.cpp:181` |
| `DX12_MOD_HOT_IA_RESIDENT_STATE` | ① 有 Draw/Dispatch 侧覆盖工作且 `gHasIaResidentStateRequirements != 0`；② **或**存在绑定期静态 IA 替换（`gHasStaticIaReplacements != 0`，这是独立路径，源码刻意不把它挂到 `FLAG_SHADER_OVERRIDES`）。 | ① `:183-184`　② `:213-214` |
| `DX12_MOD_HOT_PRESENT_REPLACEMENT` | `gHasPresentRuntimeEffect != 0`，即 `[Present]` 侧存在运行时效果。**这是第 8.6 节整条 `[Present]` 路径的总闸门。** | `:185-186` |
| `DX12_MOD_HOT_PRE_SKIN_PROBE` | `gHasTextureOverrides != 0 && gHasShaderTriggeredTextureOverrides != 0`（存在被着色器触发的 TextureOverride）**且** `gHasPreSkinTextureOverrideCandidates != 0`（存在 PreSkin 候选）。 | `:187-190` |
| `DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK` | 只在 `PRE_SKIN_PROBE` 打开的前提下置位：`gHasActivePreSkinTextureOverridesFast != 0`（有活跃的 PreSkin 覆盖）**或** `gPreSkinBindingIdleFrames < kPreSkinBindingIdleMax`（绑定追踪尚未连续空闲到上限）。 | `:191-197` |
| `DX12_MOD_HOT_DRAW_SRV_SLOT_REPLACEMENT` | 存在被着色器触发的 TextureOverride **且** `gHasDrawSrvTextureReplacementOverrides != 0`（存在 `ps-tN` / `vs-tN` / `this =` 的绘制期槽位替换）。 | `:199-201` |
| `DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK` | `gHasShaderDescriptorTextureOverrideTriggers != 0`（着色器侧的描述符型 TextureOverride 触发器）。 | `:202-205` |

`DX12ModHasActiveShaderOverrides()` 返回的就是第一个标志（`DX12ShaderOverrideRuntime.cpp:137`）。**没有 ShaderOverride 时，整条 PSO 替换与绘制解释路径都不会进入。**

同理，**没有 `DX12_MOD_HOT_PRESENT_REPLACEMENT` 时 `[Present]` 章节完全不会被执行**——`DX12ModPreparePresentReplacement` 在 `src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp:266` 据此提前返回。

---

## 2. 哈希约定

### 2.1 着色器哈希：64 位 FNV-1a

DX12 的着色器哈希是 **DXIL 字节码的 64 位 FNV-1a**（偏移基 `14695981039346656037`、素数 `1099511628211`）：

- 运行时：`DX12ModHashShaderBytecode`（`src/DirectX12/ModRuntime/DX12ShaderOverrideRuntime.cpp:125`）
- 导出工具：`Fnv1a64`（`src/DirectX12/ShaderTools/DX12ShaderDump.cpp:112`）

两者是**同一算法**，所以 F9 导出的哈希可以直接填进 `[ShaderOverride] hash =`。哈希对象是 `D3D12_SHADER_BYTECODE` 的**原始字节**（`pShaderBytecode` / `BytecodeLength`），不是反汇编文本、不是 DXBC。

### 2.2 资源哈希：32 位

TextureOverride 用的资源哈希是 32 位（`MigotoTextureOverrideParser::ParseHash`，`src/DirectX12/Config/DX12Config.h:1345`）。它由三条不同的计算路径产生，都落在同一个 32 位通道上：

| 用途 | 函数 | 位置 |
| --- | --- | --- |
| 索引 / 顶点缓冲（IA）内容视图哈希 | `DX12HashIaBufferContentView` | `src/DirectX12/Tracking/DX12ResourceContentHashing.cpp:1201` |
| 描述符（SRV / UAV / CBV）绑定哈希 | `DX12HashDescriptorBufferView` | `src/DirectX12/Tracking/DX12ResourceContentHashing.cpp:1547` |
| DX11 风格的纹理描述符哈希 | `Dx11StyleTexture2DDescHash` | `src/DirectX12/Resources/Dump/DX12TextureHashUtils.h:410` |

其中 `DX12HashIaBufferContentView` 是**元数据型**哈希（GPUVA / 大小 / 步长 / 格式 / 槽位），不含内容字节；`DX12HashDescriptorBufferView` 哈希的是描述符的 kind、view format、dimension、`firstElement`、`numElements` 等描述符字段。

抓取工具复制到剪贴板的格式也印证了这一点（`src/DirectX12/ShaderTools/DX12ShaderHunt.cpp:763`）：

- VS / PS / CS：`%016llx`（16 位十六进制，64 位）
- IB / VB：`%08x`（8 位十六进制，32 位）

### 2.3 书写规则与失败路径

| 场景 | 规则 | 失败行为 |
| --- | --- | --- |
| `[ShaderOverride] hash =` | 纯十六进制，可带 `0x` / `0X` 前缀，大小写均可，**位数不限**（64 位）。**超过 16 位十六进制会按 2^64 静默回绕**，得到一个与输入意图不同的哈希，既不报错也不被拒绝。 | 非法字符 → 该行被**静默忽略**，`hasHash` 保持 false。 |
| `[TextureOverride] hash =` | 同上，但**最多 8 位十六进制**（32 位）。 | 超过 8 位 → **整行忽略**（`hasHash` 保持 false），与着色器哈希的"静默回绕"表现不同。 |

两者的失败表现必须区分：

- **资源哈希写长了**（> 8 位）→ 整行被丢弃，章节相当于没写 `hash`；
- **着色器哈希写长了**（> 16 位）→ 不会报错，但算出来的哈希是回绕值，与任何真实着色器都对不上，表现为"章节永不命中"。

解析失败**不会**产生警告或错误日志：`MigotoShaderOverrideParser::ParseSections` 在 `hash` 解析失败时直接 `continue`（`src/DirectX12/Config/DX12Config.h:1160`），`MigotoTextureOverrideParser::ParseSections` 同理（`DX12Config.h:1557`）；着色器哈希的逐字符累加没有长度上限，见 `DX12Config.h:1099-1126`。**排查时第一件事是确认哈希位数与来源匹配**（着色器 16 位、资源 8 位）。

---

## 3. `[ShaderOverride]`

### 3.1 语法

```ini
[ShaderOverride<任意名字>]
hash = 0123456789abcdef
handling = skip
run = CommandListFoo
pre = CommandListBar
post = CommandListBaz
CheckTextureOverride = ps-t0
```

支持的键：

| 键 | 类型 | 说明 |
| --- | --- | --- |
| `hash` | 64 位十六进制 | **必需**。缺失则整个章节不会被注册（`DX12Config.h:1194`）。同一哈希只能存在一个章节：`(*shaderOverrides)[config.hash] = config` 会覆盖同名哈希的先前章节（`DX12Config.h:1195`）。 |
| `handling` | `skip` | 见 3.4。其他取值不产生效果，且不进入动作流。 |
| `run` / `pre` / `post` | 命令列表引用 | `pre`/`pre run`/`prerun` 与 `post`/`post run`/`postrun` 是等价写法（`DX12Config.h:944`）。 |
| `CheckTextureOverride` | 资源槽 | 取值为 `ib`、`vbN`、`cs-cbN`、`ps-cbN`、`vs-cbN`、`cs-tN`、`ps-tN`、`vs-tN`、`cs-uN`、`ps-uN`（`src/DirectX12/Config/DX12Config.h:647`）。阶段前缀只有 `vs-` / `ps-` / `cs-` 三种被识别（`ParseShaderStagePrefix`，`src/common/MigotoIniCommandListUtils.hpp:80`），因此 `gs-` / `hs-` / `ds-` 形式**不被接受**；UAV 形式另外被限制为像素与计算两个阶段（`DX12Config.h:685`）。 |
| 其它动作键 | — | `draw` / `drawindexed` / `dispatch` / `vs` / `hs` / `ds` / `gs` / `ps` / `cs` / 变量赋值 / `post $x = ...` / `xNN = ...` / `Resource* = ...` / `this = ...` 等，与命令列表同一套动作语法（`DX12Config.h:815`）。 |

### 3.2 注册与匹配：按 PSO 的着色器字节码哈希

`[ShaderOverride]` **不按绘制参数匹配**，只按着色器哈希匹配。匹配发生在 PSO 层面：

`FindShaderOverridesForPsoLocked`（`DX12ShaderOverrideRuntime.cpp:1031`）从 PSO 中取出着色器信息：

- 图形 PSO：取 **VS 与 PS** 的哈希（`src/DirectX12/ModRuntime/DX12ShaderOverrideRuntime.cpp:1060-1071`）。
- 计算 PSO：取 **CS** 的哈希（`DX12ShaderOverrideRuntime.cpp:1046-1052`）。

也就是说：

- 一个 `[ShaderOverride]` 只声明了 VS 哈希，它只在**绑定了含该 VS 的 PSO** 的绘制上生效；
- 同一 PSO 上的 VS 与 PS 各自命中的章节**都会**执行；
- **GS / HS / DS 的哈希不参与匹配**，即使章节里写了它们的哈希。

匹配结果按 PSO 指针缓存到 `gShaderOverridePsoMatchCache`，缓存带代际；代际不匹配时重新计算（`DX12ShaderOverrideRuntime.cpp:945`）。

### 3.3 图形与计算的区别

| | 图形（Draw） | 计算（Dispatch） |
| --- | --- | --- |
| 参与匹配的着色器 | VS + PS | CS |
| 章节命令列表执行 | 绘制解释器内 | 计算解释器内 |
| `handling = skip` 的作用 | 跳过该 PSO 上的原始绘制 | 跳过该 PSO 上的原始 dispatch |

### 3.4 `handling = skip`

`handling = skip` 的语义**不是**"跳过这一段绘制"，而是"**凡是使用这个着色器的绘制/派发都跳过**"。

判定发生在 PSO 记录上（`UpdateStoredPsoSkipLocked`，`DX12ShaderOverrideRuntime.cpp:1809`）：

- 计算 PSO：`skipDispatch = ShaderBytecodeHasSkipLocked(record->computeDesc.CS)`；
- 图形 PSO：`skipDraw = ShaderBytecodeHasSkipLocked(VS) || ShaderBytecodeHasSkipLocked(PS)`。

`ShaderBytecodeHasSkipLocked` 用字节码哈希去 `gShaderOverrides` 查 `handlingSkip`（`DX12ShaderOverrideRuntime.cpp:1799`）。因此：

- 只要 VS **或** PS 有一个被标记 skip，**整个 PSO 的所有绘制都被跳过**；
- skip 标记在绘制决策里体现为 `DX12CompiledDrawDecisionKind::SkipOriginal`（`src/DirectX12/Core/CommandState/DX12IaDecisionState.h:424`、`DX12IaDecisionState.h:544`）。

**这是 DX12 里最容易误伤的一项**：同一着色器被多个模型共用时，skip 会同时隐藏它们。

### 3.5 命令列表与时序

`[ShaderOverride]` 的 `pre` / `run` / `post` 展开为 `CommandListLinks`，执行顺序固定为 **pre → main → post**（`RunCommandListLinks`，`src/DirectX12/ModRuntime/DX12CommandListExecutor.h:1703`）：

1. `pre` 列表按声明顺序执行；
2. `main`（即 `run`）列表按声明顺序执行；
3. `post` 列表按声明顺序执行。

其中：

- `pre` / `main` 在绘制**之前**执行（`includePost = false`），`post` 在绘制**之后**执行（`includePost = true`）。
- `post` 列表是在 `DX12ModRunPostShaderOverrideReplacement` 里单独跑的（`DX12ShaderOverrideRuntime.cpp:1755`），它会在**同一绘制点**再次构造一次执行器，只跑 post 列表与排队的 post TextureOverride。
- 递归深度上限 **16**；超过即停止展开（`DX12CommandListExecutor.h:1707`、`DX12CommandListExecutor.h:1417`）。
- 同一命令列表被递归调用时用栈检测防止自引用（`EnterCommandList` / `LeaveCommandList`，`DX12CommandListExecutor.h:1744`）。

`[ShaderOverride]` 的动作流本身在 `pre`/`main` 阶段执行（`RunShaderOverride`，`DX12CommandListExecutor.h:232`）：先登记 post 列表，再按 `handlingSkip` 设置 skip，最后执行 `commandLists` 与 `actions`。

### 3.6 `CheckTextureOverride`

`CheckTextureOverride = ps-t0` 是一条**动作**，不是匹配条件。它声明："执行本章节时，需要检查绑在 `ps-t0` 上的资源是否命中某个 `[TextureOverride]`"。

它的作用是**扩大绘制决策的预取范围**：绘制决策需要知道哪些 IA 槽位与哪些描述符槽位必须先算哈希，才能决定这次绘制是"必然不匹配"还是"需要继续匹配"。`RequireTargetLocked` 把 `CheckTextureOverride` 的目标翻译成目标计划里的必需项——索引缓冲、顶点缓冲槽位或描述符目标（`src/DirectX12/ModRuntime/DX12ShaderOverrideIaTargetPlanUtils.h:17`），计划由 `BuildDrawPlanLocked` 汇总（`DX12ShaderOverrideIaTargetPlanUtils.h:169`）。

因此：

- 不写 `CheckTextureOverride` 的章节，运行时不会为对应槽位计算哈希，可能匹配不到依赖该槽位的 TextureOverride；
- 写了但目标槽位不存在（例如该 PSO 没有 `ps-t0`），该槽位不会有绑定可检查，匹配自然落空。

### 3.7 失败路径

| 现象 | 原因 | 位置 |
| --- | --- | --- |
| 章节完全不生效 | 缺 `hash`，或 `hash` 含非十六进制字符 | `DX12Config.h:1160`、`DX12Config.h:1194` |
| 改了哈希没反应 | 哈希不是该 PSO 的 VS/PS/CS 字节码哈希 | `DX12ShaderOverrideRuntime.cpp:1031` |
| F10 后旧 PSO 仍用旧着色器 | **游戏自己创建出来的 PSO 对象不会被重建**：路径 A 的字节码替换只在 PSO *创建*时发生，路径 B 只额外产生一个替换 PSO。F10 能让替换 PSO 重建，但替换不了游戏手里那个已经含旧字节码的 PSO。要让全部 PSO 走新字节码，需要重启游戏。 | `DX12ShaderOverrideRuntime.cpp:2062` |
| 命中章节但命令列表没跑 | 章节没有运行时效果（命令列表既无可执行动作也无 IO 效果），会被预检剔除 | `CommandListRuntimeEffectLocked`，`src/DirectX12/ModRuntime/DX12ShaderOverrideIaTargetPlanUtils.h:10` |

---

## 4. `[TextureOverride]`

### 4.1 注册条件与桶

`[TextureOverride]` 的注册条件是 **或**关系（`DX12Config.h:1788`）：

```cpp
if (hasHash || config.hasVertexLimitRaise || config.hasCsHash || hasDrawContextMatch)
    (*textureOverrides)[config.hash].push_back(config);
```

即：写了 `hash`、写了扩容字段（`override_byte_stride` / `override_vertex_count` / `uav_byte_stride`）、写了 `cs_hash`、或写了任一 `match_*` 绘制上下文字段，章节都会被注册。**只有这四类都没有的章节会被丢弃**（例如只写 `ib =` 的章节不会被注册）。

没有 `hash` 的章节进入 **hash = 0 的通用桶**。运行时候选集分两批取：先取 hash=0 的通用批（`FindIaTextureOverridesCached(target, 0, ...)`），再取哈希命中批（`src/DirectX12/ModRuntime/DX12TextureOverrideMatching.cpp:1662`、`DX12TextureOverrideMatching.cpp:1670`）。所以"不写 hash、只写 `match_index_count = 300`"的章节可以对**任何**索引数为 300 的绘制生效——这是它最容易被滥用的地方。

### 4.2 匹配字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `hash` | 32 位十六进制 | 资源哈希。 |
| `match_vertex_count` | 数值匹配 | 绘制顶点数。 |
| `match_index_count` | 数值匹配 | 绘制索引数。 |
| `match_instance_count` | 数值匹配 | 实例数。 |
| `match_first_vertex` | 数值匹配 | 首顶点。 |
| `match_first_index` | 数值匹配 | 首索引。 |
| `match_first_instance` | 数值匹配 | 首实例。 |
| `match_priority` | 整数 | 匹配优先级，**数值大者优先**。 |
| `cs_hash` | 64 位十六进制 | 限定计算着色器哈希（PreSkin 路径）。 |

数值匹配支持比较前缀（`ParseNumericMatch`，`DX12Config.h:1410`），按下列顺序判定：

```text
<=   >=   !   =   <   >
```

无前缀时等价于 `=`。比较运算符之后的数字用 `wcstoul(..., 0)` 解析（**基 0**，因此 `0x10` 会被当成 16 进制，`DX12Config.h:1392`）。

匹配判定在 `TextureOverrideMatchesDrawContext`（`DX12TextureOverrideMatching.cpp:31`）——六个字段全部满足才算命中，**是 AND 关系**。

### 4.3 替换字段与"分支深度"规则

这是 DX12 与 DX11 差异最大的一块。`[TextureOverride]` 的绑定类字段在**顶层**和**分支内**语义不同（`DX12Config.h:1515`）：

解析时维护一个 `branchDepth`：遇到 `if ` 开头的控制流行 +1，遇到 `endif` -1（`DX12Config.h:1543`）。

| 字段 | 顶层（`branchDepth == 0`） | 分支内（`if` / `else`） |
| --- | --- | --- |
| `ib = X` | 记入 `indexBufferResource`，同时产生一条运行时动作 | 只产生运行时动作（分支依赖） |
| `vbN = X` | 记入 `vertexBufferResources[N]`，同时产生运行时动作 | 只产生运行时动作 |
| `ps-tN = X` | 记入 `drawPsSrvResources[N]`，走"静态快路径" | 只产生运行时动作，由 VM 判定分支 |
| `vs-tN = X` | 记入 `drawVsSrvResources[N]` | 只产生运行时动作 |
| `this = X` | 记入 `drawReplaceThisResource`，静态快路径 | 置 `hasConditionalThisReplacement = true`，产生 `SetThisResource` 动作 |
| `ib = null` / `vbN = null` | 置 `explicitNullBindings = true`，**不**产生指向 `Resourcenull` 的动作 | 同左 |

原因写在源码注释里：顶层绑定是无条件的，可以在加载期静态捕获；分支内的绑定依赖运行时分支，静态捕获表达不了（`DX12Config.h:1515`）。

注意 `else` **不会**改变 `branchDepth`——只有 `if ` 与 `endif` 会。所以 `if`/`else`/`endif` 结构里，绑定行一律处于分支内。

### 4.4 计算（PreSkin）相关字段

| 字段 | 说明 |
| --- | --- |
| `cs_hash` | 64 位，限定计算着色器。 |
| `match_cs_tN_hash` | 32 位，计算输入 SRV 的资源哈希，`N` 为槽位。 |
| `match_srvN_hash` | 同上，`match_srv` 前缀的等价写法。 |
| `cs-tN` | 计算输入资源引用（`[Resource...]` 名）。 |
| `srvN` | 同上，`srv` 前缀的等价写法。 |
| `match_uav_bytes` | **只记录"存在"标记，数值本身被丢弃**（`DX12Config.h:1663`：解析出的 `value` 未写入任何字段）。不能把它当成精确的输出字节筛选。 |

这一组字段只在 PreSkin（GPU 蒙皮输出替换）路径上生效，且要求游戏本身存在对应的 compute dispatch 流程。运行时是否对某个 CS 哈希做探测，由 `DX12ModShouldProbePreSkinningForCs` 决定（`src/DirectX12/ModRuntime/DX12PreSkinRuntime.cpp:5549`），条件是"存在被着色器触发的 TextureOverride"且"存在 PreSkin 候选"。

### 4.5 扩容字段

| 字段 | 说明 |
| --- | --- |
| `override_byte_stride` | 顶点字节步长。 |
| `override_vertex_count` | 顶点数。 |
| `uav_byte_stride` | 输出缓冲步长。 |

派生量在解析末尾计算（`DX12Config.h:1779`）：

```text
overrideByteWidth  = overrideByteStride * overrideVertexCount
overrideNumElements = overrideByteWidth / uavByteStride
```

只要写了其中任意一个，章节就被标记为"扩容目标"（`hasVertexLimitRaise`）并进入 `gVertexLimitRaiseConfigs`。运行时会用 `DX12VertexLimitRaiseConfig` 日志行回报实际生效值（`DX12ModRuntimeLoad.cpp:1596`）。

### 4.6 排序与优先级

最终匹配结果按以下顺序排序（`SortTextureOverrideMatches`，`DX12TextureOverrideMatching.cpp:1607`）：

1. `match_priority` **降序**；
2. 章节名**小写升序**；
3. `matchedHash` 降序。

章节桶内的初始排序（解析结束时）同样是 `match_priority` 降序 + 章节名小写升序（`DX12Config.h:1792`）。

因此**同一 `match_priority` 下，章节名决定顺序**——不要依赖 INI 文件的先后顺序。

### 4.7 匹配阶段

TextureOverride 的匹配入口按目标类型分三路（`FindTargetTextureOverride`，`DX12TextureOverrideMatching.cpp:1629`）：

| 目标 | 匹配依据 |
| --- | --- |
| `ib`（索引缓冲） | 索引缓冲的**内容视图哈希** `DX12HashIaBufferContentView`（元数据型：GPUVA / 大小 / 步长 / 格式 / 槽位，不含内容字节）；未命中时再试"区域哈希" `DX12IaRegion::HashIndexBufferView`（`src/DirectX12/ModRuntime/DX12TextureOverrideMatching.cpp:1683`，定义在 `src/DirectX12/Core/Models/DX12BindingTrackerModels.h:256`）。 |
| `vbN`（顶点缓冲） | 顶点缓冲的**内容视图哈希** `DX12HashIaBufferContentView`；未命中时再试 `DX12IaRegion::HashVertexBufferView`（`DX12TextureOverrideMatching.cpp:1746`，定义在 `DX12BindingTrackerModels.h:265`）。 |
| `ps-tN` / `vs-tN` / `ps-uN` / `cs-uN` / `*-cbN` | 描述符（SRV / UAV / CBV）绑定哈希 `DX12HashDescriptorBufferView`（哈希描述符的 kind、view format、dimension、`firstElement`、`numElements` 等字段）（`FindDescriptorTextureOverrides`，`src/DirectX12/ModRuntime/DX12TextureOverrideMatching.cpp:1768`；哈希实现 `src/DirectX12/Tracking/DX12ResourceContentHashing.cpp:1547`）。 |

IA 侧还有一条"资源要求"规则（`TextureOverrideMatchesIaBinding`，`DX12TextureOverrideMatching.cpp:205`）：

- `handling = skip` 的章节**匹配它哈希命名的那个 IA 资源**——经典 3DMigoto 的 `[TextureOverride_IB_xxxxxxxx]` 写法就靠这条生效（`DX12TextureOverrideMatching.cpp:216`）。
- 非 skip 章节若要匹配索引缓冲，必须有 `ib =` 或 `drawindexed` 动作；匹配顶点缓冲必须有对应 `vbN` 且槽位相符。

### 4.8 执行时序

TextureOverride 的执行分三段（`RunTextureOverride`，`src/DirectX12/ModRuntime/DX12CommandListExecutor.h:598`）：

1. **pre**：先跑 `pre` 命令列表，再跑章节自身的动作（`RunTextureOverrideActions`），再跑 `main` 命令列表；
2. **post**：单独排队，由 `DX12ModRunPostShaderOverrideReplacement` 在绘制之后执行（`DX12ShaderOverrideRuntime.cpp:1781`）；
3. **绘制期槽位替换**：`ps-tN` / `vs-tN` / `this =` 的静态捕获在动作之后应用（`ApplyTextureOverrideDrawSrvResources`，`DX12CommandListExecutor.h:675`）。

注意 `pre` / `main` 的执行被 `mExecuteCommands` 门控（`DX12CommandListExecutor.h:632`），而 post 阶段即使 `preRuntimeEffect` 为 false 也会在 `QueuePostTextureOverride` 里排队（`DX12CommandListExecutor.h:622`）。

### 4.9 失败路径

| 现象 | 原因 |
| --- | --- |
| 章节毫无作用 | 既没有 `hash`，也没有扩容/`cs_hash`/`match_*` 字段 → 未注册（`DX12Config.h:1788`）。 |
| `this =` 不生效 | 写在分支里时走运行时动作路径，需要对应的运行时判定；不是加载期静态替换。 |
| `match_uav_bytes` 看起来没筛掉任何东西 | 该字段只存存在标记，数值被丢弃（`DX12Config.h:1663`）。 |
| 同哈希多章节，只有一个生效 | 对 `[ShaderOverride]` 成立：它的键是 `hash`，同名哈希的章节**互相覆盖**，只保留最后一个（`DX12Config.h:1195`）。`[TextureOverride]` 不同：同一 `hash` 的多个章节会**全部保留**并按 4.6 的规则排序。 |
| 优先级看起来随机 | 同 `match_priority` 时按**章节名**排序，不是按文件顺序（`DX12TextureOverrideMatching.cpp:1620`）。 |

---

## 5. PSO 记录与替换

### 5.1 拦截点

DX12 侧与本文相关的拦截点（`src/DirectX12/Hooks/DX12HookRegistry.h:145`）：

| 接口 | 槽位 | 作用 |
| --- | --- | --- |
| `ID3D12Device::CreateGraphicsPipelineState` | 设备包装器 | 着色器字节码替换 + PSO 记录（`src/DirectX12/Presentation/DX12DeviceWrapper.h:476`）。 |
| `ID3D12Device::CreateComputePipelineState` | 设备包装器 | 同上（`DX12DeviceWrapper.h:527`）。 |
| `ID3D12GraphicsCommandList::SetPipelineState` | 命令列表包装器 | 替换 PSO 代入（`src/DirectX12/Core/CommandFlow/DX12CommandListStateCapture.cpp:153`）。 |
| `DrawInstanced` / `DrawIndexedInstanced` | 命令列表包装器 | 绘制覆盖解释器入口（`src/DirectX12/Hooks/DX12DrawHookFlow.h:88`）。 |
| `Dispatch` | 命令列表包装器 | 计算覆盖解释器入口（`src/DirectX12/Hooks/DX12DispatchHookFlow.h`）。 |
| `ExecuteIndirect` | 命令列表包装器 | 记录间接绘制（`src/DirectX12/ShaderTools/DX12ShaderHunt.cpp:512`）。 |

### 5.2 PSO 记录内容

`DX12ModRecordGraphicsPipelineState`（`DX12ShaderOverrideRuntime.cpp:1903`）在 PSO 创建成功后**深拷贝**创建输入：

- `D3D12_GRAPHICS_PIPELINE_STATE_DESC` 全量拷贝；
- VS / PS / DS / HS / GS 的字节码**拷贝到自有缓冲**（`StoreShaderBytecode`，`DX12ShaderOverrideRuntime.cpp:29`），避免游戏释放原字节码后悬垂；
- 输入布局元素、语义名、StreamOutput 描述与 `CachedPSO` blob 同样拷贝；
- `pRootSignature` **AddRef 保活**（`DeepCopyGraphicsDesc`，`DX12ShaderOverrideRuntime.cpp:82`）。

计算 PSO 由 `DX12ModRecordComputePipelineState` 同法记录（`DX12ShaderOverrideRuntime.cpp:1954`）。

### 5.3 替换管线的产生

`DX12ModGetReplacementPipelineState`（`DX12ShaderOverrideRuntime.cpp:2062`）：

1. 若该 PSO 没有记录 → 返回 `nullptr`（未经过包装器的 PSO 无法替换）；
2. 若缓存的替换 PSO 与当前代际一致 → 直接复用；
3. 否则按记录重建描述符，调用 `CreateGraphicsReplacement` / `CreateComputeReplacement`（`DX12ShaderOverrideRuntime.cpp:2019`、`DX12ShaderOverrideRuntime.cpp:2046`），后者内部再调 `DX12ModReplaceShaderBytecode` 换掉 VS / PS / CS 字节码；
4. 重建期间**不持有 `gModLock`**（先 AddRef device 与根签名，释放锁后再调用原生创建），避免长时间持锁；
5. 创建失败（`HRESULT` 失败或返回空）→ 返回 `nullptr`，**静默不替换**，不崩溃。

替换结果在 `SetPipelineState` 时代入游戏（`DX12CommandListStateCapture.cpp:158`），并在命令列表上按 `(pipelineState → replacement)` 做一层局部缓存（`cachedReplacementPsoKey` / `cachedReplacementPso`，`DX12CommandListStateCapture.cpp:161`）。

### 5.4 缓存与失效

| 缓存 | 键 | 失效条件 |
| --- | --- | --- |
| `gShaderOverridePsoMatchCache` | PSO 指针 | 代际不匹配即重算（`DX12ShaderOverrideRuntime.cpp:945`）。重载时整体清空（`DX12ModRuntimeLoad.cpp:1503`）。 |
| `gPsoRecords[].replacement` | PSO 指针 + 代际 | 代际变化时 `replacementGeneration` 归零，重建（`DX12ModRuntimeLoad.cpp:1560`）。 |
| 负缓存（`TryFindShaderOverrideNegativeCache`） | 线程局部 + 代际 | 代际变化时整体清空（`DX12ShaderOverrideRuntime.cpp:938`）。键包含 PSO 指针、绘制数量、IA 哈希等（`MakeShaderOverrideNegativeCacheKey`，`DX12ShaderOverrideRuntime.cpp:837`）。 |
| 预编译 PSO 计划 | PSO 指针 + 代际 | `ClearCompiledPsoPlans()`（`DX12ShaderOverrideRuntime.cpp:2171`）。 |

**重要**：路径 A 的字节码替换只发生在 PSO **被创建**的那一刻，游戏拿到的就是含替换字节码的 PSO；路径 B 产生的是一个**额外**的替换 PSO，游戏原来那个含旧字节码的 PSO 对象不会被就地改写。因此：

- F10 之后，**替换 PSO 会按新代际重建**（`replacementGeneration` 归零），之后的 `SetPipelineState` 会绑到新字节码；
- 但**游戏自己创建、且已经拿到引用的 PSO** 无法被改写。游戏若缓存了 PSO 对象并长期复用，F10 不足以让全部绘制走新字节码；
- 稳妥做法是替换 `ShaderFixes` 字节码后**重启游戏**。

### 5.5 根签名约束

替换 PSO **沿用游戏原本的根签名**（`DeepCopyGraphicsDesc` 只 AddRef，不替换 `pRootSignature`）。因此替换后的着色器必须与**游戏的根签名兼容**：

- 使用的 SRV / UAV / CBV 槽位必须在原根签名的 register space 中存在；
- 常量缓冲的寄存器号、大小与绑定方式不能超出原根签名声明；
- 原根签名的 visibility 决定新着色器能否看到某个资源。

不兼容时 `CreateGraphicsPipelineState` 会失败，替换静默失效（第 5.3 节第 5 条）。**这是着色器二进制替换最常见的失败原因，且没有任何日志。**

---

## 6. 着色器二进制替换（`ShaderFixes`）

### 6.1 目录与文件名

| 项 | 值 | 来源 |
| --- | --- | --- |
| 目录 | `<d3dx.ini 所在目录>\ShaderFixes` | `SetBasePaths`（`src/DirectX12/ModRuntime/DX12PreSkinRuntime.cpp:2215`） |
| 文件名 | `%016llx-<stage>.bin`，`stage` ∈ {`vs`, `ps`, `cs`} | `DX12ModReplaceShaderBytecode`（`DX12PreSkinRuntime.cpp:5501`） |

示例：

```text
ShaderFixes/0123456789abcdef-vs.bin
ShaderFixes/0123456789abcdef-ps.bin
```

哈希是**小写十六进制、不足 16 位补零**；哈希必须与着色器阶段对应（VS 的哈希只能配 `-vs.bin`）。

### 6.2 生效路径

字节码替换有两条路径，二者都以 `gShaderOverrides` 中**存在同哈希的章节**为前提：

| 路径 | 触发点 | 效果 |
| --- | --- | --- |
| **A：创建期** | `ID3D12Device::CreateGraphicsPipelineState` / `CreateComputePipelineState`（`DX12DeviceWrapper.h:491`、`DX12DeviceWrapper.h:542`） | 替换后的字节码**直接进入游戏拿到的 PSO**。游戏永远看不到原字节码。 |
| **B：绑定期** | `SetPipelineState` → `DX12ModGetReplacementPipelineState` → `CreateGraphicsReplacement`（`DX12ShaderOverrideRuntime.cpp:2028`、`DX12ShaderOverrideRuntime.cpp:2051`） | 保留原 PSO，另建一个替换 PSO 并在绑定时代入。 |

路径 A 只在 PSO **由被包装的 `ID3D12Device` 创建**时生效；路径 B 依赖 `gPsoRecords` 里存在该 PSO 的记录（同样要求 PSO 经过包装器创建）。

### 6.3 约束

1. **必须先声明章节**：`DX12ModReplaceShaderBytecode` 先查 `gShaderOverrides.find(hash)`，查不到直接返回 `false`（`DX12PreSkinRuntime.cpp:5491`）。只放 `.bin` 文件而不写 `[ShaderOverride<名字>] hash = ...` 不会有任何效果。
2. **文件必须是该阶段可用的合法编译字节码**（DXIL）。运行时只做字节拷贝，**不校验、不反汇编**（`DX12PreSkinRuntime.cpp:5505`）。
3. **根签名必须兼容**（见 5.5）。失败的返回是"静默不替换"。
4. **不支持 DX11 的 `_replace.txt` / HLSL 文本替换**：DX12 只读二进制 `.bin`。
5. 读取失败（文件不存在、被占用）→ `ReadFileBytes` 返回 false → 静默返回 `false`（`src/DirectX12/ModRuntime/DX12PreSkinRuntime.cpp:5505-5509`；路径拼接在同文件 `:5501-5503`）。

### 6.4 导出的着色器

`F9` 会把 PSO 与着色器信息导出到 `ShaderFixes` 目录（`GetShaderFixesDirectory`，`src/DirectX12/ShaderTools/DX12ShaderDump.cpp:202`），文件名同样是 `%016llx-<stage>.bin`，因此**导出的字节码可以直接作为替换源**。导出的汇编（`.asm`）与使用摘要用于分析，不保证每个格式都能反编译成 HLSL。

**目录差异（需注意）**：F9 导出用的是 **DLL 模块所在目录**下的 `ShaderFixes`（`GetModuleFileNameW(DX12RuntimeState::GetModule(), ...)`，`DX12ShaderDump.cpp:206`），而运行时读取用的是 **`d3dx.ini` 所在目录**下的 `ShaderFixes`（`DX12PreSkinRuntime.cpp:2228`）。标准部署中 `d3dx.ini` 与 DLL 同目录，两者相同；若把 INI 放在别处，需要手动把 `.bin` 放到 INI 目录下的 `ShaderFixes` 里。

---

## 7. `[ShaderRegex]`

### 7.1 当前支持的范围

DX12 只实现了 **shader-model 筛选**，且只对**没有 `.Pattern` 子节**的组生效：

```ini
[ShaderRegexPixel]
shader_model = ps_6_0
exclude_hash = 0123456789abcdef
run = CommandListPixel
```

- `exclude_hash` 每行填写一个 1–16 位十六进制着色器 Hash，可带 `0x`。同组重复项合并。匹配时先排除当前 Hash，再链接命令。无效值会产生配置警告并禁用该组。
- `shader_model` 的值按空白切分成多个 token，统一转小写（`AppendShaderModels`，`DX12Config.h:1985`）。
- 运行时用 PSO 里的实际着色器模型字符串比较，模型格式为 `ps_6_0` / `vs_6_0` / `cs_5_1` 这种 `前缀_主版本_次版本`（`FormatShaderModel`，`src/DirectX12/ShaderTools/DX12ShaderModelParse.h:53`；DXIL 优先于 DXBC，`DX12ShaderModelParse.h:141`）。
- 匹配到后，组的 `pre` / `run` / `post` 与动作按与 `[ShaderOverride]` 相同的顺序执行（`MakeShaderOverrideFromRegex`，`DX12ShaderOverrideRuntime.cpp:232`）。

**判定"可执行"的硬条件**：`ShaderModelMatchesRegex` 在 `regex.hasPattern` 为真时**直接返回 false**（`DX12ShaderOverrideRuntime.cpp:222`）；加载期也只把无 Pattern 的组计入 `hasExecutableShaderRegexes`（`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:798`）。**加一个空的 `.Pattern` 节就会让整组失效**，这不是可以绕过的限制。

### 7.2 不支持

| DX11 语义 | DX12 状态 |
| --- | --- |
| `.Pattern` / `.Pattern.Replace` 的 PCRE2 汇编修补 | **不支持**。有 `.Pattern` 的组不进入任何执行路径。 |
| `.InsertDeclarations` | **不支持**（解析器没有对应处理）。 |
| `temps` 自动临时寄存器 | 键会被解析但**忽略**（`DX12Config.h:2071`）。 |
| `filter_index` | 同上，被忽略。 |

---

## 8. `[CustomShader]`

### 8.1 元素识别

`[CustomShader*]` 章节先被当作**命令列表**解析（`IsExplicitCommandListSection`，`src/common/MigotoIniCommandListUtils.hpp:46`），再由 `DX12CustomShaderElementParser::BuildRegistry` 从动作流**重建**出元素规格（`src/DirectX12/ModRuntime/DX12CustomShaderElement.h:200`）。

判定为"自定义着色器元素"的条件是**至少含一个阶段文件或一条渲染状态行**（`BuildSpec` 返回 `anyState`，`DX12CustomShaderElement.h:160`）。一个恰好叫 `[CustomShaderX]` 但只有 `run` 的普通命令列表不会被当成元素。

阶段槽位顺序固定为 `vs, hs, ds, gs, ps, cs`（`AssignStageFile`，`DX12CustomShaderElement.h:356`）。

### 8.2 阶段文件与编译

```ini
[CustomShaderDraw]
vs = draw_vs.hlsl
ps = draw_ps.hlsl
```

- 路径解析：绝对路径直接用；相对路径依次尝试 **INI 所在目录 → 加载根目录 → 原样**（`ResolveStageFilePath`，`src/DirectX12/ModRuntime/DX12CustomShaderCompile.h:157`）。
- 编译 profile：`vs_6_0` / `hs_6_0` / `ds_6_0` / `gs_6_0` / `ps_6_0` / `cs_6_0`（`ProfileForStage`，`DX12CustomShaderCompile.h:136`），入口固定为 `main`（`DX12CustomShaderCompile.h:414`、`DX12CustomShaderCompile.h:316`）。
- 后端顺序：先 **DXC**（`dxcompiler.dll` + `dxil.dll`，两者从同一目录加载，避免版本不匹配），失败再试 **d3dcompiler_47** 配 SM6 profile（`CompileStage`，`DX12CustomShaderCompile.h:185`；`EnsureDxcLoaded`，`DX12CustomShaderCompile.h:360`）。两个后端都没有可用编译器时**降级为设置一条错误文本后不做任何事**（`DX12CustomShaderCompile.h:233`）——该文本不会写入日志，见 8.7。
- 编译缓存按 **路径 + profile** 键，按文件最后写入时间失效；**失败也缓存**（负缓存），避免每帧重复尝试（`LookupCacheLocked`，`DX12CustomShaderCompile.h:511`；`DX12CustomShaderCompile.h:208`）。重载时 `DX12CustomShaderCompile::ClearCache()` 清空（`src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp:761`）。
- **只有 `vs`（槽 0）与 `ps`（槽 4）真正进入 PSO 创建**；`cs` 等阶段名能被解析，但图形 PSO 路径不使用（`DX12CustomShaderDevice.h:385`）。只有 VS 或只有 PS 的元素**无法形成 PSO**，`EnsurePipeline` 直接返回 `nullptr`（`DX12CustomShaderDevice.h:400`）。DX11 可以只换单个阶段，D3D12 做不到。

### 8.3 共享根签名

所有自定义元素共用**一个**根签名（`DX12CustomShaderPipelineAssembler::SerializeRootSignature`，`src/DirectX12/ModRuntime/DX12CustomShaderPipeline.h:41`）：

| 项 | 值 |
| --- | --- |
| 描述符表 | 1 个表，SRV `t0`–`t127`，register space 0，visibility `ALL` |
| 静态采样器 | `s0`，`MIN_MAG_MIP_LINEAR` + `CLAMP`（U/V/W），最大各向异性 16，比较函数 `ALWAYS` |
| 标志 | `D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT` |

**约束**：自定义着色器只能访问 `t0`–`t127` 与 `s0`；不能使用 CBV、UAV、root CBV、额外采样器或多个 register space。着色器按**绝对寄存器号**寻址（`ps-t100` 就是 `t100`），无需重映射（`DX12CustomShaderPipeline.h:7`）。

### 8.4 管线状态与默认值

`FillGraphicsPipelineDesc`（`DX12CustomShaderPipeline.h:265`）在**没有对应键**时使用下列默认值（DX11 是与游戏状态合并，这里是显式默认）：

| 项 | 默认值 |
| --- | --- |
| `DepthEnable` / `StencilEnable` | `FALSE` / `FALSE`（`FillDepthStencilDesc`，`DX12CustomShaderPipeline.h:248`） |
| `NumRenderTargets` / `RTVFormats` | 1 个，格式取交换链 back buffer 的 typed 格式；其余 7 个为 `DXGI_FORMAT_UNKNOWN` |
| `SampleDesc.Count` / `Quality` | 1 / 0（无 MSAA） |
| `InputLayout` | 空（`nullptr`, 0） |
| `PrimitiveTopologyType` | 由 `topology` 推导；未指定时 `TriangleList` |
| `CullMode` | `BACK`（`FillRasterizerDesc`，`DX12CustomShaderPipeline.h:223`） |
| `FrontCounterClockwise` | `FALSE` |
| `FillMode` | `SOLID` |
| `DepthBias` / `DepthBiasClamp` / `SlopeScaledDepthBias` | 0 |
| `DepthClipEnable` | `TRUE` |
| `SampleMask` | `0xffffffff`（`sample_mask` 可覆盖，`DX12CustomShaderElement.h:441`） |
| Blend | 默认关闭（`FillBlendDesc`，`DX12CustomShaderPipeline.h:191`）；`blend = disable` 显式关闭 |

`blend` 支持的操作：`add`、`subtract`、`rev_subtract`、`min`、`max`；因子：`zero`、`one`、`src_color`、`inv_src_color`、`src_alpha`、`inv_src_alpha`、`dest_alpha`、`inv_dest_alpha`、`dest_color`、`inv_dest_color`、`src_alpha_sat`、`blend_factor`、`inv_blend_factor`、`src1_color`、`inv_src1_color`、`src1_alpha`、`inv_src1_alpha`（`DX12CustomShaderElement.h:235`）。`blend` 行最多 6 个 token，后 3 个是 alpha 通道的 `op src dst`（`ParseBlendLine`，`DX12CustomShaderElement.h:263`）。

`topology` 支持：`point_list`、`line_list`、`line_strip`、`triangle_list`、`triangle_strip`、`line_list_adj`、`line_strip_adj`、`triangle_list_adj`、`triangle_strip_adj`，以及 `N_control_point_patch_list`（`N` 为 1–32）（`ParseTopologyToken`，`DX12CustomShaderElement.h:300`）。

`oN`（`o0`–`o7`）被解析为渲染状态行；只有值里含 `bb` 时才置 `hasBackbufferTarget`（`DX12CustomShaderElement.h:450`）。

**注意**：内部模型里虽然有 `alpha`、`fill`、`front`、`sample_mask` 等字段，但只有上面列出的键会被送进 PSO 描述符。`depth`、`stencil`、采样器、逐目标混合（`blend1`…）等键**只被原样记录在 `stateLines` 里**，不会改变生成的 PSO（`DX12CustomShaderElement.h:462`）。不要按 DX11 的经验假设它们生效。

### 8.5 描述符表与 IniParams

- 描述符表堆按帧轮转 4 个槽位，每槽再按元素切成 64 个 128 槽区域（`DX12CustomShaderTableHeap`，`src/DirectX12/ModRuntime/DX12CustomShaderDevice.h:520`；`kHeapSlots = 4`，`kMaxCustomElementsPerFrame = 64`，`DX12CustomShaderRegisters.h:40`）。
- 区域内**每个槽位先被填成 1×1 黑色纹理**，然后把 IniParams 的 SRV 放到 `t120`（`DX12CustomShaderDevice.h:643`）。未被绑定的槽位因此始终是合法的 `Texture2D` 描述符，避免着色器读到类型不符的描述符。
- **IniParams**：`t120` 上的 1D float4 纹理，512 个 float4（8 KB）。寄存器 `xNN` 映射到第 `NN` 个 float4 的 `.x`，`yNN`/`.zNN`/`.wNN` 对应其余分量（`DX12CustomShaderRegisters.h:22`）。写入 `x87 = 1.0` 后，着色器读 `IniParams[87].x` 得到 1.0。

### 8.6 执行路径与时序

自定义元素的**绘制**不在 `[Present]` 章节执行的那一刻发生，而是延迟到真正的 Present 回放。完整链路：

1. `[Present]` 章节本身**不是**在 Present 时执行的。整条路径受 `DX12_MOD_HOT_PRESENT_REPLACEMENT` 开关门控（置位条件见 1.3）：开关未打开时 `DX12ModPreparePresentReplacement` 在 `src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp:266` 提前返回，`[Present]` 完全不会执行。开关打开后，它在**本帧第一个被拦截的绘制**上用 compare-exchange 抢占本帧（同文件 `:261`），然后执行 `[Present]` 命令列表（`RunNamedCommandList(device, commandList, L"Present", ...)`，`:291`）。原因见源码注释：DX11 的 `[Present]` 语义是"游戏渲染之后、Present 之前"，而游戏在帧首录制命令列表时无法满足该语义（`src/DirectX12/Hooks/DX12IaReplacement.h:1227`）。
2. `[Present]` 执行到 `run = CustomShaderX` 时，发现 X 是已注册的元素 → **不解释其 body**，而是记录一份快照：元素规格 + 当前绑定的像素着色器资源槽 + 当前 IniParams 打包值 + 首个 `draw` 动作的参数（`src/DirectX12/ModRuntime/DX12CommandListExecutor.h:1419`）。
3. 快照排队到 Present 通道（`DX12ModQueuePresentCustomElements`，`src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp:718`）。注意：**一个 `[Present]` 章节如果只记录元素而不产生其它改动，本身不会把替换标记为"有改动"**，因此源码专门把 `customElementRecords` 非空也算作有工作（`DX12CommandListExecutor.cpp:302`）。
4. 真正的 Present 时，由 `DX12PresentOverlayCompose` 统一录制到 **mod 自有的命令列表与分配器**，并在游戏的 direct queue 上、原始 `Present` 之前提交（`DX12CommandListExecutor.cpp:911`）。同一帧的 Mod 覆盖层与调试文字共用一条命令列表和一对 back buffer 屏障。
5. 回放时逐元素：取描述符表区域 → 上传 IniParams → 填默认描述符 → 拷贝绑定的纹理 SRV → 取得/创建 PSO → `SetDescriptorHeaps` / `SetGraphicsRootSignature` / `SetGraphicsRootDescriptorTable` / `SetPipelineState` / `IASetPrimitiveTopology` / `OMSetRenderTargets` / `RSSetViewports` / `RSSetScissorRects` / `DrawInstanced`（`DX12ModRecordPresentOverlay`，`DX12CommandListExecutor.cpp:800`）。

绘制参数来自元素的第一个 `draw` 动作（`record.drawCount` / `record.drawStart`）；没有则回落到默认的 4 顶点（`DX12CommandListExecutor.cpp:891`、`DX12CommandListExecutor.h:1464`）。**条件分支或多个 draw 的元素目前只会用第一个 draw。**

因为游戏自己的命令列表在 Present 时已经关闭，DX12 只能像这样"另开一条列表回放"，这也是自定义元素不能读游戏当帧状态的原因。

### 8.7 失败路径与诊断

诊断事件统一打在 `DX12CustomShaderDiag` 日志通道。**该通道只在 TIMING 构建存在**：`DX12_LOG` / `DX12_LOG_FLUSH` 在非 TIMING 构建下被编译成空语句（`src/DirectX12/Core/Diag/DX12Diag.h:2197` 与 `:2206`），日志文件也只在 TIMING 构建里打开（`src/DirectX12/Resources/DX12RuntimeControl.h:112`）。因此下表事件在 Release 版**完全不会出现**，而不是"信息较少"。

| 事件 | 原因 |
| --- | --- |
| `event=replay_skip reason=invalid_spec` | 元素缺少 `vs` 或 `ps`（`src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp:828`）。 |
| `event=replay_skip reason=element_capacity_exceeded` | 单帧元素数超过 64，剩余元素被**跳过**（可见降级，不损坏画面）（`DX12CommandListExecutor.cpp:839`）。 |
| `event=replay_skip reason=ini_params_upload_failed` | IniParams 上传失败（`DX12CommandListExecutor.cpp:847`）。 |
| `event=replay_skip reason=pipeline_unavailable` | 根签名创建失败、阶段编译失败或 `CreateGraphicsPipelineState` 失败（`DX12CommandListExecutor.cpp:866`）。 |
| `event=replay_skip reason=overlay_channel_unavailable` | 取不到游戏的 direct queue、覆盖层通道或 back buffer 视图（`DX12CommandListExecutor.cpp:951`）。 |
| `event=replay_skip reason=overlay_begin_record_failed` | 通道槽位仍在上一次提交中，等待超时（`kSlotWaitMs = 2000`，`src/DirectX12/ModRuntime/DX12CustomShaderDevice.h:1188`），该帧被丢弃。 |
| `event=element_deferred section=... recorded=0` | 元素规格未找到（注册表与命令列表不一致）（`src/DirectX12/ModRuntime/DX12CommandListExecutor.h:1491`）。 |
| `event=element_registered ...` | 加载期登记的元素摘要（`src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:1295`），位于 `#if DX12_TIMING_BUILD` 内。 |

**没有编译错误文本**：编译失败时 `errorText` 只写进局部变量，不会输出到日志（`DX12CustomShaderDevice.h:387`）。排查编译问题要单独用 DXC 编译 HLSL。

---

## 9. 时序总表

| 阶段 | 触发 | 发生什么 | 相关缓存 |
| --- | --- | --- | --- |
| 进程启动 | `DX12Runtime::Initialize` | 只**预约**配置路径，不解析 | — |
| `present > 2` 且 ≥ 2 s | `DX12ModMaybeRunDeferredInitialLoad` | 首次完整加载：`++gReloadGeneration` | 全部缓存建立 |
| `CreateGraphicsPipelineState` | 设备包装器 | 字节码替换（路径 A）+ PSO 记录 + 预编译 PSO 计划 | `gPsoRecords` |
| `CreateComputePipelineState` | 设备包装器 | 同上（CS） | `gPsoRecords` |
| `SetPipelineState` | 命令列表包装器 | 代入替换 PSO（路径 B） | 命令列表局部缓存、`gPsoRecords[].replacement` |
| `DrawInstanced` / `DrawIndexedInstanced` | 绘制钩子 | 绘制决策 → 负缓存 → 覆盖解释器 → pre/main → 原绘制 → post；**本帧第一个被拦截的绘制**还会抢占并执行 `[Present]` 命令列表 | `gShaderOverridePsoMatchCache`、负缓存、预编译计划、`gPresentCommandListExecutedPresent` |
| `Dispatch` | 计算钩子 | 计算侧覆盖解释器 | 同上（dispatch 计划） |
| `Present` | `DX12PresentOverlayCompose` | 回放此前记录的自定义元素与调试文字 | 描述符表堆轮转、IniParams 上传环、PSO 缓存 |
| `F10` | UI 动作 | 保存持久化设置 → 重新加载 → `++gReloadGeneration` → 清缓存 | 全部按代际失效 |

---

## 10. 不支持语法速查

下列 DX11 语义在 DX12 当前源码中**没有对应的注册或消费路径**：

| 语法 | 状态 | 说明 |
| --- | --- | --- |
| `[ShaderOverride]` 省略 `hash` | 不支持 | 章节不会被注册。 |
| `[ShaderOverride]` 的模糊匹配 / 绘制参数匹配 | 不支持 | 只按 VS/PS/CS 字节码哈希匹配。 |
| `[ShaderOverride]` 匹配 GS / HS / DS | 不支持 | 只有 VS、PS（图形）和 CS（计算）参与匹配。 |
| `_replace.txt` / HLSL 文本替换 | 不支持 | 只支持 `ShaderFixes\<hash>-<stage>.bin` 二进制。 |
| `[ShaderRegex] .Pattern` / `.Pattern.Replace` | 不支持 | 有 `.Pattern` 的组完全不执行。 |
| `[ShaderRegex] .InsertDeclarations` | 不支持 | 解析器无对应处理。 |
| `[ShaderRegex] temps` / `filter_index` | 被忽略 | 键被识别但不产生行为。 |
| `[CustomShader]` 只给 `vs` 或只给 `ps` | 不支持 | 阶段不全会导致 `EnsurePipeline` 返回 `nullptr`。 |
| `[CustomShader]` 的 `cs =` | 解析但未接入 | 图形 PSO 路径只用 VS/PS。 |
| `[CustomShader]` 的 `depth` / `stencil` / 采样器 / `blend1`… | 只记录不生效 | 见 8.4。 |
| `[CustomShader]` 访问 CBV / UAV / 多 register space | 不支持 | 共享根签名只提供 `t0`–`t127` 与 `s0`。 |
| `[CustomShader]` 元素内读取游戏当帧状态 | 不支持 | 元素在 Present 回放，body 不被解释。 |
| `[TextureOverride]` 的 `match_uav_bytes` 精确筛选 | 只记录标记 | 数值被丢弃。 |
| 显式实例绘制 | 替换绘制支持索引和非索引两种形式 | Present 独立图形元素只支持非索引形式。见 [`usage/dx12/custom-shaders.md`](../usage/dx12/custom-shaders.md)。 |
| 间接绘制与间接派发 | 待实现 | 尚缺从参数缓冲执行命令的路径。 |
| 像素着色器着色器常量 `xNN` 直写 | 部分支持 | 只在自定义元素的 IniParams 通道上有意义。 |

---

## 11. 诊断入口

**日志可见性前提**：`DX12_LOG` / `DX12_LOG_FLUSH` 只在 **TIMING 构建**下有实现，非 TIMING 构建被编译成空语句（`src/DirectX12/Core/Diag/DX12Diag.h:2197` 与 `:2206`），且日志文件也只在 TIMING 构建里打开（`src/DirectX12/Resources/DX12RuntimeControl.h:112`）。**下表所有通道都只在 TIMING 版存在**；Release 版没有任何这些输出。

| 目的 | 手段 |
| --- | --- |
| 确认配置是否被加载 | 日志通道 `DX12ModRuntime`，含 `shaderOverrides` / `shaderRegexes` / `textureOverrides` / `resources` / `commandLists` 计数，以及 `activeShaderOverrides`、`activeTextureOverrides`、`shaderTriggeredTextureOverrides`、`descriptorTextureOverrideTriggers`、`preSkinCandidates`、`preSkinProbeEnabled`、`presentRuntimeEffect`、`shaderFixes` 与 `generation`（格式串在 `src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp:1572`）。第 12 节第 3、4、6 条要判断的正是 `presentRuntimeEffect` / `activeTextureOverrides` / `preSkinProbeEnabled` 这几个字段。 |
| 确认 INI 解析错误 | 日志通道 `DX12ModRuntimeIni`，逐条列出 `error` 与 `warning`（`DX12ModRuntimeLoad.cpp:1588`）。 |
| 确认实际展开了哪些章节 | 日志通道 `DX12ModRuntimeIni`，`status=loaded` 行带 ini 路径（`DX12ModRuntimeLoad.cpp:1585`）。 |
| 确认扩容参数 | 日志通道 `DX12VertexLimitRaiseConfig`（`DX12ModRuntimeLoad.cpp:1596`）。 |
| 确认自定义元素注册 | TIMING 版日志通道 `DX12CustomShaderDiag`，`event=element_registered`（`DX12ModRuntimeLoad.cpp:1295`，位于 `#if DX12_TIMING_BUILD` 内）。 |
| 确认抓到的哈希 | 抓取界面复制到剪贴板：VS/PS/CS 为 16 位十六进制，IB/VB 为 8 位（`src/DirectX12/ShaderTools/DX12ShaderHunt.cpp:763`）。 |
| 运行时崩溃 | 见 [`DX12-崩溃自诊断.md`](DX12-崩溃自诊断.md)。 |

**通用排查顺序**：先确认哈希位数与来源正确（着色器 16 位 / 资源 8 位）→ 再确认章节被加载（计数不为 0）→ 再确认匹配条件（`match_*` 是 AND 关系、`match_priority` 同级按章节名排序）→ 最后才怀疑拦截路径（PSO 是否经过包装器、替换字节码是否与根签名兼容）。

---

## 12. 待确认事项

以下条目**在当前源码中可以确认解析与注册路径，但没有在目标游戏中实测**，使用前需按第 11 节逐项核对：

1. `ShaderFixes\<hash>-<stage>.bin` 在**真实游戏根签名**下创建替换 PSO 的成功率。源码只在失败时静默返回 `nullptr`，没有区分"文件缺失"与"根签名不兼容"的日志。
2. `[ShaderOverride] handling = skip` 的实际影响范围。源码按字节码哈希判定 PSO 级跳过，但"同一着色器被多少模型共用"取决于游戏。
3. `[CustomShader]` 在真实游戏中的回放效果。回放发生在 Present、走 mod 自有命令列表与独立提交，是否与游戏的 present 模型冲突需要实机确认。
4. PreSkin 相关的 `cs_hash` / `match_cs_tN_hash` / `cs-tN` / `match_uav_bytes` 组合。`match_uav_bytes` 的数值当前被解析器丢弃，其预期语义需要先补实现或改用其他筛选条件。
5. 区域哈希回退（`DX12IaRegion::HashIndexBufferView` / `HashVertexBufferView`）在真实游戏资源上的命中率。
6. `override_byte_stride` / `override_vertex_count` / `uav_byte_stride` 触发的扩容在真实蒙皮流程中的正确性。源码只保证派生量计算与候选筛选，不保证骨骼与输出格式正确。
7. 自定义元素单帧 64 个的上限在实际 UI Mod 中是否够用。

---

## 附录：源码索引

| 主题 | 文件 |
| --- | --- |
| INI 模型、章节前缀、命名空间 | `src/DirectX12/Config/DX12Config.h` |
| INI 加载、include、user_config | `src/DirectX12/Config/DX12ConfigLoader.h` |
| ShaderOverride / TextureOverride / ShaderRegex 解析 | `src/DirectX12/Config/DX12Config.h` |
| 动作语法与命令列表引用 | `src/common/MigotoIniCommandListUtils.hpp` |
| 加载、重载、代际、热路径索引 | `src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp` |
| 着色器覆盖运行时、PSO 匹配与缓存 | `src/DirectX12/ModRuntime/DX12ShaderOverrideRuntime.cpp` |
| TextureOverride 匹配 | `src/DirectX12/ModRuntime/DX12TextureOverrideMatching.cpp` |
| 覆盖执行器（pre/main/post） | `src/DirectX12/ModRuntime/DX12CommandListExecutor.h`、`DX12CommandListExecutor.cpp` |
| 绘制/计算钩子 | `src/DirectX12/Hooks/DX12DrawHookFlow.h`、`DX12DispatchHookFlow.h` |
| 绘制决策 | `src/DirectX12/Core/CommandState/DX12IaDecisionState.h` |
| PSO 拦截与记录 | `src/DirectX12/Presentation/DX12DeviceWrapper.h`、`src/DirectX12/Core/CommandFlow/DX12CommandListStateCapture.cpp` |
| 着色器字节码替换与 PreSkin | `src/DirectX12/ModRuntime/DX12PreSkinRuntime.cpp` |
| 着色器导出与模型解析 | `src/DirectX12/ShaderTools/DX12ShaderDump.cpp`、`DX12ShaderModelParse.h` |
| 抓取（哈希来源） | `src/DirectX12/ShaderTools/DX12ShaderHunt.cpp` |
| CustomShader 元素、编译、管线、设备对象 | `src/DirectX12/ModRuntime/DX12CustomShaderElement.h`、`DX12CustomShaderCompile.h`、`DX12CustomShaderPipeline.h`、`DX12CustomShaderDevice.h`、`DX12CustomShaderRegisters.h` |
| 拦截点清单 | `src/DirectX12/Hooks/DX12HookRegistry.h` |
