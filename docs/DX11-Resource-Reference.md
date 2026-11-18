# DX11 资源参考

本文面向使用 ProjectBunny / XXMI DX11 DLL 的 Mod 作者与维护者，覆盖 DX11 侧的资源类型、声明与加载、格式、复制与引用、区域、输入布局、流输出、元数据访问、感知哈希（dHash）、GPU readback 以及资源池的索引与生命周期。

**范围与证据**：本文按当前 `src/DirectX11` 的注册、解析与消费路径撰写（`IniHandler.cpp`、`CommandList.cpp`、`ResourceHash.cpp`、`HackerDevice.cpp`、`HackerContext.cpp`、`TextureMatch.cpp`、`DHash64Gpu.cpp`、`globals.h`、`CommandList.h`）。文中的 INI 片段是**语法示例片段**，不是可直接运行的完整配置；本文没有执行游戏实测，也没有给出帧率或性能结论。资源语法扩展的既有细节见 [ini-language-extensions](ini-language-extensions/README.md)，本文只做汇总与补充，冲突时以源码为准。

**术语约定**：`[ResourceXxx]` / `[PoolXxx]` / `[TextureOverrideXxx]` 是 INI 段名；`copy`、`ref`、`->Region()` 等保持原文写法；`ID3D11Resource`、`DXGI_FORMAT`、`D3D11_BIND_*` 等保持 SDK 标识符。

---

## 1. 资源类型与标志

### 1.1 自定义资源类型

`[Resource*]` 段的 `type` 键取自 `CustomResourceType`（`src/DirectX11/CommandList.h`）：

| `type` 值 | 内部枚举 | 说明 |
| --- | --- | --- |
| `Buffer` | `BUFFER` | 普通缓冲区 |
| `StructuredBuffer` / `AppendStructuredBuffer` / `ConsumeStructuredBuffer` | `STRUCTURED_BUFFER` | 结构化缓冲区；三者共用同一枚举值 |
| `ByteAddressBuffer` | `RAW_BUFFER` | raw 视图缓冲区 |
| `Texture1D` | `TEXTURE1D` | 一维纹理 |
| `Texture2D` | `TEXTURE2D` | 二维纹理 |
| `Texture3D` | `TEXTURE3D` | 三维纹理 |
| `TextureCube` | `CUBE` | 立方体贴图，按二维纹理路径创建 |

`RWBuffer`、`RWStructuredBuffer`、`RWByteAddressBuffer`、`RWTexture1D`、`RWTexture2D`、`RWTexture3D` 是同名的 `RW` 变体：**枚举值与上表相同**，因为决定行为的是用途（usage）而不是读写方向。未列出的名称解析为 `INVALID`。

### 1.2 绑定标志 `bind_flags`

`bind_flags` 用空格分隔的标志名，值与 `D3D11_BIND_FLAG` 一致（`CustomResourceBindFlags`）：

`vertex_buffer`、`index_buffer`、`constant_buffer`、`shader_resource`、`stream_output`、`render_target`、`depth_stencil`、`unordered_access`、`decoder`、`video_encoder`。

适用条件与失败路径：

* 声明 `bind_flags` **替换**了 3DMigoto 自动推导的绑定标志，而不是与之合并。源码注释明确说明这是给需要影响驱动启发式（例如让同时带 RT/UAV 的缓冲区被立体化）的场景用的，请自行确认后再用。
* `constant_buffer` 不能与其他绑定标志组合：`CustomResource::AddFlags` 检测到冲突时输出 `Cannot combine constant-buffer and other bind flags` 并**拒绝该次标志变更**——它在写回 `bind_flags` / `misc_flags` 之前就返回失败，因此原有标志保持不变。
* 该提示与 `Invalid flag`、`Unknown format`、`Cannot use hash= and match options together!` 等 INI 警告走同一条通道 `LogOverlayW(LOG_WARNING, ...)`：日志文件开启且日志级别不低于 WARNING 时会**写入日志文件**，同时在 `gShowWarnings` 打开时显示在叠加层上。唯一的差别是它**不**设置 `ini_warned` 标志，因此不会触发配置重载后的那条失败提示音。
* 已经创建出来的原生资源无法获得新的绑定标志；相关说明见 [Stream Output → Bind flags](ini-language-extensions/resources/stream-output.md#bind-flags)。
* 若声明了 `constant_buffer`，`BUFFER_STRUCTURED` 与 `BUFFER_ALLOW_RAW_VIEWS` 会被自动清除。

### 1.3 其他标志 `misc_flags`

`misc_flags` 用空格分隔的标志名，值与 `D3D11_RESOURCE_MISC_FLAG` 一致（`ResourceMiscFlags`）：

`generate_mips`、`shared`、`texturecube`、`drawindirect_args`、`buffer_allow_raw_views`、`buffer_structured`、`resource_clamp`、`shared_keyedmutex`、`gdi_compatible`、`shared_nthandle`、`restricted_content`、`restrict_shared_resource`、`restrict_shared_resource_driver`、`guarded`、`tile_pool`、`tiled`、`hw_protected`。

同样地，声明后**替换**自动推导值。写错标志名会输出 `Invalid flag` 警告并整体拒绝该字段。

### 1.4 模糊匹配用的标志名

`[TextureOverride*]` 的模糊匹配键使用另一套命名（用于 `match_*`）：`match_bind_flags` 复用上表的绑定标志名；`match_cpu_access_flags` 取 `write` / `read`；`match_misc_flags` 复用上表的 misc 标志名。这三者的匹配方式支持 `0x1234 / 0xffff` 掩码写法以及 `+flag` / `-flag` 增量写法，详见 [TextureOverride 匹配](#134-模糊匹配键)。

---

## 2. 配置到运行的路径

理解“配置项在什么时候真正生效”对排查问题很关键。DX11 侧的资源处理分三个阶段。

### 2.1 解析阶段（配置重载时）

`IniHandler.cpp` 的 `ParseResourceSections()` 在每次配置重载时执行：

1. 先清空 `customResourcePools` 与 `customResources`。
2. 遍历所有以 `Pool` 开头的段，调用 `ParseResourcePoolSection()`：读取池选项，然后以 `<段名>_template` 为后缀再次调用 `ParseResourceSection()` 生成**模板资源**（`CustomResourcePool::resource_template`）。
3. 遍历所有以 `Resource` 开头的段，调用 `ParseResourceSection()`。

`ParseResourceSection()` 的要点：

* 段名统一转小写作为资源 id，因此 `[ResourceFoo]` 与 `[resourcefoo]` 指向同一个资源。
* 空的 `[Resource*]` 段是合法的，等价于声明一个变量占位。
* `filename` 的路径在**解析时**解析并展开为绝对路径；路径过长时输出 `Resource filename rejected: path too long` 警告并**放弃该资源**（返回 `nullptr`）。池模板因此被拒绝时，整个池会被丢弃并输出 `Resource pool rejected: template filename path too long`。
* `format`、`match_format`/`original_format`、`match_dhash`/`original_dhash`、`match_confidence`、`color_space`、`width`/`height`/`depth`/`mips`/`array`/`msaa`/`msaa_quality`/`byte_width`/`stride`、`width_multiply`/`height_multiply`、`bind_flags`、`misc_flags`、`data`、`max_copies_per_frame` 都在这一步解析。
* `match_format` 存在时优先于 `original_format`；`match_dhash` 存在时优先于 `original_dhash`。二者互为别名，写其一即可。
* `match_confidence` 默认 `0.75`；非有限值输出 `Invalid match_confidence; using 0.75` 警告并回落到 `0.75`；随后被夹到 `[0, 1]`。

### 2.2 实例化阶段（第一次真正使用时）

`CustomResource::Substantiate()` 在资源第一次被需要时执行，且**只执行一次**（`substantiated` 标志）：

* 如果该资源已经通过其他途径（`copy`/`ref`）获得了原生资源或视图，就不会覆盖它。
* 有 `filename` 时走 `LoadFromFile()`；否则按 `type` 走 `SubstantiateBuffer()` / `SubstantiateTexture1D()` / `SubstantiateTexture2D()` / `SubstantiateTexture3D()`。
* 实例化之前会把当前赋值需要的绑定标志并入该资源（这是解析期无法推导的部分）。
* 实例化被推迟到此时，是为了确保使用游戏真正在渲染的设备——某些游戏在探测硬件时会创建多个设备。

失败路径：

* 文件型缓冲区：`CreateFile` 失败输出 `Failed to load custom buffer resource <file>: <err>`；读取长度不符输出 `Error reading custom buffer from file <file>`。此时该资源保持为空。
* 文件型纹理：加载失败输出 `Failed to load custom texture resource <file>: 0x<HRESULT>`。该资源保持为空。

### 2.3 释放与配置重载

* 原生资源通过 `ResourceReleaseTracker`（一个挂在 `ID3D11DeviceChild::SetPrivateDataInterface()` 上的 COM 对象）感知释放，资源释放后从 `G->mResources` 中清除，避免地址复用后使用过期数据。
* 配置重载会重建 `G->mTextureOverrideMap` / `G->mFuzzyTextureOverrides`，并调用 `InvalidateTextureOverrideCandidates()`、`InvalidateResourceDataCaches()`、`InvalidateTextureMatchSnapshots()` 丢弃旧的候选缓存、CPU 快照与感知匹配快照。**旧快照不会跨重载沿用**。
* `G->mResources` 由独立的 `G->mResourcesLock` 保护；调用 DirectX 时不得持有该锁。取 `mCriticalSection` 时必须先于 `mResourcesLock`，否则可能形成 AB-BA 死锁。

---

## 3. 声明与加载

### 3.1 `[Resource*]` 段键表

| 键 | 默认值 | 适用条件 | 失败路径 |
| --- | --- | --- | --- |
| `filename` | 无 | 相对路径按 §3.2 解析 | 路径过长 → 拒绝该资源 |
| `type` | `INVALID` | 见 §1.1 | 未列出 → 解析为 `INVALID` |
| `format` | 无 | `DXGI_FORMAT` 名或十进制数值 | 未知名称 → `Unknown format "<x>"` 警告，格式保持未设置 |
| `data` | 无 | 仅缓冲区；不能与 `filename` 同用 | 纹理 → `Initial data currently only supported on buffers` 警告 |
| `color_space` | `DEFAULT` | `sRGB` / `Linear` | 省略或非法 → 自动处理；非法值另加配置警告 |
| `bind_flags` | 自动推导 | 替换而非合并 | 与 `constant_buffer` 冲突 → 警告并拒绝 |
| `misc_flags` | 自动推导 | 替换而非合并 | 非法标志 → `Invalid flag` 警告并拒绝该字段 |
| `width` / `height` / `depth` / `mips` / `array` / `msaa` / `msaa_quality` / `byte_width` / `stride` | `-1` | 覆盖创建描述符；`-1` 表示不覆盖 | 非 `-1` 的负值在创建期被拒绝 |
| `width_multiply` / `height_multiply` | `1.0` | 创建期按倍数缩放 | 非有限值或溢出 → 警告并放弃该次覆盖 |
| `original_format` / `match_format` | `-1`（无） | 参考元数据 | 未知名称 → 警告；仍保持 `-1` |
| `original_dhash` / `match_dhash` | `0` | 参考元数据，精确 64 位 | 非法字面量 → `Hash parse error` 警告并使用 `0` |
| `match_confidence` | `0.75` | 感知匹配阈值 | 非有限 → 警告并回落到 `0.75`；随后夹到 `[0,1]` |
| `max_copies_per_frame` | `0` | 限制每帧复制次数 | — |

### 3.2 文件路径解析

`filename` 的解析顺序：

1. 若该段来自带命名空间的配置文件，先按“**配置文件所在目录 + 相对路径**”查找；找到即采用。
2. 否则（或上一步没找到）按“**DLL 所在目录 + 相对路径**”查找。

路径拼接有长度上限，超限即拒绝该资源（不是截断）。因此把资源文件放在 Mod 自己的目录里时，命名空间相对路径是更可靠的选择。

### 3.3 加载格式

`CustomResource::LoadFromFile()` 按扩展名与 `type` 分流：

* `type` 为 `Buffer` / `StructuredBuffer` / `ByteAddressBuffer` → `LoadBufferFromFile()`：整个文件原样读入内存并作为缓冲区初始内容。
* 扩展名为 `.dds`（大小写不敏感）→ `CreateDDSTextureFromFileEx()`。
* 其余 → `CreateWICTextureFromFileEx()`（PNG/JPG 等 WIC 支持的格式）。

纹理路径下 `bind_flags` / `misc_flags` 的覆盖值会直接传给加载器；缓冲区路径则由 `SubstantiateBuffer()` 用 `bind_flags` / `misc_flags` 创建。

`SubstantiateBuffer()` 的细节：`Usage` 固定为 `D3D11_USAGE_DEFAULT`；字节宽度默认取文件长度或 `data` 初始数据长度，若声明了 `byte_width` 则以声明值为准；没有文件也没有 `data` 时仍会创建缓冲区，并用零初始化。

### 3.4 初始数据 `data`

`data` 只支持缓冲区。格式可以在数据行首内联指定，也可以用独立的 `format` 键：

```ini
[ResourceStructured]
type = StructuredBuffer
format = R32_FLOAT
data = R32_FLOAT 1 2 3 4
```

* 引号包裹的字符串按文本处理（`ConstructInitialDataString`）。
* 支持的格式按元素宽度分派：32 位浮点/无符号/有符号、16 位与 8 位的 UNORM/SNORM/UINT/SINT，以及对应的多通道组合。**16 位浮点（`R16G16B16A16_FLOAT` 等）与 `R1_UNORM` 尚未支持**，遇到会输出 `Unsupported format <fmt> for specifying initial data` 警告。
* `data` 与 `filename` 不能同时使用，否则输出 `Initial data and filename cannot be used together` 警告并忽略 `data`。

### 3.5 颜色空间

`color_space` 的取值语义与 WIC/DDS 差异见 [Texture Color Space](ini-language-extensions/resources/loading.md) 与 [Resource Options](ini-language-extensions/resources/resource-options.md)。要点：

* `sRGB` → WIC 强制 sRGB 格式 / DDS 启用 force-sRGB。
* `Linear` → WIC 抑制 sRGB 元数据处理；**不会**把已经是 sRGB 的 DDS 格式改回线性。
* 省略时，若 `[System] force_detect_color_space = true`（默认 `false`），会在第一个 PNG IDAT 之前检查是否存在完整的 sRGB chunk；扫描不校验 CRC，也不从 iCCP 推断 sRGB。

---

## 4. 格式

### 4.1 格式名解析

`ParseFormatString()`（`src/common/util.h`）的规则：

* ① `allow_numeric_format` 为真时（`format`、`match_format`、`original_format` 均为真），**先把整串按十进制数值解析**：只有整串都是数字时才接受，命中即立刻返回该数值。
* ② 数值解析未命中时，剥离可选的 `DXGI_FORMAT_` 前缀（12 个字符，大小写不敏感）。
* ③ 再按不区分大小写的方式查内置的 `DXGIFormats` 名称表。
* 全部未命中则返回 `(DXGI_FORMAT)-1`，表示“解析失败”，**不是** `DXGI_FORMAT_UNKNOWN`。

因此 `format = R8G8B8A8_UNORM` 与 `format = 28` 等价，`format = 0` 是合法的 `DXGI_FORMAT_UNKNOWN`（结构化缓冲区常用），而 `format = NOT_A_FORMAT` 会被拒绝。

注意：**`DXGI_FORMAT_<数字>` 不是合法写法**。`format = DXGI_FORMAT_28` 会被拒绝——①的数值解析因整串不是数字而失败，②剥前缀后得到 `28`，③在名称表里查不到 `28`，最终返回 `(DXGI_FORMAT)-1`。数值格式只能写裸数字 `format = 28`。

表达式里也可以直接使用 `DXGI_FORMAT_*` 数值字面量，详见 [Expressions → Literals](ini-language-extensions/expressions/literals.md#dxgi_format-enum-literals)。

### 4.2 `format` 与 `match_format` / `original_format` 的分工

| 键 | 作用对象 | 含义 |
| --- | --- | --- |
| `format` | 自定义资源 | 创建/复制该资源时使用的 DXGI 格式；同时是 `->Format` 的最高优先级来源 |
| `original_format` / `match_format` | 自定义资源 | **参考元数据**：描述“被匹配的原始资源应该是什么格式”，不改变分配，也不改变当前视图格式 |

`->Format` 的取值优先级（`ResourceCopyTarget::GetResourceFormat`）：

1. 自定义资源的 `format` 覆盖值（`override_format`，既不是 `-1` 也不是 `UNKNOWN`）；
2. 自定义资源已缓存的 `format` 元数据；
3. 资源已知的格式；
4. typed 视图的格式；
5. 原生纹理的格式。

若最终仍为 `DXGI_FORMAT_UNKNOWN`，返回错误值 `UNKNOWN`（`-1`）。

`->OriginalFormat`（`->getmatchformat` 为别名）只读 `original_format` / `match_format`，默认 `-1`；非法名称也保持 `-1`；管线槽（`ps-t0` 等）返回 `-1`。

---

## 5. 复制与引用

### 5.1 赋值语法

资源赋值出现在命令列表（`[CommandList*]`）中，形式为：

```ini
<目标> = <选项...> <源>
```

目标是管线槽（`ps-t0`、`vs-cb0`、`vb0`、`ib0`、`o0`、`so0`、`u0`…）、自定义资源（`ResourceFoo`）、池（`PoolFoo`、`PoolFoo[0]`、`PoolFoo[0:3]`）或 `null`；源同理。

### 5.2 选项表

`ResourceCopyOptions`（`src/DirectX11/CommandList.h`）：

| 选项 | 别名 | 语义 |
| --- | --- | --- |
| `copy` | — | 复制数据到新的分配 |
| `ref` | `reference` | 引用源资源本身（不复制数据） |
| `copy_desc` | `copy_description` | 只复制**资源描述**，不复制数据 |
| `unless_null` | — | 源不存在时保持目标原值 |
| `no_view_cache` | — | 不保留缓存的视图，操作结束后释放 |
| `raw` | — | 创建 raw 视图 |
| `mono` | — | 单目（立体化相关） |
| `set_viewport` | — | 复制后按资源尺寸设置视口 |
| `resolve_msaa` | — | 解析 MSAA（依赖设备对目标格式的 `D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE` 支持，**无回退实现**） |

`copy`/`copy_desc`/`resolve_msaa` 归入 `COPY_MASK`（`0xc9`）；再加上 `ref` 构成 `COPY_TYPE_MASK`（`0xcb`），用于判定“是否隐含复制语义”。`COPY_MASK` 里还包含一个没有任何选项名可以设置的 `0x40` 位，对用户不可见。

### 5.3 `copy` / `copy_desc` / `ref` 的实际差异

源码路径（`ResourceCopyOperation::CopyResourceToResource`）：

* **`ref`**：目标直接持有源资源；视图在可用时沿用源的视图，否则按目标类型新建。数据不复制，因此后续对源的写入会反映到目标。带显式 `Region(...)` 的 `ref` 会把窗口发布给下游（例如常量缓冲窗口），但源本身不被裁剪。
* **`copy`**：创建与源兼容的新分配。
  * 没有选中窗口时执行整资源 `CopyResource`。
  * 选中了窗口（`->Region(...)`）时执行区域复制，目标分配**从偏移 0 开始**，内容是窗口所覆盖的可用前缀；窗口越过物理缓冲末尾时只复制可得部分，目标尾部字节未定义。
* **`copy_desc`**：只按源描述重建资源，**不复制任何数据**。目标的内容因此是未定义的（对于缓冲区是新建的零初始化存储）。

其他要点：

* 复制的常量缓冲存储从偏移 0 开始，并作为**完整缓冲区**绑定，即使尺寸小于 `SetConstantBuffers1` 的 256 字节粒度。只有**显式 `ref` 的 `Region`** 才会发布常量缓冲窗口。
* 引用一个没有指定 `Region` 的普通源到常量缓冲槽，会恢复整缓冲区绑定；需要窗口语义时必须显式写 `->Region(...)`。
* `no_view_cache` 会在操作结束时释放缓存视图；`src.forbid_view_cache` 也会阻止缓存。
* 每帧复制次数由 `[Resource*]` 的 `max_copies_per_frame` 限制，默认 `0`（不限制）。

### 5.4 失败路径

* 源的动态槽表达式非法 → 视为“源不存在”：普通赋值会**清空**目标，`unless_null` 则保留目标。
* 目标槽范围非法 → 该范围操作被**跳过**，目标绑定保持不变。这与上一种“源非法导致清空”不同。
* 范围赋值要求两端都写出显式边界，且包含的元素个数必须相同；`ps-t[3:5] = ref PoolSaved`（整池作为源）被拒绝。
* 池到池的 `copy` 不被支持，只能用 `ref` 与 `copy_desc`。
* 池范围始终选择**物理 Ring 包装后的槽位**，不执行键查找，也不改变键映射。

详见 [Dynamic Slots and Slot Ranges](ini-language-extensions/resources/slot-indexing.md) 与 [Whole-Pool Operations](ini-language-extensions/pools/pool-operations.md)。

---

## 6. 资源区域 `Region`

```ini
cs-cb0 = copy vs-cb0->Region($byte_offset, $byte_size)
cs-cb0 = ref  vs-cb0->Region($byte_offset, $byte_size)
```

* `$byte_offset` 是**底层缓冲的绝对字节偏移**，不是相对当前视图偏移。
* `$byte_size` 是区域字节数。两个参数都必须有限、在无符号 32 位范围内，偏移非负、尺寸至少为 1；小数先截断为整字节再做范围与对齐检查。
* 常量缓冲目标要求**偏移与尺寸都是 256 的倍数**，且尺寸不超过 65536 字节。尺寸为 0 会被拒绝，不会被当作整缓冲区绑定。
* 引用窗口可以超出物理分配（例如 1024 字节的源上 `Region(768, 512)` 是合法窗口）；此时越界读取返回 0。复制则受物理边界限制。
* 复制范围不要求 256 字节对齐，`Region(20, 32)` 对足够大的缓冲是合法的。

完整规则与工作示例见 [Resource Regions](ini-language-extensions/resources/resource-regions.md) 与 [Constant Buffer Region](ini-language-extensions/examples/constant-buffer-region.md)。

---

## 7. 布局

### 7.1 输入布局覆盖

输入布局通过 `->elementformat` 与 `->elementoffset` 覆盖，参数是（语义名，元素序号）：

```ini
[CommandListLayout]
vb0->elementformat("POSITION", 0) = R32G32B32_FLOAT
vb0->elementoffset("POSITION", 0) = 0
```

适用条件：目标必须是**固定的顶点缓冲槽**；动态槽表达式（`vb[$slot]`）不被支持。详见 [Input Layouts](ini-language-extensions/input-layouts/README.md)。

### 7.2 常量缓冲窗口

常量缓冲槽的可见区域由 `FirstConstant` / `NumConstants` 决定，字节语义下即“偏移 + 长度”，对齐与上限见 §6。`->Offset` 读取当前配置区域的绝对字节偏移：

* 常量缓冲：`FirstConstant` 换算为字节；
* 索引缓冲：绑定偏移 + 当前绘制的 first-index 贡献；
* 顶点缓冲：绑定偏移 + 当前绘制的 first-vertex 贡献。

在绘制命令列表之外，顶点/索引缓冲只报告绑定偏移，不含绘制贡献。

---

## 8. 流输出

四个流输出目标为 `so0`–`so3`，支持动态标量索引 `so[$slot]`，**不支持** SO 槽范围。

```ini
so0 = ref ResourceOutput
so0 = null
ResourceSaved = ref so0
so[$slot] = ref ResourceOutput->Region($byte_offset, $byte_count)
```

关键限制：

* 原生 SO API **只接受偏移，不接受尺寸**。`Region` 的字节数由命令解析器校验，但**不限制**着色器实际写出的记录量；记录可以继续写到后备缓冲容量允许的位置。
* 赋值未点名的 SO 槽会被以 append 偏移重新绑定，从上一次输出之后继续。D3D11 的 `SOGetTargets` 只返回缓冲区、不返回绑定偏移，因此这些槽的偏移无法查询也无法恢复；`ResourceSaved = ref so0` 会报告偏移 `0`，`so0->Region(...)` 从缓冲起点开始度量。
* SO 要求 `stream_output` 绑定标志；本地配置图会在分配前通过资源引用与池模板传播所需绑定标志，早于首次运行时使用。
* `CreateGeometryShaderWithStreamOutput` 创建的着色器**不注册**到常规哈希/绑定缓存，因此 hunting、`ShaderOverride` 与着色器替换对它不生效。

详见 [Stream Output](ini-language-extensions/resources/stream-output.md)。

---

## 9. 元数据访问

### 9.1 `@` 资源身份

`@` 返回底层 D3D11 资源的**指针哈希**（30 位浮点可表示的紧凑值），无底层资源时返回 `0`。

* 它是地址哈希而非内容哈希，**可能碰撞**，销毁后地址复用也会得到相同值。
* 它可用于低成本判断“资源是否存在”：`if @ResourceFoo`。
* 池整体用 `@PoolFoo` 时返回池对象自身的身份。

### 9.2 `->` 资源属性

可用的成员名（`ResourceCopyTarget::ParseTargetMember`，源码中的完整列表）：

`mips`、`size`、`array`、`index`、`width`、`offset`、`stride`、`format`、`height`、`region`、`bindflags`、`lastframe`、`hashregion`、`spatialhash`、`tocandidate`、`sourcestride`、`elementformat`、`elementoffset`、`originalformat`、`getmatchformat`、`hammingdistance`。

各属性的详细语义、返回单位与优先级见 [Resource Metadata](ini-language-extensions/resources/resource-metadata.md)；要点：

* `->Width` / `->Height` 返回**整个资源**的纹素尺寸，不按视图的 mip 或数组切片缩小；一维纹理的 height 为 `1`；缓冲区返回 `NOT_A_TEXTURE`（`-4`）；资源缺失返回 `-2`。
* `->Array` / `->Mips` 同样是整个资源的数量，不是视图窗口的数量。
* `->Format` 返回数值 DXGI 格式，优先级见 §4.2。
* `->Offset` / `->Size` 只对 VB / IB / CB 槽与自定义资源有明确意义，不要假设任意自定义资源的 `Offset` 都有意义。
* `->SourceStride` 只对**自定义**资源有意义（管线槽不支持）。
* `->HashRegion(offset, size)` 与 `->SpatialHash(x, y, z, cell_size)` 需要可用的 CPU 侧快照，见 §11.2。

### 9.3 `#` 池元数据

`#` 返回池元数据：解析后的索引（FIFO 下为该键当前占用的槽位）、池容量等。`PoolFoo->Size` 返回容量，`PoolFoo[$id]->Index` 返回解析后的物理槽位；池元素的 `->Size` 报告的是资源字节数，二者不同。详见 [Pool Metadata](ini-language-extensions/pools/pool-metadata.md)。

### 9.4 错误值

| 值 | 名称 | 含义 |
| --- | --- | --- |
| `-1.0f` | `UNKNOWN` | 所有数据来源都返回 0，或结果不可用 |
| `-2.0f` | `RESOURCE_NOT_FOUND` | 目标没有对应资源 |
| `-3.0f` | `NOT_A_BUFFER` | 目标不是 D3D11 缓冲区 |
| `-4.0f` | `NOT_A_TEXTURE` | 宽度/高度查询落在非纹理资源上 |

---

## 10. 感知哈希（dHash）

### 10.1 计算方式

dHash 是 64 位差分哈希，由内置计算着色器在 GPU 上生成（`src/DirectX11/DHash64Gpu.cpp`）：

* 以 `cs_5_0` 编译，`[numthreads(64,1,1)]`，1 个工作组。
* 采样 9×8 共 72 个点（`SampleLevel`），每点取 RGBA 四通道等权平均（各 0.25）。采样器为 `D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT`（min/mag 线性、mip 取点）、`MaxAnisotropy = 1`、`AddressU/V/W = D3D11_TEXTURE_ADDRESS_CLAMP`、`ComparisonFunc = D3D11_COMPARISON_ALWAYS`、`MaxLOD = 0`。
* 每行 9 个相邻采样点两两比较，得到 8 位，共 64 位比较结果；高 32 位与低 32 位分别写入两个 `uint`。
* 若 64 位全为 0（完全平坦的图），回退为把中心点 RGBA 各量化到 0–16 级并编码成 4 段 16 位“温度计码”，因此 **0 是一个合法的哈希值**。

### 10.2 适用条件与失败路径

`GetDhash()`（`src/DirectX11/ResourceHash.cpp`）要求全部满足：

* 上下文是**立即上下文**（`D3D11_DEVICE_CONTEXT_IMMEDIATE`），延迟上下文不支持。
* 资源与上下文属于**同一个设备**。
* 资源类型是 `TEXTURE2D`。
* `ArraySize == 1`、`SampleDesc.Count == 1`（非数组、非 MSAA）、`MipLevels > 0`。
* 被哈希的纹理必须带 `D3D11_BIND_SHADER_RESOURCE` 绑定标志；只作 RTV / UAV 用途的纹理无法计算 dHash——`DHash64Gpu::ComputeHash` 在这一条件下直接返回 `E_INVALIDARG`，表达式层表现为 `UNKNOWN`（`-1`）。
* 该纹理必须被**纹理上传跟踪路径**记录，且跟踪到的宽高 / mip 数 / 数组大小与当前描述符一致；至少有一个 mip 的上传代次（generation）有效。跟踪路径之外创建的原生纹理即使有像素也无法计算。
* 若提供了视图，采样格式取自该视图（支持 typeless 资源的 typed 解释与 sRGB 视图），且视图必须属于同一资源；通用 SRV / RTV / UAV / DSV 接口会被安全地识别，但**并不表示所有深度或整数格式都可采样**。
* 采样格式必须支持 `D3D11_FORMAT_SUPPORT_SHADER_SAMPLE` 与 `D3D11_FORMAT_SUPPORT_TEXTURE2D`。
* 计算本身还要求设备特性级别 ≥ `D3D_FEATURE_LEVEL_11_0`（`DHash64Gpu::Initialize` 否则返回 `E_NOTIMPL`）。

任一条件不满足即返回“不可用”，表达式层表现为 `UNKNOWN`（`-1`）。

### 10.3 mip 选择与缓存

```ini
[Rendering]
dhash_mip_size = 256
dhash_allow_rehash = 0
```

以上为默认值，配置重载时会重新读取。

* `dhash_mip_size` 为正数时，从小到大搜索已初始化的 mip，要求两侧至少该尺寸；mip 0 始终作为尺寸兜底。`0` 表示从大到小搜索。搜索使用**整个物理纹理的 mip 链**，不受视图显示窗口限制。
* 缓存键包含采样格式、首选 mip 策略、跟踪的资源生命周期与上传代次，因此同一纹理的线性视图与 sRGB 视图不会互相复用对方的解释。
* `dhash_allow_rehash = 0`（默认）：同一采样格式与 mip 策略下的已有哈希在后续上传后**保留**，改动后的首次查询也返回保留值。这适合“识别一张已知纹理”，不代表结果描述当前像素。
* `dhash_allow_rehash = 1`：发生变化的纹理会用最新上传代次中符合条件的 mip 重新计算；若该代次没有符合条件的已初始化 mip，则结果不可用。计算期间若发生上传或资源状态重置，该结果不会被作为新的缓存项发布，之后可以重试。

### 10.4 `HammingDistance`

`HammingDistance(a, b)` 返回 0–64 的整数差异位数：

* 两个零哈希 → 距离 `0`；
* 恰好一个为零 → 距离 `64`；
* 否则为异或后置 1 的位数。

**距离小只说明“不同的位少”，不保证两图相同。**

表达式层：`TextureTarget->HammingDistance(ResourceReference)`。参数必须是**资源引用**（自定义资源或带相应元数据的池元素），不是内联十六进制或数值表达式；结果为 `0`–`64`，不可用时为 `-1`，目标资源或所需运行时设备/上下文缺失时为 `-2`。

```ini
[ResourceReferenceImage]
original_format = R8G8B8A8_UNORM
original_dhash = 0123456789abcdef

[CommandListCheckReferenceTexture]
$reference_matches = 0
$reference_distance = ps-t0->HammingDistance(ResourceReferenceImage)
if $reference_distance >= 0
    if $reference_distance <= 8
        $reference_matches = 1
    endif
endif
```

上例是**片段**：它假定 `ps-t0` 已经是可采样的、已跟踪且已初始化的 Texture2D，且调用点选对了钩子。`original_format` 只是参考元数据，不会转换 `ps-t0` 或强制其采样格式。完整规则见 [Perceptual Texture Hashes](ini-language-extensions/resources/perceptual-hash.md)。

### 10.5 参考元数据驱动的自动匹配

除了显式的 `HammingDistance` 表达式，还存在一条“用参考元数据自动挑选被替换槽位”的路径（`TextureOverrideOperation`，`src/DirectX11/TextureMatch.cpp`）：

* 前提：参考资源必须声明了 `match_dhash`（或 `original_dhash`）、格式有效、`match_confidence` 有限。
* 阈值：`limit = (1 - match_confidence) * 64`。默认 `match_confidence = 0.75` 对应阈值 `16`。
* 在像素着色器声明的 SRV 槽范围内逐槽计算 dHash；**参考格式已声明且与该槽视图格式不同**的槽会被跳过。
* 取距离最小的槽执行替换；同一距离的多个槽都可被替换。
* 需要立即上下文与像素着色器的原始绑定快照；配置重载会使旧快照失效。

---

## 11. GPU readback 与 CPU 快照

### 11.1 `[Rendering] cache_resource_data`

```ini
[Rendering]
cache_resource_data = constant_buffer
```

可取 `none`、`all`（受支持的 VB / IB / CB 缓冲区）或空格分隔的 `vertex_buffer`、`index_buffer`、`constant_buffer` 组合。默认 `none`。`track_region_hashes = 1` 会**追加**启用全部三种类型——它是并集而非覆盖，`cache_resource_data` 已有的选择不会被它清除。

适用条件与失败路径：

* 该选项只捕获**受支持的 Map/Unmap 写入**。`UpdateSubresource` 与 GPU 写入会使缓存数据**失效**，而不是保证立即刷新 CPU 快照；之后的缓存缺失可能需要同步 GPU 回读。
* 只选择模式并不代表数据新鲜，必须验证实际的写入路径。
* 自定义资源的哈希使用通过受支持的 ref/copy 路径捕获的跟踪数据；仅仅打开该选项**不会**让任意文件型自定义资源获得快照。
* 该选项在配置重载时读取，并调用 `InvalidateResourceDataCaches()` 丢弃旧快照。

### 11.2 区域哈希与空间哈希

* `->HashRegion(byte_offset, byte_size)`：用 CRC32C 对缓冲区字节求哈希，返回 30 位紧凑浮点编码。偏移是**相对目标当前区域**的字节偏移（与 `Region` 的绝对偏移不同）；参数必须有限，偏移非负、尺寸为正；超出快照末尾的尺寸会被截断为剩余字节。缺失资源返回 `-2`，非缓冲区返回 `-3`，哈希数据不可用或非法返回 `-1`。**哈希可能碰撞，相等不证明内容相等。**
* `->SpatialHash(x, y, z, cell_size)`：在当前区域内按 **float 元素索引**读取三个 float32 坐标，用有限的正 `cell_size` 量化。连续 float 使用索引 `0, 1, 2`（内部各换算为 4 字节），区域的字节基址只加一次。打包后的空间键在 4096 × 256 × 4096 的网格上回绕，不同位置可能共享同一个键；打包后的零格是合法值。

两者都要求存在可用的 CPU 侧缓冲区跟踪/缓存数据，因此需要先配置 §11.1。

### 11.3 dHash 的 GPU 路径与回读

`DHash64Gpu::ComputeHash()` 的执行序列：

1. 把选中的 mip **复制到私有纹理**（压缩格式会向上对齐到块尺寸，把 mip 放到对齐后的私有顶层之下），避免直接绑定应用纹理而意外解绑 CS UAV 或 OM 输出槽。
2. `CSSetShader` / `CSSetShaderResources` / `CSSetSamplers` / `CSSetUnorderedAccessViews`，`Dispatch(1,1,1)`。
3. 把 8 字节的输出 UAV 缓冲 `CopyResource` 到 staging 缓冲。
4. **同步 `Map(D3D11_MAP_READ)`** 读回两个 `uint`，组装成 64 位哈希，随后 `Unmap`。
5. 离开作用域时恢复原来的 CS / SRV / Sampler / UAV 绑定（对 append/consume UAV 使用 `nullptr` 保留其隐藏计数器）。

因此：**缓存缺失会触发 GPU 工作与同步 CPU 回读**。请按 Mod 的实际需要控制查询频率；本文不给出任何帧率或耗时结论。

---

## 12. 池索引与生命周期

### 12.1 索引类型

`pool_index_type` 取 `ring`（默认）、`static`、`fifo`、`spatial`，详见 [Pool Indexing](ini-language-extensions/pools/indexing.md)。

| 类型 | 索引含义 | 关键行为 |
| --- | --- | --- |
| `ring` | 直接下标 | 负数从尾部回绕，超出容量自动取模；连续循环访问无需手写取模 |
| `static` | 解析期固定 | 索引必须是数字字面量；`$PoolFixed[$index]` 被拒绝；直接解析的元素绕过运行期查找/过期 |
| `fifo` | 用户自定义键（UID） | 同一 UID 总是解析到同一元素，直到其槽位被回收；池满时淘汰最旧的 UID |
| `spatial` | 打包空间键 | 网格每轴回绕（X/Z 4096，Y 256），默认查找半径 1 格；命中邻近格时按请求的键跟随映射 |

* FIFO / spatial 的**缺失读取**返回**模板资源**，不分配槽位、不触发淘汰。`pool_allocate_slot_on_missing = 1` 可恢复“读取即分配”的旧行为。赋值总是解析到实际元素，绝不会写入共享的缺失模板。
* `spatial` 的半径以 120 FPS 为参考，会按 `FRAME_TIME * 120` 向上取整缩放（半径 2 在 60/120/240 FPS 下分别为 4/2/1 格）。两个边界：半径上限为 **2048 格**（环面网格的最大切比雪夫距离，超出即按 2048 截断）；帧时间非有限或不大于 0 时有效半径退化为 **0**，即只命中本格。
* 池范围（`PoolFoo[0:3]`）始终选择**物理 Ring 包装后的槽位**，不执行键查找、不改变键映射，跨度不能超过容量。

### 12.2 生命周期选项

| 选项 | 默认 | 含义 |
| --- | --- | --- |
| `pool_size` | `1` | 物理容量；小于 1 不创建池；值可以写成表达式（如 `pool_size = 1+2`） |
| `pool_lazy_initialization` | `1` | 按需创建元素对象 |
| `pool_element_type_switch_reset` | `1` | 资源/变量类型切换时重置旧类型状态 |
| `pool_expiration_timeout_frames` | `-1` | 任意负值表示不过期；非负值表示最后更新早于该帧数时过期 |
| `pool_expiration_reset_elements` | `1` | 过期时同时重置已存内容 |
| `pool_expiration_refresh_on_read` | `0` | 读取已有元素时也刷新时间戳 |
| `pool_allocate_slot_on_missing` | `0` | 缺失键读取时分配槽位而不是返回模板 |
| `pool_spatial_radius` | `1` | spatial 最近格搜索半径；小于 1 会警告并夹到 1 |
| `pool_variable_default_value` | `0` | 变量初始/重置值 |
| `pool_persist_variables` | `0` | 持久化 Ring 索引的变量槽位 |

旧名 `pool_keep_alive_frames` 与 `pool_reset_expired_elements` 仍被接受；两者同时出现时**新名优先**。

其他要点：

* 过期检查在访问时进行，**每帧至多一次**。
* `pool_persist_variables` 仅对 `ring` 索引类型有效，其他类型会输出警告并被禁用。
* 过期/复用的详细说明见 [Declaring Pools](ini-language-extensions/pools/declaration.md) 与 [Pool Metadata](ini-language-extensions/pools/pool-metadata.md)。

### 12.3 池的模板与绑定标志传播

* 池的资源元素使用一份**模板资源**承载配置与初始数据；`[Pool*]` 段里的 `[Resource]` 类选项会覆盖模板默认值。模板同时是绑定标志传播的终点。
* 引用图上的绑定标志传播是**延迟**的：解析期先记录边，随后由 `PropagateDeferredBindFlags()` 反复迭代直到不再变化，因此 `ResourceA = ref ResourceB` 与 `ps-t0 = ref ResourceA` 无论书写顺序如何都能把标志传到位。
* 池到池的 `ref` 是**代理**：目标在绑定时解析源的根。绑定到已代理的源会捕获当时的根，之后中间代理改变不会重新指向；循环引用会被拒绝且不改变原有绑定。

---

## 13. `TextureOverride` 与资源哈希

### 13.1 资源哈希的构成

DX11 为每个被跟踪的原生资源计算 32 位哈希（CRC32C）：

* 二维纹理：`CalcTexture2DDataHash()` 对初始数据求 CRC，再 `CalcTexture2DDescHash()` 对整个 `D3D11_TEXTURE2D_DESC` 求 CRC。
* `[Rendering] texture_hash`（默认 `0`）：非零时改用 `CalcTexture2DDataHashAccurate()`，覆盖**全部** mip 与数组切片，而不是仅第一个子资源。
* 描述符哈希计算前会做**常量分辨率替换**：当纹理的宽高与 `[Device] get_resolution_from` 确定的分辨率成 1×/2×/4×/8× 关系时，宽高被替换为 `SRES`/`SR*2`/`SR*4`/`SR*8`/`SR/2` 标记，使不同分辨率下的同一资源得到相同哈希。
* `orig_hash` 在资源注册时写入且**此后不再变化**；`hash` 会随数据更新而变化。
* 未被哈希的资源（3DMigoto 自己创建的、交换链后备缓冲等）`GetResourceHash()` 返回 `0`，便于识别“这是一个未哈希资源”。
* 哈希污染（copy / update / map 造成的内容变化）只对 **TEXTURE2D 与 TEXTURE3D** 跟踪（`supports_hash_tracking`）。

### 13.2 描述符覆盖

`[TextureOverride*]` 的以下键在**资源创建时**生效（`HackerDevice.cpp` 的 `process_texture_override` / `override_resource_desc`）：

| 键 | 默认 | 作用 |
| --- | --- | --- |
| `Format` | `-1` | 覆盖 `DXGI_FORMAT`（数值）；负值 → `Invalid resource override` 警告 |
| `Width` / `Height` | `-1` | 覆盖纹素尺寸（数值） |
| `width_multiply` / `height_multiply` | `1.0` | 按倍数缩放；非有限或溢出 → 警告并放弃该次覆盖 |
| `override_vertex_count` | `-1` | 顶点上限提升；换算为 `override_byte_width` 与元素数 |
| `override_byte_stride` | `-1` | `override_vertex_count` 换算所需；≤0 → 警告并放弃 |
| `uav_byte_stride` | `-1` | 覆盖 UAV 元素数换算 |

限制：

* 上述覆盖**只对二维与三维纹理生效**（`override_resource_desc` 对 `D3D11_TEXTURE1D_DESC` 直接返回 true，不做任何覆盖）。
* 缓冲区只支持 `override_byte_width` 的**扩容**（`desc->ByteWidth < override_byte_width` 时才扩容）。
* 顶点上限提升功能受 `[Rendering] allow_buffer_resize` 控制（默认 `true`）；关闭时相关键被忽略。
* 段名包含 `VertexLimitRaise` 时，在 `allow_buffer_resize` 打开的前提下回落到约 8.8 MB（8800000 字节）的缓冲。
* 覆盖生效后若资源创建失败，日志会输出 `Resource creation failed with matching TextureOverride` 并附带描述符。

### 13.3 迭代与执行顺序

* `Iteration` 接受逗号分隔的整数列表（解析上限 10 个值；遇到非正数即停止解析，其后的值被丢弃）。列表的**第 1 项是运行期计数器**：解析期写入 0，之后每次创建资源时自增 1，**自身不参与匹配**；第 2..N 项才是需要命中的迭代号。命中则执行覆盖，否则输出 `override skipped` 并跳过。因此 `Iteration = 1, 3, 5` 表示第 1、3、5 次创建该资源时应用覆盖。
* 同一哈希的多个 `TextureOverride` 会按 `match_priority` 优先、其次段名排序，保证多次运行结果一致；相同哈希且没有绘制上下文匹配或 `match_priority` 时会给出重复警告。
* `expand_region_copy = 1` 会把 `CopySubresourceRegion` 的源盒子扩展到目标整幅宽度（针对 CryEngine 类游戏的透明折射效果）。它按**目标**资源的哈希查找覆盖项。
* `deny_cpu_read = 1` 会拦截该资源**第 0 个子资源**的 CPU 读映射（只处理第一个子资源，且只针对读访问）。它按资源的当前哈希查找覆盖项。

### 13.4 模糊匹配键

不带 `Hash=` 的 `[TextureOverride*]` 段可以用模糊匹配键，键集包括：

`match_type`、`match_usage`、`match_bind_flags`、`match_cpu_access_flags`、`match_misc_flags`、`match_format`、`match_byte_width`、`match_stride`、`match_mips`、`match_width`、`match_height`、`match_depth`、`match_array`、`match_msaa`、`match_msaa_quality`。

* `match_type` 取 `D3D11_RESOURCE_DIMENSION_*`（`UNKNOWN`/`BUFFER`/`TEXTURE1D`/`TEXTURE2D`/`TEXTURE3D`）。
* `match_usage` 默认 `D3D11_USAGE_DEFAULT`；显式指定其他值才改变。
* 数值类键支持关系表达式（如 `>= 512`、`== 1024`）。
* **不能同时使用 `Hash=` 与模糊匹配键**：同时出现会输出 `Cannot use hash= and match options together!` 警告。
* 若匹配条件不可能命中任何资源（例如类型互斥），会输出 `Section can never match any resources` 警告并丢弃该段。

绘制上下文匹配键（与哈希/模糊匹配正交）：`match_first_vertex`、`match_first_index`、`match_first_instance`、`match_vertex_count`、`match_index_count`、`match_instance_count`，以及 `match_priority`。

---

## 14. API 限制与已知边界

* **槽位数量**遵循 D3D11 的普通上限：着色器资源槽 `D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT`（128）、常量缓冲槽 `D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT`（64）、像素/计算 UAV 槽 `D3D11_PS_CS_UAV_REGISTER_COUNT`（8，`D3D11_1` 设备上为 `D3D11_1_UAV_SLOT_COUNT` = 64）。
* **常量缓冲窗口**：偏移与尺寸需 256 字节对齐，尺寸 ≤ 65536 字节（`SetConstantBuffers1` 的粒度与上限）。
* **流输出**：原生 API 只接受偏移；未点名的槽以 append 方式重新绑定，其偏移不可查询。
* **感知哈希**：仅 Texture2D、非数组、非 MSAA、立即上下文、同设备、已被上传跟踪路径记录、采样格式支持 `SHADER_SAMPLE`。
* **资源区域复制**：目标从偏移 0 开始，越界尾部未定义；引用窗口可以越过物理末尾（越界读取返回 0）。
* **`resolve_msaa`** 依赖设备对目标格式的 `D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE`，**没有回退实现**，源码明确标注“不要鼓励使用、不要写进文档”。
* **`copy_desc` 不复制数据**，目标内容未定义。
* **引用不复制数据**，源后续写入会反映到目标。
* **哈希不是唯一标识**：`@` 身份与各类哈希都可能碰撞，不能当作对象 ID。
* **`GetResourceHash()` 返回 0 表示“未哈希”**，不表示“内容为空”。
* 本文未验证任何目标游戏的实际行为；所有 INI 片段均未作为完整配置运行。

---

## 15. 相关文档

* [Extended INI Language](ini-language-extensions/README.md) — 总入口
* [Resources](ini-language-extensions/resources/README.md) — 资源扩展主题索引
  * [Resource Metadata](ini-language-extensions/resources/resource-metadata.md) — `@` / `->` / `#` 完整属性表
  * [Resource Regions](ini-language-extensions/resources/resource-regions.md) — 显式区域与 CB 窗口
  * [Perceptual Texture Hashes](ini-language-extensions/resources/perceptual-hash.md) — dHash 细节
  * [Stream Output](ini-language-extensions/resources/stream-output.md) — SO 目标与限制
  * [Dynamic Slots and Slot Ranges](ini-language-extensions/resources/slot-indexing.md) — 动态槽与范围
  * [Shader Declarations](ini-language-extensions/resources/shader-declarations.md) — 原始着色器声明
  * [Resource Options](ini-language-extensions/resources/resource-options.md) / [Texture Color Space](ini-language-extensions/resources/loading.md) — 颜色空间
* [Pools](ini-language-extensions/pools/README.md) — 池总览
  * [Declaring Pools](ini-language-extensions/pools/declaration.md) — 全部池选项与默认值
  * [Pool Indexing](ini-language-extensions/pools/indexing.md) — 索引类型
  * [Whole-Pool Operations](ini-language-extensions/pools/pool-operations.md) — 整池引用/配置/清空
  * [Pool Metadata](ini-language-extensions/pools/pool-metadata.md) — `#` 与池成员
  * [Pool Resources](ini-language-extensions/pools/resources.md) — 模板资源
* [Input Layouts](ini-language-extensions/input-layouts/README.md) — 输入布局覆盖
* [Examples](ini-language-extensions/examples/README.md) — 集成示例
  * [Constant Buffer Region](ini-language-extensions/examples/constant-buffer-region.md)
  * [GPU Readback](ini-language-extensions/examples/gpu-readback.md)
  * [Pool-Based Tracker](ini-language-extensions/examples/pool-based-tracker.md)
  * [Per-Object State](ini-language-extensions/examples/per-object-state.md)
* [Expressions → Literals](ini-language-extensions/expressions/literals.md) — `DXGI_FORMAT_*` 与 `D3D11_BIND_*` 字面量
* [DX11 Resource Properties Review](DX11-Resource-Properties-Review.md) — 属性 getter 优先级的代码级复核
* [DX11 Range Documentation Review](DX11-Range-Documentation-Review.md) — 范围与池语义的代码级复核
* [DX11 Upstream Sync](DX11-Upstream-Sync.md) — 上游同步与验证记录
