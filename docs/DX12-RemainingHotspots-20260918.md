# DX12 剩余热点优化：最新运行日志后的迭代

## 任务边界与证据

用户要求检查新运行日志并继续优化，本轮不再排除 CopyDescriptor。目标是降低有证据支持的 CPU 冗余，同时保留所有绘制、VM执行顺序、缓存失效、descriptor发布与资源生命周期语义。有限场景/硬件上的测量无法证明“必定全局最优”，本报告不会用这个保证代替结果。

开始时工作区干净，基线提交 `357ba2b`（包含上轮已提交优化）。最新日志原路径 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt`，39567868 bytes、330610行，最后更新时间2026-09-18 09:50:34；只读取采样片段并定向搜索。

截图对应 `present=8049`、63帧窗口（日志320252行起），另检查7925/7986/8109/8169窗口，避免只挑一个好样本。8049帧均24.953ms，7986约24.911ms，8109约27.888ms，8169约26.998ms；负载及系统波动存在，不能直接把与上轮FPS差归因于代码。

| 8049窗口热点 | 总µs / 调用 | 归一化 | 处理方向 |
|---|---:|---:|---|
| CopyDescriptorsWrapper.Record | 269967 / 191341 | 4.285ms/frame，1.411µs/call | 合并重复chunk/slot storage解析，完整保留缺页回退 |
| CopyDescriptorsWrapper.Original | 10877 / 191341 | 0.173ms/frame | 原始调用保留 |
| DrawModPrepareOverride | 41794 / 248 | 0.663ms/frame，168.52µs/call | SRV无工作时不做全量查询；缩小IA资源快照；VM字面量比较 |
| DrawOvIaStateNegCache | 63971 / 61369 | 1.015ms/frame，1.042µs/call | 对已验证hash先做编译索引缺席证明，再用原精确缓存处理余项 |
| SetPipelineState | 60223 / 35185 | 0.956ms/frame，1.712µs/call | 已有上一轮去重，本轮不跳原生操作 |
| RootSignature | 44934 / 19155 | 0.713ms/frame，2.346µs/call | 保留root失效/驱动语义，未新增裸指针生命周期缓存 |

IA旧缓存58018 hits、3351 runs，约94.5%；hash memo 80048 hits / 88979 slot rebuilds，仍有真实重验证。没有以扩大容量/弱化失效来追命中率。日志watchdog的几个stall记录中采样的锁均free、deviceRemovedReason=0；这不能证明没有游戏/GPU等待，也不构成删除等待的依据。

## 1. IA：先查询编译索引，再支付resident缓存成本

`DX12TextureOverrideMatching.cpp` 将既有hash-index查找提取为单一共享函数，新增 `DX12ModIaSnapshotDefinitelyAbsent`。它只回答：刚经过资源身份/范围验证的hash snapshot，在reload构建的完整count-independent索引里，是否一个候选都没有？

- 不添加缓存、全局可变状态、锁、资源指针或额外失效代际。
- 仍先执行原 `DX12ModBuildIaStateNegativeHashSnapshot`，GPUVA重用/资源身份/未解析范围验证完全保留。
- IB、指定VB槽和any-slot fallback与原谓词共用同一查找函数；merged bucket存在时不会错误合并any-slot。
- count-sensitive时失败开放；hash存在候选时继续原有section-sensitive精确缓存及完整匹配，不把hash存在当成必然匹配。
- 缺席时跳过raw-view签名、精确key堆指针、缓存查找及写入，仍保存bound-negative结果并结束同一绘制判定。
- 原缓存保留给有hash候选、但需match_cs等语义判定的路径，而不是盲目删除所有缓存。

新增 `Rm.IaDictionaryReject`，HUD单独显示“编译索引直接拒绝 / fallback缓存命中”。不能把两者合成更高的缓存命中率。原Stage.DrawOvIaStateNegCache名称保留以便对照，它现在包含新增的直接拒绝路径。

### IA离线证据

真实生产函数体抽取测试通过，原26880匹配差分、资源发布/范围失效/碰撞/bit31/1000次reset保持等回归仍通过；新增检查确保dictionary拒绝一定蕴含原完整谓词对IB及VB都为negative，count-sensitive必须失败开放。

MSVC C++20/O2、3轮中位数（资源解析为test double，不是游戏帧率）：

| 对比 | 基线/旧路径 | 新路径 |
|---|---:|---:|
| 相同snapshot：原signature+Find vs dictionary | 40.3ns | 10.2ns |
| 512个轮转view、完整snapshot+拒绝路径 | 235.2ns | 150.9ns |

8K视图超memo工作集仍约601ns/call，这是保留的负例。QPC单次读取实测约13ns，不能直接声称1µs整条路径都来自计时器。

HUD新测试直接包含生产格式化头，检查新counter接线、dictionary比例分母包含所有probe、fallback命中比例仅包含fallback查询，以及三个页面输出边界。旧本地 `dev_tests/profiling_hud_test.cpp` 无法编译，引用的是已经不存在的Compact/Summary/Top/Sections接口；未伪造这些生产接口让旧测试通过，新增与当前API对应的独立测试。

## 2. CopyDescriptors

**候选优化已撤回，生产Copy路径本轮不变。** 实际窗口1,570,467slots / 1,565,998blocks，几乎全是单槽块；每API约8.18块，80.21% unchanged、100% direct path，ResolveMiss仅220。重复chunk存在性/slot解析确实存在，但“少几次查询”不自动代表优化后的机器码更快。

候选将chunk存在性检查并入slot access，正确性回归通过。然而生产使用 `/O2`（另有 `/GL`），不是 `/Od`；最终三轮80%unchanged混合负载中位数：同源1heap 59.155→60.873ms/100k（退化2.9%），40heap同源83.270→94.116ms（退化13%，区间有重叠），scatter8为69.503→67.214ms（改善3.3%），scatter40为121.703→119.683ms（改善1.7%）。此前单range40 mixed还有11.1%退化负例。日志没有scatter分布，不能只给有利用例加权；`/Od`的收益没有生产权重。因此完整撤回候选生产分支及专用参数，不声称4.285ms桶有改善。

保留 `descriptor_copy_perf_test.cpp` 的回归增强：公开生产路径的前后向overlap/self版本、缺chunk与已分配空槽的ledger区别、目标首次分配跨chunk，以及64连续槽dense/chunked基准。最终 `/O2` 测试通过，生产 `DX12DescriptorSlotArray.h`、`DX12ResourceTrackerHooks.cpp` 和提取脚本均确认与基线一致。

测试提取真实resolver/cache/slot/copy代码，但policy/ledger/fallback含测试适配器；不证明驱动/COM或合法并发heap retirement的实机行为。Overlap测试只守住已有shadow方向语义，不把native D3D overlap说成合法。

## 3. Draw准备与VM

### 减少不必要的数据准备

`RunTextureOverride` 原本无条件调用 `GetGraphicsSrvBindings()`，随后ApplyConfig/ApplyCompiledPlan在没有任何SRV资源替换时马上返回false。现在末尾helper先按被调函数同样的空资源条件判断，只有确实有PS/VS/this资源替换才采集。VM/IA动作和post入队仍全部先执行，不能把整个texture override跳掉。

`DX12IaResourceViewSnapshot` 原来内嵌整个 `DX12LoadedResource`，复制名称、路径和两个std::map，但消费者只需要选中资源的width/stride/format。改为32B标量快照（原296B），仍在原锁内选择资源并解析UAV/SRV/原资源alias的宽度，保留UINT饱和、format/stride回退、AddRef/Release和barrier语义；没有移动或放松资源所有权。

### VM词法比较

`MigotoCommandVmUtils.hpp` 的ExpressionParser::Match每个运算符比较原先都构造一个临时wstring。所有调用都是字符串字面量，现用模板长度直接比较。SkipSpaces的位置、失败时pos、解析错误、变量副作用和逻辑运算符RHS求值均保持，不改表达式语法。

### 生产函数体差分验证

`dev_tests/executor_prepare_test.ps1` 固定对提交357ba2b提取基线：512种资源选择/UAV epoch/alias宽度/生命周期/缺失与重载用例、真实Append函数、SRV空/非空/this条件，以及真实新旧VM头的1050个正常/畸形表达式差分；覆盖post/local/branch/live globals及逻辑RHS副作用。测试通过。两个C4101来自禁用计时器的测试桩，非生产编译错误。

局部O2微基准三轮中位数：空map资源元数据复制169.1→2.2ns，16项map为1556.7→2.1ns；三种表达式分别250.1→121.8ns、1611.4→869.2ns、1940.1→1017.6ns。元数据基准排除了锁/COM/GPU；这些数据只能说明所改局部，不可换算成168µs整体准备成本或FPS。没有正向执行缓存，不跳过VM变量赋值、draw/dispatch动作或动态资源更新。

## 构建、部署与最终验证

- `build_release_timing_x64.ps1` 全量38/38构建成功，exit 0；已部署至既有ZZMIDX12目录，没有启动游戏（LaunchGame=false）。备用ssmt4目录不存在，脚本按既有规则跳过。
- 最终串行回归全部exit 0：`ia_match_perf_test.ps1`、`ia_dictionary_hud_test.ps1`、`executor_prepare_test.ps1`、`descriptor_copy_perf_test.ps1`、`noncopy_hotpath_test.ps1`、`preskin_dispatch_test.ps1`、`wrapper_hotpath_test.ps1 -AddressSanitizer`，以及 `git diff --check`。
- 汇总原始输出：`build/remaining_validation.txt`。末次IA完整轮转路径244.8→166.7ns，再次确认方向；相同snapshot 41.6→9.8ns，有hash候选时新增fallback检查约4.8ns。报告上文保留此前完整三轮结果，不只挑最快一轮。
- 本地产物 `x64/Release/d3d12.dll` 与部署 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll` 均4657152 bytes，时间2026-09-18 10:16:10，SHA256一致：`116E7A6C8C20A20A18C06DA87872B821ADFE14713766F33BAFF1C1B2620D02BF`。
- 本轮基线日志保留为 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.before_20260918-101610.txt`。部署后尚无新 `d3d12_log.txt`，因此没有实机加载/帧率收益的验证结论。

### 回退

部署前已复制旧文件：

- `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll.before_remaining_20260918-100843.bak`，SHA256 `53FB044D73179DC40EAD94A9428343F688F793B7BA3A25BA0597C73C2E66FDEF`。
- `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\Run.exe.before_remaining_20260918-100843.bak`，SHA256 `0C764264AFE5F1990E2DBF9EAA32A7E53A1542AABB57DBFFCF7BE5BF8EF106FE`。

需要回退时先退出游戏，再将对应备份复制覆盖同目录的d3d12.dll/Run.exe。源代码未自动提交。

### 下一次实机验证

直接使用既有启动方式运行相同场景，确认mod视觉结果、动态切换和PreSkin正常，再对照相近draw/dispatch/API调用量的多个60帧窗口。重点看DrawModPrepareOverride、DrawOvIaStateNegCache总成本与新IaDictionaryReject计数；旧缓存命中率现在仅代表fallback部分，不宜与原94.5%直接比较。CopyDescriptor本轮生产代码未变，不预期靠本轮候选获得其独立桶收益。

交付状态：**实现、离线回归、TIMING构建和部署完成；目标游戏运行验证待进行。**
