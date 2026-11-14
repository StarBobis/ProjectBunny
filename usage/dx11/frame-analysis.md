# 帧分析

## 抓取缓冲

```ini
[Hunting]
analyse_frame = no_modifiers F8
analyse_options = dump_cb dump_vb dump_ib buf txt
```

按 F10 后，在目标可见时按 F8。检查 FrameAnalysis 输出目录中的日志、资源和对应 draw 编号。

## 选项

| 选项 | 输出/行为 |
| --- | --- |
| `dump_rt` | 渲染目标与 UAV |
| `dump_depth` | 深度模板 |
| `dump_tex` | 着色器输入资源 |
| `dump_cb/dump_vb/dump_ib` | 常量、顶点、索引缓冲 |
| `buf/txt` | 二进制/解码文本缓冲 |
| `dds` | DDS 纹理；完整数据可能很大 |
| `jpg/jps` | 较小的预览图片，不能覆盖所有资源 |
| `jps_dds` | 可预览时用 JPS，否则 DDS |
| `desc` | 资源描述 |
| `hold` | 按住键连续抓帧 |
| `clear_rt` | 首次使用目标时清空，可能改变效果 |
| `filename_reg` | 文件名先按寄存器排序 |
| `mono/stereo` | 单目/立体输出 |
| `dump_on_unmap` | 捕获 Map/Unmap 更新 |
| `dump_on_update` | 捕获 UpdateSubresource 更新 |
| `share_dupes` | 跨抓帧目录共享去重文件 |
| `symlink` | 尝试符号链接去重 |
| `deferred_ctx_immediate` | 用即时上下文抓延迟上下文资源，存在状态与线程限制 |
| `deferred_ctx_accurate` | 延后抓取以保留更新后数据，额外保存资源 |

延迟上下文优先限定具体资源，不全量打开。`hold` 与大范围 DDS 组合会迅速消耗磁盘。

## 定点导出

在匹配的 ShaderOverride 或命令列表中：

```ini
pre dump = buf txt vs-cb0
post dump = dds o0
```

`pre` 在原始操作前，`post` 在操作后。`dump` 可指定自定义资源、动态槽和明确范围。

```ini
analyse_options = dump_tex dds
```

放在 Override 中可改变该 draw 的分析选项；加 `persist` 可保留到后续分析。先用小范围抓取确认触发，再增加数据量。
