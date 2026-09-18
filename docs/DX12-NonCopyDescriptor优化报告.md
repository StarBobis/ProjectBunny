# DX12 非 CopyDescriptor 热点优化

## 范围与基线

本次不修改 CopyDescriptor/CopyDescriptors 实现，不关闭 Mod、PreSkin、资源跟踪或 TIMING 日志。保留开始时已有的 IA 缓存、范围失效和资源发布改动；这些已有改动不计作本次新成果。

依据截图 build `2026-09-18 09:13:47` 和 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt` 的 `present=27176`、60 帧窗口（原日志行 401996–402240）。日志以分段读取及定向搜索检查，没有整文件载入。此窗口 frameWall=27.69 ms/frame、max=33.48 ms；统计是 TIMING 版且有嵌套，不可把下面各行相加，也不能把微基准换算为 FPS。

| 热点 | 原窗口每帧 | 本次处理/覆盖 |
|---|---:|---|
| DrawHook / DrawOverride | 3.109 / 2.610 ms | IA 负缓存命中路径压缩键；保留精确校验 |
| DrawOvIaStateNegCache | 1.315 ms | 有效槽位掩码 + 稀疏遍历；不是继续扩大缓存 |
| DrawModPrepareOverride | 0.749 ms | Executor pooled 路径不再构造备用节点容器 |
| DrawApplyAndExecute | 0.200 ms | 审查；动态命令/资源/绘制照常执行，不缓存正向执行副作用 |
| SetPipelineState | 1.195 ms（API 约1.24） | 去掉紧接完整编译前的重复 decision Reset；没有新增裸指针缓存 |
| ResourceBarrier / BarrierUnwrap | 1.164 / 0.376 ms | wrapped 后缀只 unwrap 一次；修复 fallback 数组悬垂 |
| RootSignature | 0.744 ms | 审查；不跳过驱动调用或必要 root invalidation |
| ResetCmdList | 0.537 ms | targeted reset 原地保留缓存，删除无意义的大成员 move-out/move-back |
| IASetVertexBuf | 0.464 ms | 去掉两份32槽输出数组预清零，修复双重redirect输出悬垂 |
| DispatchHook / PreSkinBindQuery | 0.735 / 0.349 ms | PreSkin 重复检查及快照准备优化，详见最终记录 |
| PreSkinMatch / CopyPatch | 0.087 / 0.035 ms | 审查匹配；不放宽 descriptor/value/资源失效规则 |
| CopyBufferRegion | 约0.595 ms | 审查，不能删除内容/读回跟踪或原始 GPU copy |
| ExecuteCommandLists | 约0.52 ms | 原窗口 queueOriginal=0.429 ms/frame；不把驱动提交删掉或改顺序 |
| Present / BeginFrame | 约0.97 / 0.303 ms | 审查退休、输入、预加载、PreSkin清理；不降低更新频率 |
| Readback.submit | 约0.378 ms | 失败事件由逐项erase改线性稳定压缩；修复发布后读取batch的竞态 |
| CloseCommandList / Fence | 约0.173 / 0.158 ms | 原始驱动操作、同步语义保留 |
| DescriptorCbvWrapper.Record | 约0.766 ms | 已核查专用valueId、sequential/TLS slot定位、unchanged早退及preResolved传递；约47.6ns/次，没有再次增加共享descriptor缓存或省略发布 |

IA negative cache：62816 hits、907 verdict runs，即约98.6%命中。旧的“命中率低”不能解释本轮剩余耗时。PreSkin geometry-negative 有2918次，collect_bindings=20772 µs/window；不能把几何字段CRC说成GPU内容哈希/读回。

## 已实现的变更及不变量

### IA 命中路径

`DX12IaHashCaches.h`：以前 all-IA 计划为32槽逐个混合槽号/是否绑定/哈希有效，即使绝大多数槽为空。现在用绑定mask和有效hash mask表示缺席，只遍历实际绑定或有hash的槽。

- mask每次由权威 `hasVertexBuffer` 派生，不维护第二套容易漏更新的绑定状态。
- 保留 reload、PreSkin section代际、索引view、每个有效hash、required mask和逐字段vertex view精确校验。
- 碰撞仍只能丢命中，不能误命中；槽31、交换、解绑、重载、GPUVA重用等受测试覆盖。
- 没有删除哈希前的资源身份/范围重验证。

### Wrapper、PSO 与 Reset

- Barrier native-only仍直接使用输入数组；遇到第一个wrapped资源后，后缀直接转换，不再先探测再转换两遍。
- `runtimeState==nullptr` 的本地barrier vector存活至capture和native call结束。
- static+PreSkin双重IA redirect使用外层输出数组，不再把内层临时数组指针传出。
- redirect helper成功才读输出，成功会写全部count条；故可删除预清零。
- PSO编译命中为完整赋值，miss的setter自行Reset，禁用consumer仍显式Reset。
- Reset只清当前recording有效性、保持已有content-keyed缓存；移除移动并原样移回这些缓存的额外工作。

### PreSkin / Dispatch

- `DX12DispatchHookFlow.h`：执行快判定及拒绝原因共用一次短路检查，不再把同一串条件计算两遍。前置binding validator与原诊断归因保留，不通过改变计数口径伪造命中率。
- `CollectPreSkinBindings` 增加默认true的resident尝试参数；当前dispatch已做identity probe时才跳过重复resident尝试。fresh snapshot和另一层content cache的键/descriptor校验不跳过，其余调用默认行为不变。
- compute snapshot改为线程局部复用。两条填充路径成功时覆盖全部11字段及3数组，失败则不消费旧快照。代价为每个使用线程保留 **29040 bytes**（约28.4 KiB）；collector沿用原有TLS scratch的非重入契约，Dispatch入口原有重入guard保持。
- MatchCs旧bool-cache的键构造已遍历并hash所有目标SRV，miss再匹配一遍、positive构造replacement时又匹配。改为对同一输入直接短路匹配一次，再由已验证config生成replacement，去掉徒劳键/节点容器开销。没有缓存正向执行，也没有放宽匹配条件。
- 该窗口identity/slot失败后第二resident尝试本来经常很便宜；因此**不宣称这些改动已解决整个0.349ms/frame绑定采集**。geometry negative日志完全保留。

### Executor 与读回

- `DX12CommandListExecutor.h`：备用scratch改为 `unique_ptr`，只有超过18层池容量才构造。常规路径不再构造并销毁5个未使用的unordered容器；overflow仍独立拥有，嵌套不共享数据。
- `DX12AsyncReadback.cpp`：失败record已经Release并置空，稳定线性压缩只移除空事件，不改顺序、ring offset、资源释放和GPU队列同步。
- 读回batch发布后由worker所有；计数在锁内复制到局部，后续诊断不再解引用可能已被worker删除的batch。

## 离线验证（构建前已实际执行）

1. `dev_tests/ia_match_perf_test.ps1`：PASS，26880匹配差分用例，新增256计划×32槽的绑定/view/hash精确键验证，1000次真实InvalidatePlan保留缓存，GPUVA重用/范围变化/重载等原回归仍通过。
2. `dev_tests/noncopy_hotpath_test.ps1`：PASS，抽取生产scratch/Acquire及readback压缩函数，覆盖18层池+2层overflow、重复使用、0/1/64/4096事件、全成/全败/混合及稳定顺序；源级断言保证生产调用和发布后无batch解引用。
3. `dev_tests/wrapper_hotpath_test.ps1`：父流程重新运行PASS，barrier 0/1/8/64/65/1024、native/wrapped/null/alias两端/unknown、resident/fallback/direct/callback；IA static/PreSkin/双重/miss/0/32/33；PSO全部功能mask、changed/repeated/null与禁用失效。专项AddressSanitizer运行亦PASS。
4. `dev_tests/preskin_dispatch_test.ps1`：生产函数体8192种guard组合、Match正常/缺失/类型不符/替换配置、collector prefix跳重复尝试及fresh fallback、两个snapshot填充函数全部字段覆盖；生产布局独立抽取测得29040B/thread。专项通过，完整构建后再集成复跑。
5. `git diff --check` 通过；新增测试已加入 `.gitignore` 的精确例外，不会因原有 `dev_tests/*` 规则漏交付。

测试中的资源解析/COM/负载payload有test double；真实函数体抽取证明CPU控制流、精确键与所有权，不证明GPU同步、实机画面或游戏帧率。

### IA微基准

同一MSVC C++20 `/O2`、同机、3轮中位数，改动前使用当时生产body生成的头（不是git HEAD，因为HEAD不含用户已有改动）：

| signature + Find | 改前 | 改后 |
|---|---:|---:|
| 单VB计划hit | 92.0 ns | 41.9 ns |
| miss | 47.9 ns | 35.0 ns |
| all-IA稀疏绑定hit | 181.3 ns | 41.9 ns |

改前独立首轮hit=85.9 ns，说明本机有波动。收益仅限该局部路径；不能声称1.315ms全被消除。8K视图超memo工作集仍约596 ns/call，该负例未隐藏。

构建前的一次测试在并行开发中触发原有Reset move源级断言：实现已改为原地保留而测试尚未更新。随后测试改为审核真实Reset完整语句并抽取真实InvalidatePlan，再运行通过；没有恢复无用移动，也没有把测试替换为no-op。

## 最终构建、专项验证与实机边界

`build_release_timing_x64.ps1` 完整构建 **38/38，退出码0**，已部署至游戏测试目录。脚本配置中的另一个目标 `D:\Dev\ssmt4\src-tauri\resources\DX12\` 不存在，脚本按设计跳过；不影响 ZZMIDX12 部署。

```text
输出：D:\Dev\ProjectBunny\x64\Release\d3d12.dll
部署：D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll
大小：4655616 bytes
文件时间：2026-09-18 09:43:28
SHA256：53FB044D73179DC40EAD94A9428343F688F793B7BA3A25BA0597C73C2E66FDEF
```

CMake输出、Release输出、部署文件三者SHA256一致。旧日志由脚本保留为 `d3d12_log.before_20260918-094328.txt`（53342108 bytes）。脚本也部署了loader与DXC依赖，没有自动启动游戏。

回退备份：
- `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll.before_noncopy_20260918-093821.bak`
- `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\Run.exe.before_noncopy_20260918-093821.bak`

旧DLL SHA256：`BFC89F7CDB362815DB8D99F4C54055EFC322551666B5CFA4A2A127EF5354640A`。需要回退时先退出游戏，再用备份覆盖同目录对应文件。

**实现及构建完成，目标场景性能和画面验证待进行。** 检查时游戏进程未运行，也尚无新 `d3d12_log.txt`，因此部署成功不等于新DLL已经加载。建议下一次在相同场景/Mod/视角运行，记录至少多个60帧窗口，再测试切场景、重载、PreSkin角色动作与纹理替换；比较每帧calls和mean/max，不只比较FPS。

仍需实机定位的重点是 PreSkin descriptor patch 偶发813µs尖峰、binding值真实变化造成的失效、DrawModPrepare剩余命令执行和读回/驱动提交成本。RootSignature和CBV已检查但没有证据支持进一步省略现有工作；未通过关闭它们伪装成优化。完整覆盖清单表示已逐项核查，不表示所有热点或卡顿已消除。

### 构建后最终回归复跑

父流程顺序重跑以下脚本，整体退出码0：

- `noncopy_hotpath_test.ps1`：PASS。
- `preskin_dispatch_test.ps1`：PASS（含生产snapshot布局编译）；两个C4189仅来自测试禁用诊断/截断collector冷尾导致的未使用局部变量，完整DLL构建无该测试警告。
- `wrapper_hotpath_test.ps1`：PASS。
- `wrapper_hotpath_test.ps1 -AddressSanitizer`：PASS，未检出被测fallback/IA路径内存错误。
- `ia_match_perf_test.ps1`：ALL PASS。
- `git diff --check`：PASS。

复跑IA hit=40.0ns、miss=35.2ns、all-IA sparse=41.9ns；与前一轮一致。非ASAN barrier微基准（64条×200000批、test registry）：native中位48.56→47.84ms，基本持平；mixed中位86.70→54.23ms，registry lookup从22.2M→13M。一次mixed样本98.04→63.15ms，存在系统波动，故只支持去重的局部结论。ASAN下native路径反而更慢（约244→278ms），仅用于查错，不将其耗时与Release混比。

新增回归源码与脚本均在 `dev_tests/`；构建会清空 `build/`，各测试可自行重新生成产物。本报告保存了构建前基准与最终验证结论。
