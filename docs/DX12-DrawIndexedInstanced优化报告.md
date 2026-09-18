# DrawIndexedInstanced 热路径优化（IA 负判定缓存 + IA 视图哈希）

## 基线与现象

依据用户提供的 Overview 截图（build 2026-09-18 07:31:43，38 FPS）与构建前的
`D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt`（已轮转为
`d3d12_log.before_20260918-075737.txt`，23 MB）。只读取采样与指定统计窗口，不整文件载入。

截图与日志的同一窗口（`present=7078`，60 帧窗口，行内为每帧值）：

| bucket | ms/frame | 说明 |
|---|---:|---|
| `Api.DrawIndexedInstanced` | 3.03 | 1001 calls/frame，3.07 µs/次 |
| `Stage.DrawHook` | 3.00 | 嵌套于上 |
| `Stage.DrawOverride` | 2.52 | 嵌套 |
| `Stage.DrawOvIaStateNegCache` | 1.44 | 嵌套，1026 次/frame 进入 |
| `Stage.DrawOvIaNegVerdict` | 1.12 | 572 次/frame 真正跑判定 |
| `Stage.IaNegVerdictHashes` | 1.01 | 判定内的取哈希+预过滤门 |
| `Stage.DrawModPrepareOverride` | 0.65 | 仅 3.93 次/frame（真正命中 overrides 的 draw） |

同窗口计数器：

- `IaNegCacheHit` 453.9/frame，`IaNegVerdictRuns` 571.8/frame → **IA 负判定缓存命中率 44.2%**。
- `IaViewHashSlotRebuild` 1181.1/frame，`IaViewHashSlotReuse` 508.7/frame → 每帧重建 1181 个视图哈希。
- `DrawNoopFastPathAttempts` 1057.7/frame，其中 `DrawNoopFastPathNonNoopDecision` 占满 → 每个 draw 都进通用路径。

嵌套 scope 不能相加；TIMING 版自身的计数与日志有成本，跨线程 CPU 耗时也不等于单帧关键路径。
下文微基准只支持局部结论，不换算成游戏帧率。

## 根因

### 1. 负判定缓存的键里放了一个每帧都动的代际（主因）

`Stage.DrawOvIaStateNegCache` 占 1.44 ms/frame，其中 1.12 ms 是**本可命中的判定重算**。这个缓存
（`DX12IaStateNegativeCache`）的签名包含 `DX12ModGetPreSkinActiveGenerationFast()`，而该代际：

- `RunPostPresentRuntime` 每帧调用一次 `ClearActivePreSkinTextureOverridesLocked`
  （`DX12CommandListExecutor.cpp:184`），该函数在 active map 非空时 `BumpPreSkinActiveGeneration()`；
- 生产者每次重新发布 active 条目且身份变化时也会 bump。

也就是说，**只要 PreSkin 生效，活跃集每帧被清空一次，代际就前进一次**，上一帧存下的全部判定
立刻变成不可达；命中率只能剩下"同帧内重复"的那部分，这正是实测的 44%。

判定真正读的并不是这个代际：`DX12ModIaStateDefinitelyHasNoCandidate` → `IaHashDefinitelyHasNoCandidate`
→ `TextureOverrideMatchCsSatisfied(config, activePreSkinSections)` 只是
`!config.hasCsHash || sections.count(config.section)`——**只看 section 名字集合的成员关系**，
不看某个 section 当前指向哪个资源。而该集合是 active keys ∪ `gConfirmedPreSkinSections`，其中
confirmed 集合按设计跨 Present 保留到重载，所以每帧的清空+重填**不改变这个并集**。

### 2. 视图哈希解析落在节点容器上（次因）

`IaNegVerdictHashes` 1.01 ms/frame 对应 1181 次重建 + 509 次复用。哈希函数自身的 L1/L2/L3 是
`unordered_map`，每次都要走哈希、桶、节点解引用。这类"每个 draw 都要问一次、下一帧又问同样的问题"
的场景，槽位 memo 又必然被重绑定打掉，于是反复付容器遍历的钱。

顺带排除的两项（有数据否定，未改动）：

- `IaHasAnyPrefilterCandidate` 预过滤门：实测 3.4~4.7 ns/次（本 mod 的预过滤表只有个位数条目，常驻缓存），不是成本来源，故未做预计算位图。
- 负判定缓存**容量**不是瓶颈：源码里 512×16 的扩容改动（`DX12IaHashCaches.h`，工作区编辑时间 07:31:11）确实进了被实测的那个二进制（该 TU 的 .obj 编译于 07:31:47~07:32:21，均晚于编辑），命中率仍是 44.3%——说明限制来自键的不稳定，而不是表的大小。

## 改动

### 1. 负判定缓存改键到"活跃 PreSkin section 集合"的身份

- 新增 `gPreSkinSectionSetGeneration` / `DX12ModGetPreSkinSectionSetGenerationFast()`
  （`DX12ModRuntimeInternal.h`、`DX12PreSkinRuntime.cpp`）。
- `ClearActivePreSkinTextureOverridesLocked` 只在**并集真的变小**时 bump（遍历 active map 时顺便判断该名字是否也在 confirmed 里）。
- `StoreActivePreSkinTextureOverrideLocked` 只在 `gConfirmedPreSkinSections.insert(...).second`（名字首次进入并集）时 bump。
- `ClearPreSkinRuntimeState` 清空 confirmed 时一并 bump。
- `DX12IaStateNegativeInputs::preSkinGeneration` 更名为 `preSkinSectionGeneration`，
  签名与精确键同步改名；`DX12IaReplacement.h` 改传新的 section 集代际。

保留 `gPreSkinActiveGeneration` 原语义不动：绑定快照、PSO fast-reject、shader override 等依赖
**生产者资源身份**的缓存继续用它。

### 2. IA 视图哈希前的扁平视图键 memo

`IaViewHashMemo`（`DX12ResourceContentHashing.cpp`，TU 局部类，非函数内定义）：4096 组 × 2 路，
64 B/条目，惰性堆分配（每解析线程 512 KiB）。

- 校验规则与 L1 完全一致而非放宽：已解析视图比较 published identity（published 记录不可变，
  身份代际不变 ⇒ 哈希输入不变，包括"同 GPUVA 重建为不同 descriptor"这一 metadata 哈希看不见的情形）；
  未解析视图只在代际未动时复用，代际一动就回落到 L1 做范围级重校验。
- 每次 L1/L2 命中与末尾计算完都 publish，所以 memo 只回答权威路径刚确认过的值。
- 集合索引取 splitmix64 全雪崩后的低位：初版只混了部分位，512 个视图挤进 64 个组、2 路直接抖动，实测无收益（209→195 ns）；改为全雪崩后为 209→24 ns。

## 验证记录

### 离线回归与微基准（`dev_tests/ia_match_perf_test.ps1`）

该脚本抽取生产函数体编译（真实 D3D 类型、真实缓存/索引/匹配/发布代码，资源解析用 test double），
不需要游戏、GPU 或整项目构建。

```
PASS real declarations/build/publication: owning swap, reload, empty cleanup, lock/generation guard
PASS production negative hash snapshot: target gating, unrelated churn, covered-range refresh
PASS real draw-template Reset moves: 1000 round trips retain table and verdict
PASS exact negative key: swaps, forced collisions, generations, plan, binding, copy, allocation reuse
PASS flat view-hash memo: key fields, identity token, unresolved epoch, republish, ways
PASS production hash/slot bodies: TLS reuse, metadata content writes, VA reuse, miss->resolve
PASS hash-index differential: 26880 cases (wildcard/specific/skip/hash-zero/match_cs)
ALL PASS
```

新增的两条**源级不变量检查**（脚本里会 throw，防止后来的改动悄悄破坏前提）：

- PreSkin section 集合的每一处变更都必须落在三个已审计函数内，且这三个函数都必须 bump 集合代际；
- 视图哈希函数每一条返回哈希的路径都必须 publish memo（缓存答案 2 条 + 末尾计算 1 条）。

微基准（3 轮取中位数）：

| 项目 | 改动前 | 改动后 |
|---|---:|---:|
| 视图哈希解析（槽位 memo 被打掉，512 视图工作集） | 209.1 ns/call | 24.1 ns/call |
| 视图哈希解析（槽位 memo 命中） | 1.8 ns/call | 2.0 ns/call |
| 视图哈希解析（8192 视图，超出 memo 容量） | 无此用例 | 606 ns/call |
| 预过滤门 `IaHasAnyPrefilterCandidate` | 3.1 / 4.5 ns | 3.6 / 4.7 ns |
| 负判定签名+查找（命中 / 未命中） | 58.5 / 44.4 ns | 61.9 / 38.9 ns |

8192 视图那一行是 memo 背后的容器路径本身：工作集超过 memo 容量时它照样要付（复用距离 8192 >
8192 条目），这也是这个 memo 定位为"加速器"而不是"权威"的原因。

### 构建与部署

`build_release_timing_x64.ps1` 完整构建成功（38/38，退出码 0）。已部署：

```text
D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll   4639232 bytes  2026-09-18 07:57:37
SHA256 37457419222D9056471473B21A0FFCD6FEA99F6E317A8A2549D52BC4E39846E6
```

部署文件与 `x64\Release\d3d12.dll` 哈希一致；DLL 内含新增计数器名 `Rm.BufferRangeInvalidations`。
优化前日志保留为 `d3d12_log.before_20260918-075737.txt`。`git diff --check` 通过。未自动启动游戏。

## 游戏内第一次后测（build 07:56:31）：本轮改动实测无效果

用户在同一场景跑出 `d3d12_log.txt`（present 至 7837）。同代窗口、同 stage 对比
（每 60 帧窗口，单位 µs/window）：

| stage | 优化前 (present=7078) | 后测 A (present=7777) | 后测 A (present=7837) |
|---|---:|---:|---:|
| `DrawHook` | 182487 | 186843 | 186880 |
| `DrawOvIaStateNegCache` | 87176 | 89812 | 92068 |
| `DrawOvIaNegVerdict` | 67575 | — | 71217 |
| `IaNegVerdictHashes` | 60277 | 60175 | 63788 |
| 判定次数 | 34309 | 32123 | 33864 |
| `IaNegCacheHit` 占比 | 44.2% | 43.8% | 44.0% |

即：**命中率没动，哈希阶段也没动**。也就是说 `IaViewHashMemo` 在这个场景里几乎没被用上，
PreSkin section 集合代际的修正也没能把命中率拉起来。上一轮报告里"预期从 44% 明显上升"的推断
被实测否定，原因是漏掉了另一个、也是实际起作用的 churn 源。

## 第二轮：真正的 churn 源（全局 buffer resolve 代际）

上一轮埋点 `BufferRangeInvalidations` 给出答案：**5~17 次/帧**（旧场景同样量级）。这个全局
代际是负判定签名的一个 term，任何一次无关 buffer 的创建/销毁都会让上一帧存下的全部判定不可达
——所以跨帧复用是 0，命中率只能是"同帧内重复"的那部分（43~44%），与两个 build 的实际观测一致。

同一份数据还说明 memo 为什么没帮忙：`IaNegVerdictHashes` 60277µs / (1170.6 重建 + 495.2 复用)
≈ **600 ns/次**，即几乎所有重建都还在走 `unordered_map` 容器路径。memo 只在两种情况下能回答：
视图解析到已发布资源（用 identity token），或代际没动。既然代际每帧动 5~17 次，能命中的就只能是
**已解析**视图；实测 600ns/次说明这些 IA 视图绝大多数是**未解析**的（GPUVA 尚未归属到已跟踪资源），
于是每次代际一动，memo 就保守拒绝、回落容器路径。

### 改动

`IaViewHashMemo::TryRead` 对未解析视图不再用"代际相等"当有效性，改用**范围级失效判定**
（`DX12BufferResolveCacheUtils::RangeInvalidatedSinceLocked`，与 L1 对同一情形执行的完全是同一个谓词，
同一把共享锁），通过后回写代际。

这**不改变任何返回值**：memo 命中即 L1 命中。未解析视图的哈希只由视图字段决定，只有在有资源发布到
该范围时才会变，而这正是范围判定回答的问题；无关 buffer churn 移动代际不构成任何证据。memo 拒绝时
照旧回落容器路径，所以最坏情况不比改动前差。

### 离线证据（同一脚本，3 轮中位数）

| 项目 | 改动前 | 改动后 |
|---|---:|---:|
| 视图哈希解析（未解析视图 + 代际 churn，64 次请求动一次代际） | 无此用例 | **15.2 ns/call** |
| 视图哈希解析（最坏：每次请求都动代际） | 无此用例 | 234.3 ns/call |
| 视图哈希解析（槽位 memo 被打掉，512 视图） | 209.1 ns | 21.9 ns |
| 视图哈希解析（8192 视图，超出 memo 容量） | — | 586.1 ns/call（memo 背后的容器路径） |

容器路径同一形状约 600 ns/次，所以现实分布下每次请求从 ~600 ns 降到 ~16 ns。新增用例
`PASS unresolved view revalidation` 覆盖两种方向：无关范围失效必须复用、覆盖该视图的范围失效必须拒绝。

### 判据

第三份日志里应看到：

- `Stage.IaNegVerdictHashes` 从 ~1000 µs/帧 掉到 ~50~100 µs/帧；
- 新计数器 `Rm.IaViewHashMemoHit`（日志 `counter":"IaViewHashMemoHit"`）接近 `IaViewHashSlotRebuild` 的次数；
- **若 `IaViewHashMemoHit` 接近 0**，说明这些请求的 memo 键本身每帧都不同（视图值真的在变），
  那是另一条根因，本轮改动不会有效果——这一点是这个计数器专门用来区分的。

### 部署（第二轮）

```text
D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll   4639232 bytes  2026-09-18 08:09:27
SHA256 3B8EA7BC2461C7B8B8183F778474F514B47B70827DCACE1A7F365CACA7D7DC8F
```

上一份日志轮转为 `d3d12_log.before_20260918-080928.txt`。

## 未验证与风险

- **PreSkin section 集合代际是纯 CPU 侧判断，没有离线行为测试**（`DX12PreSkinRuntime.cpp`
  的生产函数依赖 fence/资源/描述符环，无法在无设备环境抽取）。现有保障是：判定读取面的等价推理、
  三个变更点的完整枚举（`gActivePreSkinTextureOverrides` / `gConfirmedPreSkinSections` 全仓库只有
  clear / insert / `[section] =` 三种写入形式）、以及脚本里的源级不变量检查。
  若这里判断错了，症状是**Mod 不生效**（不是崩溃），所以游戏内必须确认角色贴图替换仍然可见。
- **DLL 内效果只有游戏内日志能证明**。离线微基准只说明被调用函数的成本下降，不等于帧时间下降；
  第一轮就是这个教训：微基准收益真实（209→22 ns），但现场那一层根本没走到它，所以帧内数据没动。
- 未解析视图的范围级重校验与 L1 用的是同一个谓词，因此**继承了它既有的一处保守性缺口**：
  `InvalidateResourceRangeChangeLocked` 在一个 epoch 内只记录一个范围（旧范围+新范围同时存在时，
  后写入的新范围覆盖旧范围），所以"资源换了 VA"这种情形下旧范围可能不在历史里。这处缺口在改动前
  就存在于 L1 对未解析视图的分支上，本次没有扩大它；本轮也没有修它（改动会动到被多处共用的
  失效历史结构，需要单独评估）。

## 游戏内复测方法

1. 完全退出旧进程后重新启动（DLL 已部署）。保持相同分辨率、画质、DLSS/帧生成、角色、Mod、场景。
2. 对比 Overview 的同一批行：`Api.DrawIndexedInstanced`、`Stage.DrawHook`、`Stage.DrawOverride`、
   `Stage.DrawOvIaStateNegCache`、`Stage.IaNegVerdictHashes`。
3. 确认 Mod 仍生效（角色贴图/换装可见）、场景切换、Hunting/dump、Mod 热重载都正常。
4. 保留新的 `d3d12_log.txt`，读两个日志计数器：
   - `counter":"IaViewHashMemoHit"`——判断 memo 是否真的接住了这些请求；
   - `counter":"BufferRangeInvalidations"`——每窗口的 buffer 范围失效次数。

`IaNegCacheHit` 占比这一项**不再是本轮的判据**：判定签名的代际 term 仍在，命中率预期仍停在 44%；
本轮要降的是每次判定的成本。若 `IaViewHashMemoHit` 接近 `IaViewHashSlotRebuild`，说明成本已降下来，
下一步才是把代际从签名里拿掉、让命中率真正上去（那时才有收益，现在做没有）。

没有游戏内后测日志前，不声称帧率或帧时间已经改善到某个数值。

## 第三轮：稳定 hash 快照负判定缓存

第二轮日志已经证明 `IaViewHashMemo` 能降低哈希解析成本，但 `IaNegCacheHit` 仍被全局
`resolveGeneration` 限制在约 44%。本轮没有直接删除失效保护，而是把负判定的真实依赖改成
**已经经过权威哈希路径验证的 IB/VB hash 快照**：

- 快照只收集预过滤表实际会探测的视图；同一 command-list IA 状态下优先复用已有槽位 hash，
  只有槽位 hash 无效或 resolve epoch 变化时才进入 `DX12HashIaBufferContentView`；
- negative cache 的 exact key 仍保留原始 view 字段，hash 只作为资源身份/解析结果的稳定依赖，
  因此不靠 hash 碰撞证明两个 GPUVA 相同；
- 移除全局 resolve epoch 对该 negative verdict 的直接淘汰，允许无关 buffer churn 跨帧复用；
- 修复 unresolved view 的两个失效缺口：新资源发布时即使没有旧 resolve-cache entry 也记录 epoch，
  一个 epoch 同时记录旧范围和新范围，避免后一次写入覆盖前一次失效范围。

### 第三轮验证

- `dev_tests/ia_match_perf_test.ps1`：全部通过；新增生产 hash snapshot 的 target gating、无关 churn、
  覆盖范围刷新用例；
- `git diff --check`：通过；
- TIMING DLL 构建完成，随后以 MSVC 环境完成剩余链接；
- 为避免把每个负缓存 lookup 的 QPC 计时本身带入热路径，hash snapshot 周围的每绘制
  `DX12_PROF_STAGE_SCOPE` 已移除，实际槽位重建仍由 `IaViewHashSlotRebuild` 统计；
- 当前 DLL 已部署到 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll`，SHA256：
  `BFC89F7CDB362815DB8D99F4C54055EFC322551666B5CFA4A2A127EF5354640A`；
- 部署前 DLL 备份在 `backups\before-scope-gate-20260918-091621\d3d12.dll`。

当前目标日志 `d3d12_log.txt` 最后写入时间仍是 08:13:29，而新 DLL 部署于 09:16 左右，
因此这份日志明确不是新 DLL 的运行结果。实际游戏窗口尚未由本轮自动启动，
`DrawIndexedInstanced` 的最终 ms/FPS 变化仍需完全退出旧进程、重启同一场景后用新的日志确认。

## 运行截图观察（用户提供，build 09:13:47）

这张 Overview 截图已经给出稳定 hash snapshot 的直接运行证据：

- `IaNegCacheHit = 1046/f`，命中率 **98.6%**；
- `IaNegVerdictRuns = 15.12/f`，且 `IaNegVerdictNegative` 约等于全部 runs；
- 说明 negative cache 的跨 churn 复用已经生效，相比此前约 43%~54% 的命中水平有明显改善。

同一截图中的总量为 `Stage.DrawOvIaStateNegCache = 1.31 ms`、
`Stage.DrawHook = 3.11 ms`、`Api.DrawIndexedInstanced = 3.06 ms`，frameWall mean 为 `27.69 ms`。
这些数值与此前窗口的 DrawHook/帧墙不能直接作严格 A/B：截图的每帧 draw 数、场景状态和统计窗口不同，
所以目前可以确认的是**负缓存层收益**，还不能据此声称整体 DrawIndexedInstanced/FPS 已下降。
另外截图 build 时间为 09:13:47，早于之后部署的去每绘制 QPC scope 版本，需再跑一次最新 DLL 才能验证最终热路径数字。
