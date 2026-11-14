# 绘制、Dispatch 与清理

这些命令属于命令列表，需要在资源与管线已正确绑定时调用。

| 命令 | 参数顺序 |
| --- | --- |
| `draw` | 顶点数，首顶点 |
| `drawindexed` | 索引数，首索引，基础顶点 |
| `drawinstanced` | 每实例顶点数，实例数，首顶点，首实例 |
| `drawindexedinstanced` | 每实例索引数，实例数，首索引，基础顶点，首实例 |
| `dispatch` | X、Y、Z 线程组数 |
| `drawinstancedindirect` | 参数缓冲目标 |
| `drawindexedinstancedindirect` | 参数缓冲目标 |
| `dispatchindirect` | 参数缓冲目标 |
| `drawauto` | 使用流输出计数，无数值参数 |

```ini
handling = skip
ib = ResourceIndices
vb0 = ResourceVertices
drawindexed = 300, 0, 0
```

这是一段模型替换命令，需放在匹配到的 Override 中，并声明资源。替换绘制时先跳过原始 draw，避免双重显示。

## 自动参数

- `draw = from_caller`：复用调用者 draw 参数。
- `draw = auto`：从当前顶点资源推导数量。
- `drawindexed = auto`：从当前索引资源推导数量。
- `drawindexedinstanced = auto`：索引实例绘制的自动数量路径。

自动数量不替你选择正确子网格或修复错误布局。

## 清理

```ini
clear = cs-u0 0 0 0 0
```

`int` 或 `0x` 数值选择整数 UAV 清理；`depth/stencil` 可限定 DSV 的清理部分。例如 `clear = od depth 1`、`clear = od stencil 0`。未填满四通道时用最后一个值补齐。

清理目标需支持相应视图类型。深度、模板或整数 UAV 按目标类型使用对应清理语义；确认数值解释而非仅看文本一样。

`[ClearRenderTargetView]` 等固定节可在游戏清理操作前后执行命令。不要在每次 draw 无条件清空游戏仍需使用的资源。
