# 区域与范围

## Buffer 区域

```ini
ResourceWindow = ref vs-cb0->Region(256, 512)
ResourceSnapshot = copy vs-cb0->Region(20, 32)
```

以上变量资源须先声明。`Region(offset,size)` 的两项都是字节，offset 相对底层缓冲起点，第二项是长度，不是结束位置。

参数须有限、offset 非负、size 至少 1，且在 uint32 范围内；小数先截断。非法区域不更新目标。

| 操作 | 限制 |
| --- | --- |
| `copy` | 起点在物理缓冲内；越界尾部只复制可用前缀，目标剩余内容未定义 |
| 引用到 CB | offset、size 都需 256 字节对齐，size 不超过 65536 |
| 保存区域到自定义资源 | 保留绝对 offset 与长度；后续 SRV/UAV 可使用该窗口 |

普通 CB `ref` 不等于保存完整绑定窗口；绑定到另一个 CB 时可能选择整个缓冲。需要窗口就明确写 `Region`。

## 动态槽

```ini
[Constants]
global $slot = 0

[ResourceSaved]

[CommandListCapture]
ResourceSaved = ref unless_null ps-t[$slot]
```

支持 `t/cb/u/vb/o/so` 的动态单槽，受各自槽上限限制。非法源被视为空；`unless_null` 可保留旧目标。非法目标不修改绑定。

## 闭区间

```ini
[PoolSaved]
pool_size = 4

[CommandListSave]
PoolSaved[0:3] = ref ps-t[0:3]

[CommandListRestore]
ps-t[0:3] = ref PoolSaved[0:3]
```

`[0:3]` 包含两端，共 4 项。支持各阶段 `t/cb`、PS/CS `u` 及资源池物理槽范围。

- 双范围必须等长，且显式写出两端。
- 单个资源可广播到目标槽范围。
- 范围无效则跳过操作；不能用裸 `ps-t` 省略范围。
- 池范围按物理槽 Ring 归一化，不按 FIFO/spatial 键查找。
- 不支持 `$Pool[first:last]` 变量子范围。
- `Region` 用于标量操作，不与槽范围合写。

## 检查与导出

```ini
CheckTextureOverride = ps-t[0:3]
dump = PoolSaved[0:3]
```

逐项检查时，`this` 指向当前项。整池没有单个资源可导出，须指定元素或范围。
