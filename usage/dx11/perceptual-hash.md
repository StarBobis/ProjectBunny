# 感知纹理匹配

`HammingDistance` 比较目标纹理的 64 位 dHash 与参考值。结果越小，位差越少；它不是像素相等判定。

```ini
[FilterReference]
match_dhash = 0123456789abcdef

[Constants]
global $distance = -1

[CommandListCompare]
$distance = ps-t0->HammingDistance(FilterReference)
if $distance >= 0 && $distance <= 8
    x = 1
endif
```

将 `match_dhash` 换成原始贴图的 64 位 Hash，并按实际贴图选择阈值。参数可引用已声明的 `[Filter...]`，也兼容旧的 `[Resource...]` 参考资源。过滤器只存匹配参数，无需贴图文件。这个表达式只比较 Hash，`match_format` 和 `match_confidence` 用于[按 dHash 替换 PS 槽位](./resources#按-dhash-匹配像素着色器贴图槽位)。

## 结果

| 值 | 含义 |
| --- | --- |
| 0–64 | 可用距离 |
| -1 | dHash 无法计算，或引用的 Filter 缺少有效参考 Hash |
| -2 | 缺少目标或设备/上下文 |

两 Hash 均为零时距离为 0，仅一方为零时为 64。Filter 的全零 Hash 须显式写成 `match_dhash = 0000000000000000`。旧参考资源省略 Hash 时仍沿用零值行为。

目标需是受跟踪、已有上传数据、单层、非 MSAA、可采样的 Texture2D，并在即时上下文对应设备上使用。数组、3D、缓冲和延迟上下文不适用。

## mip 与更新策略

```ini
[Rendering]
dhash_mip_size = 256
dhash_allow_rehash = 0
```

正的 mip 尺寸从小向大查找满足双边尺寸的已初始化 mip，mip 0 可作尺寸回退；0 从最大 mip 优先。视图的 mip 窗口不限制物理 mip 链查找。

默认保留相同采样格式与 mip 策略下已有的 Hash，之后上传不一定改变结果。要跟随新上传重算，设 `dhash_allow_rehash = 1`。

首次计算可能同步 GPU 读回，控制调用频率。帧分析日志中的 `dhash=` 可作为参考来源；缺失字段不是成功计算得到的零。
