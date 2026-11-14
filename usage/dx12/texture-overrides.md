# TextureOverride 与模型替换

```ini
[TextureOverrideBody]
hash = 12345678
match_first_index = 0
match_index_count = 300
handling = skip
ib = ResourceIndices
vb0 = ResourceVertices
drawindexed = 300, 0, 0
```

这是替换片段：需要真实 Hash、文件资源、正确顶点布局和绘制范围。没有这些前提不能直接用于任意游戏。

## 匹配字段

| 字段 | 含义 |
| --- | --- |
| `hash` | 32 位资源 Hash |
| `match_vertex_count/match_index_count/match_instance_count` | 绘制数量 |
| `match_first_vertex/match_first_index/match_first_instance` | 绘制起点 |
| `match_priority` | 匹配优先级 |

数值匹配支持比较形式。资源 Hash 匹配范围过宽时，先加 draw 条件，不用碰运气的目录排序解决。

## 替换字段

| 字段 | 用途 |
| --- | --- |
| `handling = skip` | 跳过原始绘制 |
| `ib`、`vbN` | 索引、顶点缓冲 |
| `ps-tN`、`vs-tN` | 指定纹理/缓冲 SRV |
| `this` | 替换命中的贴图资源 |
| `override_byte_stride` | 扩容相关顶点字节步长 |
| `override_vertex_count` | 扩容相关顶点数 |
| `uav_byte_stride` | 输出缓冲步长 |

`this` 与阶段槽赋值可放在分支中，由变量决定当前资源。资源路径必须存在；扩容参数不能替代真实数据和骨骼计算。

## 计算输出替换

GPU 蒙皮相关路径还接受：

| 字段 | 用途 |
| --- | --- |
| `cs_hash` | 限定计算着色器 |
| `match_cs_tN_hash` 或 `match_srvN_hash` | 计算输入资源 Hash |
| `cs-tN` 或 `srvN` | 对应计算输入资源 |
| `match_uav_bytes` | 计算输出匹配相关字段 |

`match_uav_bytes` 在当前解析器中只记录存在标记，不能把它当作已经验证的精确字节筛选。先用帧分析确认实际匹配与输出路径。

这类配置依赖游戏蒙皮流程。先确定输出由哪个 dispatch 生产、哪次 draw 消费，再调整输入、输出容量与 dispatch；只改最终 draw 无法保证动画正确。
