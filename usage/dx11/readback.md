# GPU 读回

`store` 将缓冲中的一个 float32 读到 INI 变量：

```ini
[Constants]
global $value = 0

[CommandListRead]
store = $value, vs-cb0, 4
```

最后一项是**底层缓冲起点起算的 float 索引**；4 读取第 5 个值，即字节 16。它不会自动加上 CB/SRV 的窗口偏移。

窗口首值可按当前 Offset 换算：

```ini
store = $value, vs-cb0, vs-cb0->Offset / 4
```

## 结果与失败

| 情况 | 结果 |
| --- | --- |
| 合法且成功 | 更新目标变量 |
| 合法索引越过物理缓冲、读回失败 | 写 0 |
| 源为空、非缓冲、设备或上下文不适用 | 保持原值 |
| 非有限或非法索引 | 保持原值 |
| 目标 locked | 不可写 |

也可输出到明确的变量池元素：`store = $PoolState[$id], vs-cb0, 4`。遵循变量池分配、类型切换与持久化规则。

## 成本

GPU 路径会复制并等待 CPU Map；复用 staging 内存不等于避免同步。不要在每个 draw 中反复读同一数据。

需要时使用帧号限定频率，并按更新路径配置 `cache_resource_data`。有覆盖目标范围且仍有效的 CPU 快照时可避免 GPU 读回；缓存失效后会回退。
