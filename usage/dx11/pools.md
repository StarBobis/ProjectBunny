# 资源池与变量池

池为资源或数值提供有限容量的索引存储。

```ini
[Constants]
global $id = 0

[PoolSaved]
pool_size = 8
pool_index_type = fifo
pool_variable_default_value = -1

[CommandListSave]
PoolSaved[$id] = ref vs-cb0
```

`PoolSaved[$id]` 访问资源；`$PoolSaved[$id]` 访问变量。默认切换类型会清理原类型；不要无意中用数值赋值覆盖资源状态。

## 索引策略

| 类型 | 行为 |
| --- | --- |
| `ring` | 默认；整数截断后循环，容量 4 时 -1→3、4→0 |
| `static` | 单元素索引在解析时解析，只能用数值字面量 |
| `fifo` | 方括号是 float 键；容量满时回收最早分配项 |
| `spatial` | 使用打包空间键，允许附近单元复用 |

FIFO/spatial 缺失读取默认返回模板，不分配；赋值才分配。`pool_allocate_slot_on_missing = 1` 改为读取也分配。

空间查找半径按 120 FPS 标准配置，实际半径随 `FRAME_TIME * 120` 缩放并向上取整。临近对象可能共用槽，不是精确对象跟踪。

## 配置完整表

| 选项 | 默认 | 用途 |
| --- | --- | --- |
| `pool_size` | 1 | 容量，至少 1 |
| `pool_index_type` | ring | 索引策略 |
| `pool_lazy_initialization` | 1 | 按需初始化元素对象 |
| `pool_allocate_slot_on_missing` | 0 | 缺失键读取时分配 |
| `pool_spatial_radius` | 1 | 空间近邻半径 |
| `pool_expiration_timeout_frames` | -1 | 负值不过期；否则按最后更新时间计算 |
| `pool_expiration_reset_elements` | 1 | 到期同时重置内容 |
| `pool_expiration_refresh_on_read` | 0 | 已存在项被读时刷新时间 |
| `pool_element_type_switch_reset` | 1 | 资源/数值类型切换时重置原类型 |
| `pool_variable_default_value` | 0 | 数值初始值和重置值 |
| `pool_persist_variables` | 0 | 保存已初始化的 Ring 数值槽 |

过期在访问时检查，每帧至多一次；static 直接引用绕过运行时查找。旧名 `pool_keep_alive_frames`、`pool_reset_expired_elements` 仍接受，新名优先。

Pool 节可写 Resource 字段作为模板。提前初始化只创建元素对象，不表示全部 GPU 资源已经创建。

## 变量、持久化与重置

```ini
[CommandListSet]
$PoolSaved[$id] = 3
$PoolSaved[*] = 0
$PoolSaved[*] = null
PoolSaved[*] = null
```

`[*] = 数值` 写所有物理变量槽；变量 `null` 恢复数值默认，资源 `null` 重置资源。两种 null 重置保留相反类型。

仅 Ring 变量支持持久化，FIFO/spatial 键不保存。改变容量或策略前，先考虑已保存物理槽的含义。

## 整池操作

```ini
PoolAlias = ref PoolSaved
PoolAlias = copy_desc PoolSaved
PoolAlias = null
```

须先声明 `PoolAlias`。`ref` 代理池；`copy_desc` 复制配置与模板，不备份元素内容；`null` 解除代理或清理索引记账，不等于重写所有内容。循环代理被拒绝。

## 查询

| 表达式 | 用途 |
| --- | --- |
| `PoolSaved->Size` 或 `#PoolSaved` | 物理容量 |
| `PoolSaved[$id]->Index` | 所选物理槽；缺失模板为 -1 |
| `PoolSaved[$id]->LastFrame` | 最后更新时间 |
| `@PoolSaved` | 池身份的紧凑哈希 |
| `@PoolSaved[$id]` | 元素底层资源身份哈希 |

元素 `->Size` 是资源字节大小，与整池容量不同。Hash 比较不保证唯一性。
