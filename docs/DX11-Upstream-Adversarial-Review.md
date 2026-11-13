# DX11 上游一致性对抗审查

审查基于本地 `a6f1f7e` 与上游 `eae473b3` 的实际实现，由两路独立代理核对，主代理通过真实生产/WARP回归复现后修复。不是对所有游戏、驱动或全部输入的等价证明。

## 已确认并修复

1. **CB 的引用大小限制误用于复制。** `cs-cb0 = copy vb0->Region(0,131072)` 在有足够数据的源上，上游裁剪为65536字节CB，本地却直接拒绝。新回归先复现失败；修复只让64KiB限制约束引用窗口，复制仍检查物理源范围并沿用原生CB容量裁剪。新测试实际核对独立CB分配及每个字节，并保留非法引用和越界不改目标断言。
2. **BC 小 mip 私有采样副本不合法。** 上游可直接采样合法BC1纹理的2×2/1×1 mip；本地将它作为私有资源顶层，创建失败。新回归先用原纹理SRV和实际CS证明小mip是绿色，再捕获本地 `80070057`。修复创建块对齐顶层，把数据放在尺寸完全相同的低mip，SRV从该层采样，保持归一化坐标和状态隔离。BC1三层的原生采样和精确dHash均通过。
3. **Store 失败输出契约偏离上游。** 有效UINT索引在运行时物理越界，或合法读回失败时，上游写0，本地保留旧值。新测试证明动态索引越界会留下旧值；现恢复写0，包含32位索引对应的大于4GiB字节偏移（使用UINT64计算）。正常读取、LOCKED、空源保留行为不变；负数、NaN、不可表示UINT、非buffer、deferred及设备不匹配的本地安全防护保留。零值通过正常池赋值发布，STATIC类型重置、更新时间及persist脏位也按真实赋值验证。此前账本/历史报告的 failure-no-change 选择由本次更正明确取代，不代表上游契约。

## 验证证据

- 先失败：`build/upstream/adversarial-region-red-ctest.log`、`adversarial-bc-red-ctest.log`、`adversarial-store-red-ctest.log`。
- 修复后完整生产构建：`adversarial-green-build.log` 与最终 `adversarial-static-fix-build.log`。
- 完整CTest3/3：`adversarial-final-ctest.log`，legacy0.01秒、版本生成9.56秒、主回归4.67秒，总14.41秒。
- 隔离回归、DX12兼容及文档检查：`adversarial-isolated.log`、`adversarial-dx12.log`、`adversarial-docs.log`。

用户已暂缓目标游戏测试，未部署或启动游戏。之前记录的WARP非零FirstArraySlice mip生成差异仍保留为目标驱动待验证项。当前结论是已修复本轮确认的不一致，不是“绝无其他差异”。
