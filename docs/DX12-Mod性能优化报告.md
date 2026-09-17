# DX12 Mod 生效路径性能优化

## 基线与目标

本次依据用户的 Overview 截图（build 2026-09-17 19:14:32，约 30 FPS）和构建前的 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt`，共约 463648 行、55 MB。只读取日志样本与指定统计窗口，不把整份诊断日志载入对话。

截图主要热点：

- `Create.CopyDescriptorsWrapper.Record`：约 5.42 ms/frame，3769 calls/frame。
- `Stage.DrawOvIaStateNegCache`：约 2.81 ms/frame。
- `Stage.IaNegVerdictHashes`：约 2.41 ms/frame；约 715 次 IA 负判断/frame，99.8% 为 negative。
- PreSkin 常量补丁包含按次创建小型 committed upload buffer 的成本。

日志 `present=11271` 窗口佐证：

- 描述符代理记录：246209 calls / 335473 us；驱动原始复制：246205 calls / 10890 us。
- `CopySlots`：1758639，基本为单槽块；约 77.3% 的 descriptor copy 值不变。

父子 profiling scope 有嵌套，不能把上面的时间相加；TIMING 本身有计数和日志成本，跨线程累计 CPU 耗时也不等于单帧关键路径。下文微基准不换算成游戏 FPS。

## 设计原则

- 不通过关闭 Mod、跳过候选判断、缩小必要追踪范围或删除失效校验来提高帧率。
- 保留资源销毁/地址复用、配置重载、PreSkin 活动条件、Hunting/dump 的原有语义。
- 对缓存命中补全正确性校验，而不是依赖摘要碰撞概率。
- 上传快照不按帧数覆盖、不使用缺乏 fence 保障的循环复用。
- 前次 DLSS 交换链生命周期修复保持不变。

## 优化内容

### 1. 描述符复制：减少逐槽重复管理

- 同一次槽访问复用记录、occupancy 指针及占用状态，避免重复解析 chunk/slot。
- 已占用槽不重复写 occupancy；新增/删除槽仍准确维护 live-slot 计数。
- `CopyBlocks`、`CopyBlocksSkipped`、`CopyBlocksLocked` 改为每 API 调用累加后发布；保留全部逻辑数量，不再每个单槽块查找 profiler bucket。
- 全局 heap 查找缓存以前用非原子多字段无锁替换，不构成安全的“x64 benign race”。改为每 set 的 SRW try-lock：竞争退回既有查找路径或跳过缓存插入，不等待，也不接受混合的 heap/range 快照。
- 不启用缺少验证的新描述符跳过缓存，不改变值身份/历史记录/版本发布规则。

### 2. IA 匹配：复用权威缓存和已有索引

- VB view hash 的槽缓存未命中后，直接走已有 `DX12HashIaBufferContentView` 的 TLS L1/L2，不再先锁 wrapper shard 扫描覆盖资源，再回写另一个重复 hash 缓存。
- 保留权威 hash 路径的资源 token、GPUVA 范围失效与未解析范围处理。
- Count-independent 的负判断按 hash/slot 索引获取候选，不再对每个 view 扫描全部预过滤候选。原加载器已构建这些索引，但没有发布；现补齐 3 个运行时索引，与候选表在同一个 `gModLock` 临界区、同一重载代际一起 swap，空配置也会清除旧索引。wildcard slot、specific slot、`match_cs` 和最终逐 draw 判断不变；count-sensitive 情况保守退回可能命中。
- 原负缓存的摘要混合使用可交换 XOR，字段/槽交换能形成确定性相同摘要。改为顺序相关摘要，并在摘要命中后精确比较有效目标计划、代际及所需 IA view；摘要仅负责定位，不能决定 Mod 不生效。
- 精确键按 cache way 延迟堆分配并复用容量，不在每个 draw 构造大数组、不扩充缓存 entry 数量。
- IA hash 的实际生产语义是 metadata-only，`tracksContent=false`。不额外加入全局内容写入 epoch 导致无关资源写入踢掉全部 IA 负缓存；已有其他层的内容校验不删除。

### 3. PreSkin CBV：不可变上传页

新增 `DX12ImmutableUploadAllocator.h`：

- 以 64 KiB UPLOAD 页按 256-byte 边界只追加分配，每份常量快照使用不同 GPU VA 区间。
- 一页满后创建新页；从不覆写旧页，也不以“已经过了几帧”推定安全。
- 每份返回资源仍持有独立 COM 引用，沿原有 `temporaryResources`、CBV cache 和退休路径保活。缓存清理仅释放 allocator 自己的页引用；已经录制/提交的快照保持有效。
- cache hit 仍要求原常量快照与 patched bytes 的完整匹配，保留原始顶点数识别和写入逻辑。
- device 切换时清理旧 device 的 patch cache，不跨设备复用 CPU 描述符。

不更改游戏资源、Mod 配置、hash 算法的输出或 DLSS 设置。

## 验证记录

以下测试均是离线回归，不替代游戏内前后对照。

### 不可变上传页

命令：`dev_tests/test_immutable_upload.ps1`

使用生产 allocator 头文件、真实 D3D12 WARP 设备和命令队列，验证：

- 520 份 256-byte 快照仅分配 3 个资源，地址对齐且互不覆盖。
- allocator/cache reset 后旧资源仍可使用，新分配不复用旧页。
- 把全部快照复制到 readback buffer 后逐字节核对，确认之前的常量未被后续分配改写。
- 页满切换、超页大小、非对齐大小、空请求与重复 reset。

初次 WARP allocation CPU 微基准：逐资源创建 20365 us / 520 resources；分页 553 us / 3 resources。

显式以 `--hardware` 运行测试可执行程序，实际枚举到 **NVIDIA GeForce RTX 5070**，同样全部通过，包括 520 份快照的命令队列 readback 逐字节验证。该次 allocation CPU 微基准为：逐资源创建 **93818 us / 520 resources**；分页 **1133 us / 3 resources**。

这些结果只说明这段分配工作消除了大量 driver resource-create 调用，不代表游戏整体加速比例。测试脚本的可选硬件参数转发已改为显式传参，避免把缺少参数时执行的 WARP 测试误算成硬件结果。

可选 `-Hardware` 模式严格排除 software adapter；若当前进程看不到物理显卡则显式 SKIP，不把 Microsoft Basic Render Driver 当作 RTX 测试结果。

### 描述符

命令：`dev_tests/descriptor_copy_perf_test.ps1`，以及 `-Baseline`。

脚本抽取生产 slot array、heap cache、copy hook 代码编译；registry/ledger/policy/profiler 的外部环境使用测试适配器。

验证内容：dense/chunked 记录复制、值不变、空源清除、缺失 chunk 的锁路径、range 拼接与零长度、版本/计数一致、Hunting/dump 分支；8 线程独立槽 copy/erase 的 live-slot 计数；8 线程同一全局 cache set 的替换压力 32 万次和退休 heap 拒绝。

`/O2` 下修复前后回归均通过；旧版已知数据竞争的并发 heap-cache 测试不执行，避免故意运行 UB。

- 10 万次“8 个非连续单槽 unchanged copy”适配器微基准：baseline 55.485 ms / 110 万次 profiler bucket 查找；current 34.145 ms / 40 万次。确定性收益是查找次数减少；时间只是此微基准结果。
- 100 万次真实 first-way heap-cache hit：旧版 2.233 ms，新版 13.530 ms，单线程理想命中下同步约增加 11.3 ns/hit。这是修复数据竞争的明确代价，没有隐瞒为零开销。
- 样本中 global heap-cache 命中约占 src/dst resolve 的 4.4%；全局 hit 以外仍走既有 TLS 等路径，不给每次 descriptor copy 都强加全局锁。
- 100 万次 changed slot + PublishRangeValue：chunked 15.569→15.300 ms，dense 13.296→12.955 ms，差异较小，不声称稳定收益。

### IA

命令：`dev_tests/ia_match_perf_test.ps1`。

抽取生产 plan/cache/index-builder/prefilter/match_cs/hash/slot 函数编译，底层 GPUVA 资源发布和 hash primitive 使用明确的 test doubles。

首次完整构建发现测试手写的 global indexes 掩盖了真实加载器缺失发布链的问题。已补全生产声明和发布链；测试同步改为抽取真实 global 声明、Load 局部构建调用和 swap 代码，不再直接把 builder 输出放进模拟 globals，并补重载替换、空配置清理验证。失败的构建没有部署产物。

- MSVC `/O2 /W4`：全部通过，无警告。
- 26880 个差分用例与旧 generic 扫描一致，覆盖 specific/wildcard slot、hash 0、skip、三个活动 PreSkin 集合。
- 强制摘要碰撞、slot 交换、size/stride 交换、计划与代际变化、解绑、IB 格式变化、复制和存储复用。
- 10000 次重绑仅使用暖 TLS，不调用 wrapper hash lookup/store；同 VA 和同 resource 指针复用但 descriptor 改变时失效；未解析→已解析正确失效；内容写入保持 metadata-only hash 语义。
- 真实 Load 发布路径验证：首次发布、重载删旧 hash/增加新 specific slot、candidate config 指针归属新 owner、空配置清空全部 hash 索引。
- 编译真实 `DX12SimulateContextDrawTemplate` 和 IA cache，使用 Reset 的真实 move 语句进行 1000 次移出/移回，cache table 地址及 negative 命中保持不变。
- 20 万次 absent-hash 合成探测：旧 generic 扫描 309713 us，新 hash index 638 us。这里只证明扫描复杂度下降，不把约 485 倍的探测微测比值当成游戏加速比。

### 正确性与代价

- IA 负缓存的 1024-entry 容量不变，cache 本体从 24 KiB 内联表缩为一个堆指针，避免 command-state snapshot 拷贝整个表。精确键按需分配，典型 2–4 bound slots 的完全填充状态约占每 command-list 144–176 KiB，极端全部 32 slots 约 624 KiB。功能校验增加了有界内存成本，首次预热/slot 数增长仍可能分配。
- 去掉跨线程 wrapper hash 缓存可能增加冷 TLS 的资源解析次数；避免热路径锁/扫描的收益须由新 TIMING 日志确认。
- descriptor 同步新增少量开销换取完整 cache-entry 快照，未把数据竞争当成性能优化。应用非法地并发写同一 descriptor slot 的行为不在本次补丁保证范围内。
- 上传页没有槽位复用或 fence 推断；现有 command-list 资源退休规则不变。
- 未改变 prior DLSS swapchain 修复；最终仍需游戏内场景切换、重载和帧生成复测。

### 构建与部署

`build_release_timing_x64.ps1` 在补齐发布链后完整 clean build 成功（38/38，退出码 0）。已部署：

```text
D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll
```

DLL 大小 4635136 bytes，部署时间 2026-09-17 19:47:20；部署文件与 `x64/Release/d3d12.dll` 的 SHA256 一致：

```text
632C525DB4A45616E29555FFDED4B79B1D9A0B5D405F7E91A7B98AEF1D2D1E96
```

基线日志完整轮转保留为：

```text
D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.before_20260917-194721.txt
```

默认构建配置中的另一复制目录 `D:\Dev\ssmt4\src-tauri\resources\DX12\` 不存在而跳过；用户指定的 ZZMIDX12 目标部署成功。未自动启动游戏。`git diff --check` 通过。

## 最终整合回归

完整构建后重新编译运行全部测试，最终输出 `FINAL REGRESSION SUITE: ALL PASS`，退出码 0：

- 当前/HEAD 基线描述符回归通过；当前版并发 cache 竞争通过。
- IA 真实声明/发布/重载/空配置、Reset move、精确键、26880 差分通过。
- 不可变上传页 WARP 和 **RTX 5070** 两种设备均通过。
- 上次 DLSS 修复的 swapchain 五项回归（invalidate、no-output、switch、同 HWND recreate、resize）全部通过。

最后一次微基准（CPU 时间，不是游戏帧率）：

| 工作 | 旧路径 | 新路径 |
|---|---:|---:|
| 10 万 API × 8 个 unchanged copy block | 57.856 ms / 110 万次 bucket 查找 | 36.834 ms / 40 万次查找 |
| 100 万 global heap-cache hit | 2.349 ms | 14.317 ms，包含新增同步 |
| 100 万 changed chunked slot + 发布 | 13.633 ms | 15.623 ms |
| 100 万 changed dense slot + 发布 | 13.464 ms | 14.321 ms |
| 20 万 absent-hash probe | 357.447 ms | 0.983 ms |
| RTX 5070：520 次 CBV 上传快照分配 | 95.765 ms / 520 资源 | 1.035 ms / 3 资源 |

changed-slot 单测前后多轮存在波动，最后一轮新路径略慢；因此不把槽访问整理单独宣称为稳定提速。最明确的收益是逐块 profiler 查询减少、匹配由全扫描改为索引、以及大量小资源创建被分页分配取代。整体游戏关键路径还需新日志验证。描述符测试的 C4324 是既有 cache-line 对齐 padding 警告，其他新增测试无编译警告。

## 游戏内复测方法

1. 完全退出旧进程并重新启动，以加载新的 DLL。
2. 保持相同分辨率、画质、DLSS/帧生成、角色、Mod、场景和镜头；等待加载及 shader warmup 完成。
3. 对比 Overview 的 `CopyDescriptorsWrapper.Record`、`IaNegVerdictHashes`、`DrawOverride`、PreSkin patch 创建阶段及稳定帧时间，不只看某一瞬间 FPS。
4. 同时验证角色 Mod 的显隐/切换、场景切换、Hunting/dump、Mod 重载和开启帧生成的路径。
5. 保留新 `d3d12_log.txt`；优化前日志由构建脚本自动轮转保留。

没有游戏内后测日志前，不声称已经从 30 FPS 提升到某个数值，也不把微基准当成整机帧率测试。
