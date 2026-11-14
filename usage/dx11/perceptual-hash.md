# 感知纹理匹配

`HammingDistance` 比较目标纹理的 64 位 dHash 与参考值。结果越小，位差越少；它不是像素相等判定。

```ini
[ResourceReference]
original_format = R8G8B8A8_UNORM
original_dhash = 0123456789abcdef

[Constants]
global $distance = -1

[CommandListCompare]
$distance = ps-t0->HammingDistance(ResourceReference)
if $distance >= 0 && $distance <= 8
    x = 1
endif
```

替换参考 Hash，并按实际图片选择阈值。参数必须是资源引用，不是内联十六进制值；参考资源可以只有元数据，无需文件。

## 结果

| 值 | 含义 |
| --- | --- |
| 0–64 | 可用距离 |
| -1 | 格式、跟踪数据、上下文或计算不支持/不可用 |
| -2 | 缺少目标或设备/上下文 |

两 Hash 均为零时距离为 0，仅一方为零时为 64。省略 `original_dhash` 也得到零，务必显式填写参考。

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
