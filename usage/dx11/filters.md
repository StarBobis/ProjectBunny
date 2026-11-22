# Filter 与贴图筛选

`[Filter...]` 保存匹配条件，`[Resource...]` 提供替换资源。目前 Filter 用于按 dHash 筛选当前像素着色器的贴图槽位 `ps-t`，适合贴图槽位变化、不同 mip 或画质档位使资源 CRC 改变的情况。它不是通用的任意资源属性查询器。

## 声明

```ini
[FilterDiffuse]
match_dhash = 8cb4796369a42c84
match_format = BC7_UNORM
match_confidence = 0.75
```

| 选项 | 默认值 | 含义 |
| --- | --- | --- |
| `match_dhash` | 未声明 | 原始贴图的 64 位十六进制 dHash；槽位匹配必须提供 |
| `match_format` | 不限制 | 候选原始 SRV 的 DXGI 格式，可写 `BC7_UNORM` 或 `DXGI_FORMAT_BC7_UNORM` |
| `match_confidence` | `0.75` | 最低 Hash 位相似度，范围 `[0,1]` |

参考 dHash 可从本地[帧分析](./frame-analysis)日志的 `dhash=` 字段取得。缺失字段不等于零；显式 `0000000000000000` 是有效参考值。无效 Hash 或格式不会匹配，非有限 confidence 回退到 `0.75`，有限值限制在 `[0,1]`。

## 元数据和引用

```ini
[Constants]
global $filter_id = 0
global $format = 0
global $confidence = 0

[FilterAlias]

[FilterSource]
match_format = BC7_UNORM
match_dhash = 8cb4796369a42c84

[CommandListInspectFilter]
FilterAlias = ref FilterSource
$filter_id = @FilterAlias
$format = FilterAlias->Format
$confidence = FilterAlias->Confidence
FilterAlias = null
```

`@FilterAlias` 返回有效 Filter 的不透明标识；有效定义未声明任何字段时返回 `0`。它不是 dHash，也不能作为可跨重载持久保存的地址。`->Format` 返回 DXGI 枚举值，省略时为 `DXGI_FORMAT_UNKNOWN`；`->Confidence` 默认返回 `0.75`。

`ref` 使目标引用赋值时解析出的源 Filter 根，重复引用可共享条件。`null` 取消引用，恢复目标自身声明的字段，不会删除定义。不能建立循环引用；Filter 不支持资源的 `copy` 模式。

单槽位比较使用 `ps-t0->HammingDistance(FilterSource)`，结果与失败值见[感知纹理匹配](./perceptual-hash)。该表达式只比较 dHash，不替你应用格式或 confidence 条件。

## 替换多个贴图

下面的命令列表由游戏适配的 ShaderOverride 或 TextureOverride 调用。Hash、文件和绘制参数都要按实际资源调整。

```ini
[FilterDiffuse]
match_dhash = 8cb4796369a42c84
match_format = BC7_UNORM

[FilterNormal]
match_dhash = 56577a2d6a2759a8
match_format = BC5_UNORM

[ResourceDiffuse]
filename = Diffuse.dds

[ResourceDiffuseAlternate]
filename = DiffuseAlternate.dds

[ResourceNormal]
filename = Normal.dds

[Constants]
global $variant = 0

[CommandListDrawFiltered]
run = SnapshotInitialTextures
if $variant == 0
    ps-t[FilterDiffuse] = ref ResourceDiffuse
else
    ps-t[FilterDiffuse] = ref ResourceDiffuseAlternate
endif
ps-t[FilterNormal] = ref ResourceNormal
drawindexed = 1000, 0, 0
run = RestoreInitialTextures
; 此次绘制使用已恢复的贴图。
drawindexed = 1000, 1000, 0
```

每个 Filter 遍历当前 PS 声明的候选槽，先检查原始 SRV 格式，再比较参考 dHash。只替换距离最小且满足 confidence 的槽位；最小距离并列时一起替换。同槽位后来的候选必须严格更好才能覆盖已有结果，同距离保留先到者。比较基准始终是快照，不是先前安装的替换贴图。

替换源为空、Hash 不可用或 SRV 创建/绑定失败时，不占用最佳匹配名额。其他着色器阶段不支持 `*-t[Filter...]`。复制选项和本地兼容入口见[资源声明](./resources#按-dhash-匹配像素着色器贴图槽位)。

## 快照与恢复的执行时机

“初始”指捕获时刻的绑定，不一定是游戏最早的绑定。省略 `SnapshotInitialTextures` 时，首次 Filter 槽位赋值自动捕获；此前直接修改的槽位也会进入快照。要保留修改前状态，先显式捕获。同一次被拦截 draw 内重复捕获不会覆盖快照或重置候选竞争。

`run = RestoreInitialTextures` 默认在 pre 列表中按顺序执行，因此可以放在两条自定义 draw 命令之间，如上例。恢复可以重复执行，也会重置各槽位的最佳距离，让下一轮筛选重新竞争。

如果让游戏执行原始 draw，应把清理放在 post：

```ini
[ShaderOverrideFiltered]
hash = 0123456789abcdef
run = SnapshotInitialTextures
ps-t[FilterDiffuse] = ref ResourceDiffuse
post run = RestoreInitialTextures
```

已有适配包可能负责捕获和恢复，添加前先检查它调用的命令列表，避免提前恢复替换贴图。本地旧名 `SnapshotOriginalTextures` 默认 pre，`RestoreOriginalTextures` 为兼容旧配置仍默认 post；显式 `pre run` / `post run` 优先。

快照绑定到 Context、当前 draw/dispatch、PS 和配置代次；调用结束、ClearState、Context 销毁或配置重载使其失效。跨 draw 的旧快照不能用来恢复当前绑定。没有可用 PS 或使用 deferred context 时安全跳过。

## 手动实现同类筛选

上面的槽位赋值已内置以下过程，通常不必在 INI 中重复实现：

1. 保存 PS 声明槽位的原始 SRV，并将每槽最佳距离设为 `65`（超出有效范围）。
2. 对每个原始候选检查格式，计算 `HammingDistance(FilterReference)`；忽略负值和不可用资源。
3. 找出最小距离，只有它不超过 `floor((1-confidence)*64)` 才继续。
4. 对所有并列最小槽位，确认比该槽之前的匹配更好，再尝试绑定替换资源；仅绑定成功才记录新的最佳距离。
5. 绘制后恢复原始 SRV，重置最佳距离。

手动脚本可用[资源池](./pools)保存引用、用[动态槽位](./resource-copy)逐槽比较，但必须保留上述失败处理与快照生命周期。[阈值计算和调参](./perceptual-hash#相似度与阈值)解释了如何选择允许的位差。
