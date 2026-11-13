# 第三轮：按运行场景交叉对抗审查

起点为本地 `0f0bafc`、上游 `eae473b3c3b833380e70ba9dc61a45c9937ef924`。本轮上游检查仍为 336 个提交，无新增。上一轮已逐提交覆盖；本轮改变切面，审查跨功能组合、资源身份与生命周期、非绘制调用上下文、失败恢复及测试 oracle，不将其表述为再次逐行读完 336 个补丁。

三个子代理分别审查组合路径、生命周期、测试与故障恢复；主代理独立追踪非绘制调用，复核发现并负责生产修复、接线与集成运行。

## 确认并修复

| 场景 | 运行证据 | 修复和验证范围 |
| --- | --- | --- |
| Present/Constants 等无 draw 参数时读取 VB/IB 元数据 | 真实 WARP 绑定、生产表达式读取 `vb0->offset`，修复前回归进程 SEGFAULT。RunCommandList 的这些入口传入 null call_info。 | Offset helper 无 draw 时保留绑定偏移；长度 helper 返回 0，让区域匹配回退。真实表达式覆盖 Offset/HashRegion/SpatialHash、非零绑定偏移、有效 draw 的首顶点/首索引贡献、缺失绑定。Dispatch 通常有其自己的 call_info，不能笼统等同于 null 路径。 |
| 动态 pool 属性重复选择资源 | `pool[random(1)]` 的真实随机采样计数：HashRegion、SpatialHash、Stride、Format、Size 修复前均为 2。前两项还可能用一次选择的资源配另一次选择的 CPU 快照；本轮直接运行证明了重复选择，未声称直接观察两个不同元素错配。 | 九个可能二次选择的 getter 使用本次调用的 resolved 副本：上述五项及 Width/Height/Array/Mips。原 parsed target 不改写；解析结果为空也不重选。回归覆盖单次采样、同一 target/共享副本多次读取、真实非有限索引缺失路径、buffer 元数据与类型错误、4×3 texture 的正常维度值。 |
| 资源销毁与 COM 地址复用 | 实际 WARP 重用同一地址。旧 CRC `8298e770`；修复前 replacement 仍为 `8298e770`，新数据期望 `5c72ff8e`。修复后 replacement 为 `5c72ff8e`。另有真实移除 tracker 的确定性路径验证注册消失及缓存失效。 | tracker 在释放资源注册锁后，仅持 L3 mutex 定向删除该地址的条目，不从原生释放回调新增 G 锁，不删除其他资源键。FlatHashMap 的 erase_if 无临时分配，覆盖回绕 collision cluster、混合删除、全部/无匹配与计数。空缓存立即返回；非空成本为容量扫描及 cluster 压缩，未测帧率收益或成本。 |

这些缺陷大部分也存在于上游；Stride 的重复选择还会在本地修复上游 fallthrough 后出现。为保留正确行为而修复，并非为了逐字一致恢复上游缺陷。

旧 `texture_override_cache_regression` 曾要求 tracker 移除后仍能通过热 L3 区域 hash 匹配。这实际固定了本轮确认的残留缓存行为，已改为要求孤立区域 hash 与 descriptor 候选均失效；正常追踪资源的两个区域精确匹配与模糊候选测试保留。

## 补强失败恢复 oracle

此前部分 Map observer 只统计并转发成功调用，不能证明原生 READ Map 真正失败后的恢复。本轮对选定真实 context 的下一次 READ Map 注入一次 E_FAIL，随后恢复原生转发，不伪造成功数据：

- 真实解析 Store/PERSIST：读取失败输出 0 并标 dirty；下一次成功读取真实 GPU 值 17。
- 真实 dHash：失败返回 unavailable，不发布有效缓存；成功重试计算真实黑色零 hash，并将零结果发布为有效缓存。
- hook 按作用域恢复。回归捕获各独立场景的异常后汇总失败，任何场景失败都令总测试失败。

独立 getter 适配器同步提取真实 resolution helper，保留静态 WARP getter/COM 引用测试。动态 selector 的正确性由完整生产回归承担，未用简化适配器冒充动态 INI 集成验证。

新增回归位于 `src/DirectX11/tests/round3_{nondraw_metadata,composition,lifetime,oracle}_regression.cpp`，全部接入 CMake 和现有完整生产回归入口。

## 已挑战而未确认新问题的路径

组合审查包括 selected Region 的独立 copy 零基址发布、ref 窗口保留、pool proxy root/物理 slot 定位、original format/dHash 的携带。生命周期审查包括 proxy cycle 拒绝、copy_desc 重置、配置重载前候选失效、dHash identity/generation 发布检查及 L3/G/资源锁顺序。oracle 审查核对了实际目标源码的 CMake 接线、独立 CPU dHash oracle、持久化真实文件共享锁失败与重试。这些是有界源码与回归证据，不是穷举线程交错或所有配置的证明。

## 验证边界

最终验证：项目 MSVC 环境下 CMake Ninja Release 的 DirectX11 与完整生产回归目标构建通过；完整 CTest 3/3 通过（12.76 秒）；独立 pool flags/resource properties 回归通过；DirectX12 兼容构建通过；38 页、87 个本地文档链接/锚点/代码围栏检查通过；`git diff --check` 通过。

red/green 构建、CTest 输出及子代理源链材料保存在 ignored `build/upstream/adversarial-round3/`。本文保存结论和具体观察值，原同步账本关联本轮报告。

未启动或部署目标游戏，未执行上游预编译 DLL 全功能差分；完整生产回归链接实际源码，但不等于目标进程已加载代理 DLL。上一轮记录的有意兼容差异、原生 WARP 非零 first-array-slice GenerateMips 限制、既有部分命令写入口无统一锁的边界继续保留。本轮没有据此声称任意配置和线程交错下绝对无误。
