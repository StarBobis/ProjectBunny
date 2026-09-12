# DX12 Profiling HUD 说明书（一图定位问题）

这份文档配的是**屏幕上那张全屏的表**。目标只有一个：你截一张图发我，我不看代码也能立刻说出"慢在哪"。

打开方式（没变）：`config/d3dx.ini` 里 `dx12_profiling_hud = 1`（或游戏里按 **F11** 切换页面，**F12** 冻结画面让数字别跳）。
`0` 关闭时零开销；非 TIMING 版本只有打开 HUD 才采样。

> **截图前请确认两件事**：① `dx12_profiling_hud = 1` 或已按 F11 切到 **1/3 Overview**（每页第一行都写着 `page N/3`）；② **hunting 关掉（Num0）**，否则 HUD 只会占屏幕上方 70%（见第 7 节 `kHudHuntReserveFrac`），底部的行会被裁掉。

### 三个页面（F11 循环，第一行永远写着 `page N/3`）

| 页 | 名字 | 回答的问题 | 内容 |
| --- | --- | --- | --- |
| 1/3 | `Overview` | **这一帧的时间去哪了** | 左列：按 `/frame` 排序的耗时表；中列：各域占比与命中率；右列：每帧一次的固定开销（Present/Create/Queue/Wait）。**这一页是截图首选。** |
| 2/3 | `Buckets` | 哪个 bucket 在烧时间 | 全表按总耗时排序（行数够时 138 行也全在屏上），三列切分，每行带 `/f`、每帧次数、`max` 与 `!`/`~` 标记 |
| 3/3 | `Counters` | 计数器和验证计数说了什么 | 左列：每帧一次性开销明细；中列：带 items 的计数器（按每帧 items 降序）+ 窗口总计；右列：**`Rm.*` 验证计数器**（按调用次数降序），用来看某个替换/门控是不是真的在生效 |

三页都是三列、都铺满整屏；改版前的 6 页（Snapshot/Compact/Summary/Top/Sections/Counters）已合并进这三页，`dx12_profiling_top_rows` 这个只喂 Top/Sections 的旋钮一并删除。

---

## 0. 第 1 行：`build` 与窗口

```
== page 1/3 Overview == build 2026-09-12 09:47:22 | fps=59.9 frameWall=16.67ms (max 33.20ms) | present=123456 window=60 frames (~1000ms) | flags=0x1f | F11 = next page
```

| 字段 | 含义 |
| --- | --- |
| `page N/3` + 名字 | 当前第几页、共几页。**截图先看这里**；数字对不上时按 F11 翻页确认。 |
| `build <日期 时间>` | **这份 DLL 的编译时间戳**（`__DATE__`/`__TIME__`）。截图先看它，确认你测的是不是我刚构建的那份 DLL；不是就重装再截。 |
| `flags=0x..` | 当前钩子工作量标志字（`DX12HotPathState::HookWorkFlags`）。**所有数字都受它影响**：`0x0` ≈ 快路径全开（最有参考价值），非 0 表示还有 hunt / 捕获 / 预蒙皮探测等工作在做。 |
| `present=` | **绝对** Present 计数（进程启动至今），用来对齐日志里的 `"present":N`。 |
| `window=N frames (~X ms)` | 本报告统计了**多少帧**（所有 `/frame` 的分母），括号里是这个窗口的真实墙钟时长。**窗口按帧数计**，默认目标 60 帧，见下面的说明。 |
| `fps` | 窗口内平均帧率。 |
| `frameWall=` | 帧起→下一帧起的平均墙钟（ms），**即真正的帧时间**。 |
| `maxFrameWall=` | 窗口内最差的一帧（ms）。它远大于 `frameWall` 就是卡顿。 |

### 窗口现在是"60 帧"，不是"500ms"

| 常量（`DX12ProfilingHud`） | 默认值 | 作用 |
| --- | --- | --- |
| `kWindowFrames` | `60` | **一个报告覆盖的帧数**。到了就出报告。 |
| `kWindowMinFrames` | `30` | 兜底窗口的**最小**帧数，任何情况下窗口都不会比它短。 |
| `kWindowFallbackFps` | `20` | 兜底时间上限 = `kWindowFrames / 20` 秒 = 3 秒（60 帧时）。`dx12_profiling_interval_ms` 比它大就用配置值。 |

规则：**够 60 帧就出报告**；如果帧率太低（<20fps）导致 60 帧超过 3 秒，则时间到且**已满 30 帧**时出报告。所以窗口只会是 60 帧，或在低帧率时是 30~60 帧，**不会**再出现以前那种 15 帧的窗口。

为什么改：以前是"每 500ms 出一次"，30fps 的机器一个窗口只有 15 帧，144fps 有 72 帧。`max` 是**窗口内最大值**，15 帧里最大的和 72 帧里最大的根本不是同一个统计量 —— 同一个 bucket 在不同机器/不同场景下会给出不同的尖峰，屏幕上抖得最厉害的就是这一列。改成固定帧数后，两次报告、两个 session 之间的 `max` 和 `/frame` 均值可以直接对比。

> `dx12_profiling_interval_ms`（默认 500）仍然有效，但它现在只表示"我至少要多久看到一次报告"（兜底），**只能把窗口拉长，不能把它缩短**。

---

## 1. 第 2–3 行：总量与交叉校验

```
OUR TOTAL=2.31 ms (13.9% of frameWall) [root 42 of 134 rows] | ALL BUCKETS=52.98 ms (317.9%, double-counted upper bound) | draws 4210/f | dispatch 96/f
frameWall=16.67 ms | outsidePresent=14.82 ms | Present hook=1.85 ms | Present slices known=1.20 ms (unknown 0.65 ms) | outsidePresent-OUR TOTAL=12.51 ms | time rows 65/134 shown
```

### 1.1 两个总量（并排显示，缺一不可）

| 字段 | 确切定义 |
| --- | --- |
| `OUR TOTAL` | **去重后的"我们的"时间**：Σ(每个 bucket 的 `rootUs`) ÷ 帧数。`rootUs` = 该 bucket 的样本中，**计时那一刻整条被埋点调用树里没有别的 scope 打开**（RAII scope 计数与 `Begin/End` 位置栈都为空）的那些样本之和。也就是"每个区间只算一次"。 |
| `(%.1f% of frameWall)` | 上面这个数占 `frameWall` 的百分比。 |
| `[root N of M rows]` | 有 N 行贡献了 root 时间（进 `OUR TOTAL`），左列一共 M 行"我们的"行。N<M 说明其余行被嵌套在别的行里（行尾带 `~`）。 |
| `ALL BUCKETS` | **所有"我们的"行的时间直接相加**（就是以前的 `SUM ours`）。嵌套 scope 会被自己的行算一次、又被每一层外层行再算一次，所以它是**上界**，超过 100% 是正常的、也是预期的。保留它是为了能一眼看出"重复计量有多严重"。 |
| `draws / dispatch` | 每帧 Draw / Dispatch 次数。 |
| `DROPPED FRAMES` | 只在有帧摘要被丢弃时出现，表示本窗口数据不完整。 |

**两个数各自回答什么**：
- `OUR TOTAL` 回答"我们的代码这次到底吃掉了多少帧时间"——它是**可用的那个数**。
- `ALL BUCKETS` 回答"表里所有行加起来是多少"——它必然 ≥ `OUR TOTAL`，两者**差距越大，说明嵌套/重复计量越严重**（比如 `Api.DrawIndexedInstanced ⊃ Stage.DrawHook ⊃ Stage.DrawOverride ⊃ Stage.DrawOvIaStateNegCache`，一次 draw 的时间会被算 4 遍）。

`OUR TOTAL` 的排除项（都写在图例里）：
1. **嵌套行**（行尾 `~`）：它的时间已经在父行里了。
2. **游戏自己的东西**：`Present.original_present(_skip)`（游戏自己的 Present）、`Wait.system` / `Wait.event`（游戏自己在等）、`Queue.original*`（游戏真正的 ExecuteCommandLists）。它们装在 DLL 与否都会发生。
3. **纯等待**：`Wait.*` 与 `Worker.wait.*`（我们的工作线程在等任务）。它们是墙钟时间但**不是 CPU 时间**，加进来会让 OUR TOTAL 恒等于一帧。
4. `ScopePair`（同一区间记两个名字的写法，目前没有调用点）只把**第一个名字**计入，第二个算别名。

> **`OUR TOTAL` 仍然可能 > 100%**，此时它是**跨线程的 CPU 时间之和**（多个线程并行跑同一帧，各自计时相加自然超过帧墙钟）。这种情况在同一行末尾会出现 `<-- our root total exceeds it: duplicate or parallel work`。要区分"并行"还是"还有重复计量"，就看下一节的交叉校验。

### 1.2 交叉校验行

| 字段 | 确切定义 |
| --- | --- |
| `frameWall=` | 帧起→下一帧起的平均墙钟。恒等于 `outsidePresent + Present hook`。 |
| `outsidePresent=` | 不在我们 Present 钩子里的那部分帧时间的平均值（钩子外的墙钟）。**我们的大部分 bucket（draw / IA / 描述符）都在这里跑**。 |
| `Present hook=` | 我们 Present 钩子整段墙钟的平均值（含钩子里游戏自己的 Present，但 `Present` 这个 bucket 自己在游戏 Present 期间是 Pause 的）。 |
| `Present slices known=` | Present 钩子里**已知切片**之和（所有 `Present.*` 子桶，除 `post_original_runtime*`/`hunt_on_present`/`post_present_ui`）。 |
| `(unknown X)` | `Present hook − Present slices known`，钳到 0：钩子里**没能归因**的那部分。为 0 表示切片已经把钩子解释完了（切片之间有重叠时它也会是 0，这时看 `known` 是否 ≥ `hook`）。 |
| `outsidePresent-OUR TOTAL=` | **交叉校验的核心**：`outsidePresent − OUR TOTAL`，即帧里"钩子外、又不算我们"的墙钟 = 游戏自己的 CPU。**它是负数**说明 `OUR TOTAL` 比它该待的地方还大 → 要么多线程并行，要么还有重复计量（看 `ALL BUCKETS` 涨了多少）。 |
| `time rows N/M shown` | 左列表显示了 N 行，实际有 M 行（M>N 说明被行数预算截断，剩下的都是更小的行）。 |

---

## 2. 第 4–5 行：固定图例（必须能读到的规则）

```
units: <1000us -> "us", >=1000us -> "x.xx ms" | /frame = window total / frames | "!" = row max >= 1000us | "~" = row fully nested (inside another row, counts in that row, not in OUR TOTAL)
OUR TOTAL = every top-level scope once (nested rows excluded; game's own Present/Wait and idle waits excluded) | ALL BUCKETS = all rows summed = double-counted upper bound, >100% is normal
```

- **单位规则**：任何耗时 < 1000us 显示成整数 `123 us`；≥ 1000us 显示成 `1.23 ms`。帧时间（`frameWall` 等）**永远是 ms**。
- **`/frame`**：该数字 = 本窗口累计值 ÷ 窗口帧数（分母就是第 1 行的 `window=`）。
- **`!`**（左列/右列行尾第 1 个标记位）：该行的 **max ≥ 1000us**，即这一行在本窗口里至少出现过一次 1ms 以上的单次尖峰。这是"一眼找尖峰"的标记。
- **`~`**（行尾第 2 个标记位）：该行**整行都是嵌套的**（它的每一次计时都在别的行里面），所以它**不贡献 `OUR TOTAL`**。行还在表里、还能排序，只是"这份时间已经记在父行头上了"。两个标记可以同时出现（`!~`）。
- 字体是 ASCII 点阵，所以图例、列名都是英文；中文只存在于本说明书。

---

## 3. 左列：耗时表（按 ms/frame 降序）

```
bucket                          ms/frame  calls/fr       avg      max  f
Desc.shadow_copy                 7.00 ms   30.00/f    233 us   900 us
Api.DrawIndexedInstanced         1.22 ms    0.90/f   1.35 ms  2.10 ms
Stage.DrawHook                   1.23 ms    0.95/f   1.29 ms  2.05 ms  ~
Desc.copy_blocks_patch           2.00 ms    1.00/f   2.00 ms  4.20 ms  !
...
  ... +71 more rows, ranked by ms/frame
SUM ours  root 2.31 ms 13.9% | all 52.98 ms 317.9% of frameWall
  rows  root 42, all 134, shown 60
```

| 列 | 含义 |
| --- | --- |
| `bucket` | 埋点名。前缀含义：`Present.` 帧边界/Present 各切片、`Queue.` ExecuteCommandLists 包装、`Stage.` 热路径阶段、`Api.` API 钩子、`Create.` 创建、`Lock.` 锁、`Wait.` 等待、`Fence.` 同步、`Worker.` 后台线程、`Desc.`/`Rm.` 描述符与常驻维护。 |
| `ms/frame` | **本窗口该 bucket 总耗时 ÷ 帧数**，自动单位。排序依据就是它。 |
| `calls/fr` | **本窗口调用次数 ÷ 帧数**（不是总次数）。 |
| `avg` | 单次平均 = 总耗时 ÷ 总调用数，自动单位。 |
| `max` | 窗口内**单次最大**耗时，自动单位；≥1000us 时打 `!`。 |
| `f` | 两个标记位：第 1 位 `!` = `max ≥ 1000us`；第 2 位 `~` = 整行嵌套（不进 `OUR TOTAL`）。 |

**表尾两行**：
- `SUM ours  root A x.x% | all B y.y% of frameWall`：`root` 就是标题里的 `OUR TOTAL`，`all` 就是 `ALL BUCKETS`，两个百分比都是相对 `frameWall`。两行数字来自同一次统计（`BuildWindowReport`），**不可能互相矛盾**。
- `rows  root N, all M, shown K`：能进 `OUR TOTAL` 的行数 / 所有"我们的"行数 / 实际打印出来的行数。若 `shown < all`，表里会先打印一行 `... +K more rows`（被截断的都是更小的行）。

**读法**：先扫 `ms/frame` 最大的 3~5 行——它们就是"每帧固定损失"；再扫行尾的 `!`——那是"偶发尖峰"，两者要分开修。行尾有 `~` 的大行**不要单独当成我们的净开销**，它已经算在它外层的行里了。

---

## 4. 中列：比率 / 计数（每行 `名字 | 每帧次数 | 命中率或比值`）

所有数字都**除以帧数**。百分比一律是"本窗口累计值之比"，与帧数无关。**每个"分子/分母"对都在带分子的那一行上给出百分比**，并且用同一种写法 `名字=xx.x%`（比值 <1% 时保留两位小数，避免 0.02% 显示成 0.0%）。横线是分组标题。

### 4.1 `-- IA negative cache --`（IA 负缓存）

| 行 | 含义 / 计算式 |
| --- | --- |
| `IaNegCacheHit` | `Rm.IaNegCacheHit` 每帧次数。旁边 `hit=...% of (hit+runs)` = `IaNegCacheHit / (IaNegCacheHit + IaNegVerdictRuns)`：**负缓存命中率，越高越好**。 |
| `IaNegVerdictRuns` | `Rm.IaNegVerdictRuns`：真正跑了负判定的次数。 |
| `IaNegVerdictNegative` | `Rm.IaNegVerdictNegative`：判定为"负"（确实不用做替换）的次数；`neg=...% of runs`。 |
| `IaNegVerdictGated` | `Rm.IaNegVerdictGated`：被门控挡掉的次数；`gated=...% of runs`。 |

### 4.2 `-- PreSkin verdict --`（pre-skin 判定层）

| 行 | 含义 / 计算式 |
| --- | --- |
| `VerdictDispatchHits` | `Rm.PreSkinVerdictDispatchHits`：**整条判定路径的入口次数**，下面所有 `share=` 的分母。 |
| `VerdictEarlyHits` / `VerdictDescriptorHits` / `VerdictResidentHits` / `VerdictTemplateHits` / `VerdictTemplateFpHits` / `VerdictGeometryHits` | `Rm.PreSkinVerdict{...}`；每行 `share=...% of verdicts` = 该层命中数 ÷ `VerdictDispatchHits`，即**这条判定层扛了多少流量**。 |
| `VerdictGeometryHits` | 另外还带 `geom/key=...%` = `GeometryHits / VerdictKeyMatches`：**匹配上判定键的 draw 里，有多少真的命中了 geometry 层**。 |
| `VerdictKeyMatches` | `Rm.PreSkinVerdictKeyMatches`；`share=...% of verdicts`。 |
| `VerdictBindingInvalid` | `Rm.PreSkinVerdictBindingStateInvalid`：判定键对应的绑定状态失效（pre-skin 误判/重建的来源）；`share=...% of verdicts`，非 0 要查。 |
| `IdentityProbeRuns` / `IdentityProbeHits` | `Rm.PreSkinVerdictIdentityProbeRuns/Hits`；`hits/runs=...%`，越高说明身份探测越省。 |
| `PlanCacheLookups` | `Rm.PreSkinPlanCacheLookups`；旁边 `negHit=...% of lookups (x.xx/f)` = 负命中的比例与每帧次数。 |

**读法**：6~7 个 `share=` 加起来应当≈ 100%（`VerdictDispatchHits` 自己就是 100%）；哪一路 `share` 最大，说明那条判定路径在扛主要流量。`VerdictBindingInvalid` 高或 `IdentityProbeHits/Runs` 低 = 判定层不稳，会掉到慢路径。

### 4.3 `-- PreSkin store --`（pre-skin 绑定缓存写入）

| 行 | 含义 / 计算式 |
| --- | --- |
| `BindingCacheCalls` | `Rm.PreSkinBindingCacheCalls`；`Stores/Calls=...%` = 写入率（越低越省）。 |
| `BindingCacheHits` | `Rm.PreSkinBindingCacheHits`；`hit=...% of calls`（**缓存命中率，越高越好**）。 |
| `BindingCacheMisses` | `Rm.PreSkinBindingCacheMisses`；`miss=...% of calls`。`hit + miss` 应当≈ 100%。 |
| `BindingCacheStores` | `Rm.PreSkinBindingCacheStores`；`committed=...% of stores` = `StoreCommitted / Stores`。 |
| `StoreCommitted` | `Rm.PreSkinStoreCommitted`：真正提交的写入。 |
| `StoreStillInvalidAfter` | `Rm.PreSkinStoreStillInvalidAfterCollect`：收集完仍然无效 ⇒ **每次都是白跑**；`stillInvalid=...% of stores`，非 0 要查。 |
| `StoreValidatePass` | `Rm.PreSkinValidatePass`；`passed=...% of stores`。 |
| `Xxx  <-- FAILURE` | 只显示**非零**的失败计数，并且**按每帧发生次数从多到少排序**（最多 `kPreSkinFailureRows` = 6 行）。涉及 `Rm.PreSkinStore{NoSnapshot, NotStable, RuntimeDescriptorsUnstable, ObservedSlotsInvalid}` 与 `Rm.PreSkinValidate{NoBindings, Heap, RootSignature, RootDescriptor, Constants, SlotCountMismatch, NoObservedHeap, TableUnresolved, SlotOutOfRange, HeapRecordUnresolved, SlotIdentityUnreadable, SlotValueId, SlotVersion, SerialChurn, NoContext, NoSerial}`。 |
| `store/validate failures: none this window` | 本窗口所有失败计数都是 0（健康）。 |
| `(+N more non-zero, past the top-6 cut)` | 非零的失败计数超过 6 个，**排在后面的**没占屏幕（数量仍然重要，说明问题在扩散）。 |
| `(+N more failure counters, all zero)` | 还有 N 个失败计数为 0，没占屏幕。 |

### 4.4 `-- descriptor copy --`（描述符拷贝 / 堆影子）

| 行 | 含义 / 计算式 |
| --- | --- |
| `DescCopyChanged` | `Rm.DescCopyChanged`；`changed/(ch+un)=...%` = **拷贝命中率**（变了才拷，越高说明拷贝没白做）。 |
| `DescCopyUnchanged` | `Rm.DescCopyUnchanged`；`unchanged=...% of copies`（应当与上面互补到 100%）。 |
| `DescCopyDirectPath` | `Rm.DescCopyDirectPath`；`direct=...% of copies`（不走锁的比例），旁边 `locked=x.xx/f` = `DescCopyLockedPath` 每帧次数。 |
| `DescCopyLockedPath` | `Rm.DescCopyLockedPath`（走锁的慢路径）。 |
| `DescriptorHeapChanged` | `Rm.DescriptorHeapChanged`；旁边 `ResolveFailure=x.xx/f (fail=...%)` = `Desc.ResolveFailure` 的每帧次数与占 `DescriptorHeapChanged` 的比例（**非 0 就是解析失败**）。 |
| `Lock.HeapCacheHit` | `Lock.Desc.HeapCacheHit`（堆影子缓存命中）。 |
| `Desc.CopySlots(items)` | `Desc.CopySlots` 的 **items**（槽位数）÷ 帧数；`slots/copy=x.x` = 每次拷贝平均搬几个槽位。注意这是 items 计数，不是调用数。 |
| `Desc.CopyBlocks(items)` | `Desc.CopyBlocks` 的 items ÷ 帧数；`blocks/call=...` = `CopyBlocks.items / (DescCopyChanged + DescCopyUnchanged)`。 |

### 4.5 `-- draw / dispatch fast path --`（快路径命中情况）

| 行 | 含义 / 计算式 |
| --- | --- |
| `FastForward` | `FastForward`：直接转发给原函数的次数（越多次越快路径越干净）。 |
| `DrawNoopFastPathAttempts` | 尝试走 no-op 快路径的次数。 |
| `DrawNoopFastPathForwarded` | 成功转发；`forwarded=...% of attempts`（**快路径成功率**）。 |
| `DrawNoopNonNoopDecision` | 快路径里发现"并不是 no-op"，只好退回慢路径；`nonNoop=...% of attempts`（**这个数涨 = 快路径判据不够准**）。 |
| `IaBindFastPath` / `IaBindSlowPath` | `Rm.IaBindFastPath` / `Rm.IaBindSlowPath`；`fast=...% of ia binds` = Fast/(Fast+Slow)。 |
| `IaPreSkinRedirectGated` | `Rm.IaPreSkinRedirectGated`：被 pre-skin 重定向门控挡掉；`gated=...% of ia binds`。 |
| `PreSkinRedirectNegHit` | `Rm.PreSkinRedirectNegHit`；`negHit/(hit+miss)=...%`。 |
| `PreSkinRedirectNegMiss` | `Rm.PreSkinRedirectNegMiss`（未命中 ⇒ 要重新判定）。 |
| `PreSkinRedirectViewNegHit` | `Rm.PreSkinRedirectViewNegHit`；`view=...% of redirects` = ViewNegHit/(NegHit+NegMiss+ViewNegHit)。 |

---

## 5. 右列：尖峰 / 帧信息

```
SPIKES / FRAME   (window 60 of 60 frames, 1000 ms)
-- top by max (ours) --
bucket                               max  calls/fr  f
Desc.copy_blocks_patch           4.20 ms    1.00/f  !
...
-- frame --
frameWall           16.67 ms mean / 33.20 ms max
```

| 行 | 含义 |
| --- | --- |
| `(window N of M frames, X ms)` | 本窗口实际帧数 / 目标帧数（`kWindowFrames`）/ 窗口墙钟时长。低帧率兜底时 N<M，此时**窗口比目标短**，`max` 的样本也少。 |
| `-- top by max (ours) --` | **按单次最大耗时降序**的前 N 个 bucket（`kTopN`，默认 12）。`max` 自动单位，`calls/fr` 是该 bucket 的每帧调用数。只统计"我们的"bucket（不含游戏自己的 Present/Wait）；`f` 两个标记位同第 3 节。 |
| `frameWall` | 帧时间：均值 / 最大值（ms）。 |
| `outsidePresent` | Present 之外的帧时间均值。 |
| `Present (hook wall)` | 我们 Present 钩子整段墙钟：均值 / 最大值。 |
| `Present.original` | `Present.original_present`：**游戏自己的** Present，每帧均值 + 单次最大（只观测，不归我们）。 |
| `Wait.system` | `Wait.system`：每帧次数 + 单次最大。**这是游戏自己在等（驱动/系统等待），我们只是把它观测出来**；它大说明是游戏/驱动的问题。注意它统计的是**所有线程**的等待，一个 1 秒的超时会把窗口总量顶到很大。 |
| `Wait.event` | `Wait.event`：同上，事件等待。 |
| `Fence.op` | `Fence.op`：每帧次数 + 单次最大（我们的同步开销）。 |
| `ExecuteCmdLists` | `Queue.wrapper_total`：每帧调用数 + 每帧提交的 command list 数 + 单次最大。 |
| `Queue.Wait` / `Queue.Signal` | 队列等待/信号：每帧次数 + 单次最大。 |
| `HUD text record` | `Present.overlay_text_record`：**这张面板自己**的每帧开销与尖峰。 |
| `profiling flush` | `Present.profiling_and_hotpath_flush`：采集/汇总线程的开销。 |
| `dropped summaries` | 只在非 0 时出现：本窗口有帧摘要被丢，数据不完整。 |

---

## 6. 拿到一张截图，按这个顺序看（排查顺序）

1. **`build`** —— 是不是我要的那份 DLL？不是就别往下看了。
2. **`window=` / `HookWorkFlags`** —— 窗口是 60 帧吗（低帧率兜底时会更少）？HookWorkFlags 是不是最低？非 0 说明开了 hunt/捕获，数字不能当性能结论用。
3. **`fps` / `frameWall` / `maxFrameWall`** —— 现在到底多少帧？`maxFrameWall` 是否远大于 `frameWall`（有周期性卡顿）。
4. **`OUR TOTAL` 与 `ALL BUCKETS` 的差距** —— 差距大 = 表里嵌套严重（正常现象，但要知道）；`OUR TOTAL` 才是"我们吃掉多少帧"。
5. **`outsidePresent-OUR TOTAL`** —— 正数大 = 帧时间主要是游戏自己的 CPU；**负数** = 我们（跨线程）的时间比钩子外的墙钟还多，看是不是多线程并行，或者还有没去重的计量。
6. **左列前 5 行（跳过 `~` 行）** —— 每帧固定损失在哪。带 `~` 的行是嵌套行，别重复计数。
7. **左列 + 右列的 `!`** —— 尖峰在哪。`!` 行和 Top-by-max 第一名就是"卡顿的元凶"，注意它 `calls/fr` 很低（1/f 甚至 0.07/f）时是**偶发**问题，不要按每帧平均去修。
8. **中列的百分比** —— 定位"为什么慢"：命中率低的那一行（`hit=`、`miss=`、`changed/`、`fast=`、`passed=`、`share=`）就是慢路径的来源；中列出现任何 `<-- FAILURE` 优先查它（已按发生频率排序）。
9. **右列 `frame` 块** —— `Wait.system` / `Present.original` 大 = 游戏自己的成本；`ExecuteCmdLists` 大 = 队列提交侧。
10. **`HUD text record` / `profiling flush`** —— 确认 HUD 自身开销可忽略（正常应在几十 us/帧以内）。

---

## 7. 想调整布局 / 窗口？只改两个地方

### 7.1 布局常量

全部集中在 `src/DirectX12/Core/Diag/DX12ProfilingHud.h` 的 `struct DX12ProfilingLayout`，改完重新构建即可：

| 常量 | 默认值 | 作用 |
| --- | --- | --- |
| `kHudFontScale` | `0.80f` | **字体总开关**。16px 点阵按这个比例绘制（0.80 ≈ 12.8px 高、6.4px 宽）。往下调 = 更多行（每 0.05 约多 4 行 @1080p），但字会更糊；往上调更清晰、行更少。 |
| `kHudRowGapPx` | `1.4f` | 行间距（像素，不随字体缩放）。 |
| `kColumnX[3]` | `{0, 0.34, 0.67}` | 三列左边缘占可用宽度的比例（分辨率无关）。**列宽不再由常量决定**，而是渲染端按下一列起点实测（见下）。 |
| `kTopN` | `12` | Overview 右列尖峰榜的行数。 |
| `kRefreshFrames` | `1` | 每 N 帧最多重读一次报告。默认 1（每帧），因为报告本身每个窗口才变一次、几何体有内容缓存，改成 >1 基本省不到东西。 |
| `kTitleLines` | `5` | 表格上方的整行显示行数（页眉、总量行、交叉校验行、两行图例）。行数预算按它预留。 |
| `kPreSkinFailureRows` | `6` | 中列最多列几个非零失败计数（按次数降序取前 N）。 |
| `kMaxReportLines` | `96` | 行数**上限**（覆盖 1440p），实际行数由渲染端实测。 |
| `kMaxColumnChars` | `150` | 单列字符**上限**（覆盖 2560 宽屏），实际列宽由渲染端实测。 |
| `kMaxReportChars` | `14000` | 一份报告的字符上限：覆盖层一帧只有 15360 个字形（16 位索引到 16384 为止），还要留给狩猎文字。行数预算由它反推，所以到上限只会**少几行**，不会丢尾巴。 |

**列宽与行数不再由常量猜**：渲染线程知道后缓冲尺寸和实际字宽，每帧量一次"这一屏能放多少行、每列放多少个字符"，变化时发布成 `DX12ProfilingBudget`（`DX12ProfilingHud::PublishBudget`），写线程照它排版（`DX12ProfilingText::Format`）。所以 1280×720 窗口和 2560 宽屏都能排得下，不会再出现"按 1920 排好、在 720p 上三列互相压字"；渲染端还会把超宽的行**裁到本列宽度**，被裁的行末尾标 `>`（列里被截断也标 `>`）。

当前分辨率下的实际行数：**1280×720 → 45 行（占满整屏）**；1080p → 43 行（屏上还能放 70 行，但一份报告的字符上限只够 43 行 × 三列满宽）。想让 1080p 也铺满，需要把覆盖层的索引缓冲从 16 位换成 32 位并把 `kMaxChars`/`kMaxReportChars` 一起放大（显存约翻倍到 12MB）。

### 7.2 窗口常量（`DX12ProfilingHud`，不在 Layout 里）

| 常量 / 配置 | 默认值 | 作用 |
| --- | --- | --- |
| `kWindowFrames` | `60` | 一个报告覆盖的帧数。想要更稳的 `max` 就调大（比如 120），代价是报告更慢、更迟钝。 |
| `kWindowMinFrames` | `kWindowFrames/2` | 兜底窗口的最小帧数。 |
| `kWindowFallbackFps` | `20` | 兜底时间上限 = `kWindowFrames / kWindowFallbackFps` 秒。 |
| `dx12_profiling_interval_ms` | `500` | 兜底间隔。**只能把窗口拉长**：它小于 `kWindowFrames/kWindowFallbackFps` 时不起作用。 |

另外 `src/DirectX12/ShaderTools/DX12NativeOverlay.cpp` 里还有两个和"能不能全屏"有关的常量：

| 常量 | 默认值 | 作用 |
| --- | --- | --- |
| `kHudHuntReserveFrac` | `0.30f` | **hunting 开着时**给狩猎文字留的高度比例（HUD 只用上面 70%）。**截性能图时请把 hunting 关掉**（Num0），HUD 就能用满整屏。 |
| `kMaxChars` | `15360` | 覆盖层一帧最多画多少字形（16 位索引的硬上限是 16384）。报告的字符预算 `kMaxReportChars` = 14000（含列标记与换行，实际字形更少），留了余量给狩猎文字。改大要同步 `kVertexSlotCount`/显存，并改索引格式。 |

字号上限与显存：顶点环按上限（`kMaxChars` = 15360 字形）分配，约 8 MB 上传堆；这是 HUD/覆盖层初始化时一次性分配的，与帧率无关。要改字体真实清晰度（而不是缩放），则改 `DX12NativeOverlay.cpp` 里 `BuildFontAtlas` 的 `CreateFontW(-16, ...)`，并把 `kBaseCellHeightPx` 同步成新的字高。

---

## 8. 这一版对日志（TIMING emit）的影响

窗口与两个 sink 的关系没变：**一次统计，两个出口** —— 写线程每关一个窗口就构造一份 `DX12ProfilingWindow`，HUD 画它，日志写它。所以：

1. **`func=DX12FrameTimeline` 的频率跟着窗口走**：以前是每 500ms 一条，现在是**每 60 帧一条**。60fps 下约每秒 1 条（原来是每秒 2 条，**日志量减半**）；30fps 下约每 2 秒 1 条；低帧率时由兜底触发（最少 30 帧），日志行数**明显变少**。取舍：这是为了让日志里的 `"frames":N` 恒定、`maxFrameWallUs` / 各 bucket 的 `maxUs` 之间可以跨行比较 —— 代价是低帧率时汇总来得慢。
2. **`func=DX12FramePulse` 不受影响**：仍然是每帧一条（它只报 `frameIntervalUs` / `outsidePresentUs` / `presentWallUs`），逐帧时间线照样完整。
3. **`func=DX12ApiWallTiming` / `DX12HotPathTiming` / `DX12CreateTiming` / `DX12CpuStageTiming` / `DX12ModLockTiming` / 计数段**：格式**逐字段不变**，但这些段本身**不带帧数**，只带 `"frameWallUs"`（本窗口的墙钟总和）。要按帧归一，得配同一时刻的 `func=DX12FrameTimeline` 行看 `"frames"` —— 而那一行的 `"frames"` 现在恒等于窗口目标帧数（60，兜底时是 30~60），所以新旧日志对比时不要假设它是 15/16。
4. **`func=DX12FrameTimeline` 新增字段**：
   - `"ourRootUs"` / `"ourAllUs"`：屏幕上的 `OUR TOTAL` / `ALL BUCKETS`（窗口累计值，都是微秒）；
   - `"ourRootRows"` / `"ourAllRows"`：对应的行数；
   - `"windowTargetFrames"`：窗口目标帧数（`kWindowFrames`），用来判断这一行是不是低帧率兜底产生的短窗口。
   这些字段是**增量添加**的，原有字段（含 `"frameWallUs"` / `"outsidePresentUs"` / `"presentWallUs"` / `"presentSliceKnownUs"` / `"presentWallMinusKnownSlicesUs"`）名称、顺序、单位都没动。
5. **`func=DX12Profiling` 的 `name,calls,totalUs,avgUs,maxUs,items` 表**：**没有加列**（`rootUs` 只在屏幕上用）。它默认不写（`dx12_profiling_log_buckets=0`）。
6. **埋点语义零改动**：`DX12_PROF_*` 记的还是同样的 `calls` / `totalUs` / `maxUs` / `items`，bucket 归并规则、counter 计数语义都没变。`rootUs` 是**新增的第二个累加器**（`Bucket` 从 56 字节变 64 字节，正好填满原有 padding），只影响展示。

---

## 9. 已知限制

- **`OUR TOTAL` 是跨线程 CPU 时间之和**：多线程并行时它仍然可能超过 `frameWall`（此时交叉校验行会给出提示）。它不是"独占的墙钟开销"。
- **最外层区间的时长包含被包装的原始调用**：例如 `Api.DrawIndexedInstanced` 覆盖"我们的钩子 + 游戏原本的 Draw 调用"，`Present hook` 覆盖整个 Present 钩子（但 `Present` 这个 bucket 在调用游戏自己的 Present 期间是 Pause 的）。也就是说 `OUR TOTAL` 里的这些区间是"这次调用整体花了多久"，不是纯粹的 mod 代码耗时。
- **`rootUs` 的父子判定靠埋点自身的作用域嵌套**：RAII scope（`DX12_PROF_SCOPE` / `DX12_PROF_STAGE_SCOPE` / `DX12_PROF_API_SCOPE` / `DX12_PROF_CREATE_SCOPE`）用一个计数，`DX12_PROF_STAGE_BEGIN/END` 用它们本来就有的位置栈。计数被封顶为 32 且只会往下漂（漏配对的 `Begin/End` 最多让嵌套判定变保守 = `OUR TOTAL` 略微偏大），不会凭空变小。
- **`ScopePair`（一个区间记两个名字，目前没有调用点）**：只把第一个名字计入 `OUR TOTAL`，第二个当成别名，避免同一段时间被算两次。
- **HUD 自己也在被测量**：`HUD text record` / `profiling flush` 两行就是它自己的成本，判断结论时先看这两行。
- **字体只有 ASCII 32..126**，所以屏幕上是英文；中文解释只在本文件里。
- **报告每个窗口更新一次**（默认 60 帧）；`F12` 冻结显示但采样与日志照旧。**低帧率兜底产生的短窗口**在右列标题里能看出来（`window N of M frames`）。
- 截图时请保持游戏画面与 HUD 都在（HUD 半透明黑底），分辨率 1080p 及以上，`kHudFontScale` 保持默认时字最小可读。
