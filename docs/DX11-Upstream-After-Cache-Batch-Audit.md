# DX11 上游缓存/批处理之后 25 项审查

范围：账本拓扑顺序 `48f3b6db03814bac65b0ac990e0a26446c3bd73f` 至 `ac9947bf122137f9f6aba56c25c0f200c4b149ef`，共 25 项。此文件只读审查普通提交完整 patch，并检查 5 个 merge 的 combined/remerge diff；没有修改生产/测试/账本，没有构建、运行回归或测量性能。以下测试均是后续验收要求，不能记作 PASS。

材料在 `build/upstream/audit-after-cache-batch/`：逐项 `.diff`、merge `.cc`/`.remerge`、账本切片 `slice.json`、最终 `ac9947bf:DirectX11/CommandList.cpp` 快照。SHA/父关系来自本地上游 clone 的 git 对象；本地代码事实是此次工作区快照，后续集成可能使行号移动。

## 先处理的依赖及确定缺陷

1. **无 handle 的资源不再匹配是有意语义。** `c1378828` 明确删除 uncached hash/descriptor fallback，包括 region 路径调用的 fuzzy-only 入口。当前本地 `ResourceHash.cpp` 的 `find_fuzzy_texture_overrides_for_resource` 和 `find_texture_overrides_for_resource` 仍分别回退 descriptor 或 hash+descriptor；这是下一步待同步差异，不能用“保持兼容”保留。custom/native 创建的资源、backbuffer、OpenSharedResource 在没有 `G->mResources` handle 时应不追加 TO matches，已有 matches 的调用方容器无需清空。保留本地 ComPtr 活期、G→Resources 锁序、动态分辨率 matcher、hash=0 的 cache-valid 标志，以及 region-hash 独立筛选；只删上游点名的旧入口，不能误删 `find_texture_overrides_for_resource_by_hash_from_fuzzy_matches` 这个 region fallback helper。
2. **动态槽位必须和 batch guard 同时接入。** `a4f0fe51` 新增 `slot_expression`；`84c4948f` 才排除该目标被当作默认 slot=0 的静态 batch。本地 `BatchableSlotOperation` 已排除动态 pool index，但目前尚无动态 slot 字段；新增后在 slot 一侧也加 barrier。即使 `ps-t[1+1]` 看似常量，按最终上游 guard，带 slot_expression 的目标仍不能加入旧静态 batch；range 本身另有批量路径。不能通过求随机快照来决定是否可批处理。
3. **上游浮点→整数转换有 UB，不能照搬。** 最终快照 `ResolveSlot` 9467 行仅检查 `<0 || >=max`，NaN 可绕过并转 unsigned。`ResolveRange` 9485 行对 pool 仅要求 `end>=start && end-start<limit`，`Pool[1e20:1e20]` 可满足条件然后 `(int)start/end` 越界；`pool_first+i` 还可能有 signed overflow。先检 finite，计算截断后的 count/包裹索引时用可表示的宽类型和 fmod，转换前验证；保留普通合法负索引、溢出环绕、含端点计数，以及 slot 不环绕语义，不借机把所有大索引禁用来绕过实现。本地 `CustomResourcePool::GetElementIndex` 已采用 finite/trunc/fmod 安全包装，可作为语义参照。
4. **最终 range 数组越界是真实源码缺陷。** 快照 `RunBind` 13066 / `RunFetch` 13200：views[128]，buffers/cb_offsets/cb_sizes[14]，但 SRV count 可 128、UAV 可 64。多个 null/slow-copy 分支和最终 cleanup 对所有类型无条件读写 `buffers[i]`，null 分支还写 cb 数组，SRV 15+ 项即可越界。使用按类型正确限定的访问，或所有对应 owning arrays 使用最大容量；真实测试至少覆盖 SRV `[0:127]`、UAV 支持范围及 count=15/16、null/失败/slow-copy 路径。
5. **range fast-ref 新建 buffer view 会丢 metadata。** `RunBind` 调 `element.GetResource(..., NULL, NULL, NULL, NULL)`；`ViewForSlot` 12981 行用 stride/offset/format/size=0/0/UNKNOWN/0 调 CreateCompatibleView。当前本地 FillOutBufferDesc 的 raw NumElements 会算成 0，普通 structured/typed buffer 可能只给 1 element 或 UNKNOWN 格式而失败；已有原 SRV 时会掩盖。须传真实 source metadata 并补 FillInMissingInfo。新 range view 缓存不能只按 resource pointer 命中：同物理资源换 format/offset/size/options 时要失效，且保留源 SRV/UAV 子视图、NO_VIEW_CACHE、禁缓存 getter 的语义。
6. **range slow path 必须接本地 per-call sink。** 上游 `slot_ops[index]` 是跨 run 的 mutable operation，源码写 `op->deferred=&stack_binding`、改 op src/dst/slot，且 raw COM arrays 和 extra_indent +=/-= 无异常保护。本地刚移植的 DeferredBinding 已是 ComPtr+参数、没有 operation 栈指针；新 range 路径不能退回上游成员指针。source target 用 per-run 局部值或明确同步缓存，避免多个 context 并发改同 operation；COM/日志缩进都用 RAII。bind 计算中途抛出要保留先前成功 slot 的顺序发布和失败前原绑定；fetch 要释放已预取的剩余 refs。
7. **CB 窗口修复必须使用最终组合。** `190400dc` 保留 unless_null 跳过槽的原 byte offset/count；`b6939928` 将 mixed whole+window range 一次 SetConstantBuffers1 发布，whole 使用 first=0/count=4096，而非按 ByteWidth 夹小。它不撤销本地 Region：ref 的 offset/size 仍须 256B 粒度且 count<=64KiB，合法窗口可越物理 allocation；copy 仍受物理范围限制且不强加同粒度，复制后的 CB 绑定 whole，不凭 buf_dst_size 误造小 window。未知资源维度不能不经检查 reinterpret 成 ID3D11Buffer。
8. **physical pool range 与 unless_null 早退是功能要求。** `c45db630` 对所有 pool 类型用 `GetResource(..., use_ring_index=true, ...)`，FIFO/SPATIAL 绕过 key lookup，不能触发 missing-template/UID 分配；已有 keys 不变。`91d4aae5` 必须在取 assignment element 前检查 null，否则会延长过期、lazy-init、切换 element type 或污染 dirty 状态，即便最终没写值。

推荐实现分组：A=cache 注释+no-handle 语义；B=动态 slot+guard；C=完整 range 生产组（b779/8fe/cf8/190/c45/91/b693/99/ac，逐项记账但按最终可编译语义一起验证）；D=最终文档树。共享 CommandList.h 的枚举、ParseTarget/reader 参数与 batch helpers 应由一人顺序合并。

## 逐提交证据、适配和必要验证

| # / SHA | 完整 diff 的实际内容及文件 | 依赖、适配边界及可验证场景 |
| --- | --- | --- |
| 1 `48f3b6db03814bac65b0ac990e0a26446c3bd73f` | CommandList.cpp/ResourceHash.cpp 仅注释：region hash 自行 exact 匹配，只消费按 resource desc 缓存的 fuzzy 候选；另解释未 tracked 的 custom/backbuffer/shared 走 uncached fallback。 | 前半可补说明，后半被 c137 撤销，最终不能落过期 fallback 注释。当前本地 FindTextureOverrides 仍按 region hash 独立筛选、fuzzy-only 查候选，行为已等价第一部分。无需为纯注释添测试，但 cache 组要验证一个 buffer 两 region 对应不同 exact TO，fuzzy 候选不混入 whole-hash exact matches。 |
| 2 `fbd5f9d48c0d8066b9412102c93203050eaffe79` | cache 分支合入 batch 主线；first-parent stat 有 Cmd/header/Ini/Hash/profiling，combined 和 remerge patch 均空。 | 不重复移植父提交批处理、flags/profiling；只核对最终缓存与本地 owning sink、原序 batch 共存。merge 独有解决为空有 git 证据。 |
| 3 `c1378828238f165cec8efbaf78cab985373e7bbd` | ResourceHash.cpp/h 删除 by_hash/desc uncached public functions、两 cache lookup 的无候选 fallback，改注释为无 handle 不匹配；保留 draw-info filter 和 profiler lookup 完成。 | 按上文第1项移植；当前本地明确尚未等价。修改 cache 回归里 untracked fuzzy 的旧期望，验证 native untracked/自定义 copy/backbuffer 类资源不匹配、tracked exact+fuzzy 仍追加且顺序一致、region fuzzy 对无 handle 也不匹配、reload/分辨率/hash 更新/并发锁不退化。没有真实 backbuffer fixture 就明确待测，不用 native texture 假称交换链已测。 |
| 4 `e414c5feb37c2bc9457ebea99ba08fbef1da4a5d` | PR47 将 cache 最终分支合入主线；combined/remerge 空。 | 承接 fb2/76a/034/48/c137，禁止用父分支 ResourceHash 覆盖本地安全 readback/region/hash-grid/const find_ptr；没有独有解决可独立移植。 |
| 5 `a4f0fe512e0e3df45f5421168bb78425a70de8b7` | CommandList.cpp/h 新动态 bracket slot parser、slot_expression/max_slot/ResolveSlot，Get/SetResource 在 switch 前求槽；layout operation 拒绝 dynamic VB。docs resources index+新 slot-indexing.md。支持 t/u/cb/vb/o/so，UAV 仅 p/c。 | 新增 SlotTypeInfo 必须移到函数外；本地 target 可复制且 pool expression 是 shared_ptr，不能直接放 unique_ptr 破坏 CopyResourceToPool 的 `ResourceCopyTarget destination=dst`；新 expression 按本地共享不可变树约定适配。保留 namespace/UTF/tail-hyphen/->getter/strict binary&DXGI literals。与84 guard同组，finite检查。真实 WARP 每类首末槽、fraction truncate、-0.1/max/NaN/inf拒绝不触API、同一命令下一run改index、Get/Set/TO/dump/metadata/表达式入口均覆盖；dynamic vb->ElementFormat 拒绝。 |
| 6 `1011b2183406868e5467b5641a864390ba2f70c3` | PR53 merge 动态 slot，Cmd/header/docs来自另一父；combined/remerge 空。 | 没有额外生产解决，不重复恢复旧 cache fallback。逐父匹配上项成果。 |
| 7 `b779e7e2ec9a5519ef2b00be0f89ab94aee015fa` | Cmd.cpp/h 新 POOL_RANGE/SLOT_RANGE、range_start/end、ParseTarget allow_range/IsRange/ResolveRange/RangeLimit；reader GetTarget 允许范围选项；parse_slot_range_operation；SRV/UAV/CB Get/SetSlotRange；SlotRangeCopyOperation fast-ref；dump 按显式 slot range 逐slot加文件名。 | 不是小 parser 补丁，依赖动态slot，并必须结合后续range修复。当前本地没有等价类/范围字段。枚举不能照搬位：本地 RESOURCE_FORMAT/WIDTH/HEIGHT/ARRAY/MIPS/BIND_FLAGS 已占0x00020000..0x00400000，POOL_FULL_RANGE_VARIABLE=0x00800000，旧layout/variable位也不同，选空位同时更新 masks/name表。shared_ptr表达式/文件级struct/C++17。真实 bind/fetch六stage SRV/CB及p/c UAV；explicit双边相同/不同长度、任一侧bare继承、负pool wrap、pool跨末端、空pool、非法slot和不支持VB/SO/RT range拒绝；dump显式范围各文件不同，pool/bare dump拒绝。见确定数组/metadata/数值缺陷。 |
| 8 `595de8fadcc76c47445f4ccb5b86cd7fc25a5d31` | docs README、pools/resources.md、resources index/slot-indexing：补slot/pool范围、bounds继承、最初仅ref、选项/dump范围限制。 | 生产无改动；文档应取本批最终形态，不能保“only ref/no copy”。性能表述降为实际合并API调用，无测量不写显著速度收益；链接去 `md/#` 尾斜杠并跑现有checker。示例待实际parser+WARP验证。 |
| 9 `8fe6aac1b2e93fab004cc85931dae63bc7c2768f` | Cmd.cpp/h range所有copy选项：新增每slot ResourceCopyOperation缓存/SlotOp/UsesSlotOps；bind默认ref/fetch默认copy；slowpath deferred结果；CB传offset/size；docs同步。 | 必须承接真实copy/ref/copy_desc/raw/mono/resolve/no_view_cache/SET_VIEWPORT，不因难测整体关闭；overwrite当前未实现仍reject。移除上游op->deferred栈指针，用本地参数；新增offset/size若sink要给CB消费可扩充per-call数据，保留已有destination参数。viewport每slot执行顺序；single custom/dynamic-index source广播按上游在range入口求一次，不能偷偷每slot抽random。测试所有选项同scalar最终身份/bytes/元数据/viewport，失败保原state，动态bounds二次run和多context不共享栈sink。 |
| 10 `cf8ba704a5b295601ff40ad6caa5348e468dbcd8` | Cmd范围日志：按G->analyse_frame才格式化每slot名称；各ref/copy/null/skip线及copy详情缩进；移掉泛range日志；docs新Frame Analysis Log段。 | 本地日志等级及真实view/metadata须保留。上游extra_indent裸+=/-=遇throw不平衡，改scope guard；调用中的wstring保持.c_str()。真实frameanalysis fixture检查每slot唯一名称、null保留原因和copy嵌套，不把仅源码宏检查当验证；关闭analyse_frame时不造每slot字符串。 |
| 11 `407920fb27e023cf2ab067f5759d1f7bff49438a` | 仅.github/workflows/zip-release.yml：checkout v2→v4+fetch-depth0，setup-msbuild挪后，msbuild多行。 | 本仓库不存在.github，使用CMake/项目脚本，功能无关可skip，不能新造上游MSBuild release工作流。之前版本生成已由CMake适配，checkout full history需求在真正CI存在时另行处理。 |
| 12 `84c4948fca067caf3b0dbd0bb51de5fdf457be7d` | Cmd is_batchable_bind/fetch 各加 !slot_expression。 | 依赖a4与旧batch；本地应加进统一BatchableSlotOperation而非复制上游sorted batch。真实混合固定/动态相邻ops：动态前后安全run仍batch，dynamic read到之前写入pipeline且static slot0未误改；dynamic fetch实际选不同槽。 |
| 13 `4a20acd20d0b4733dc5e0a61061b4880be2e6306` | PR54 merge：first-parent只2行batch guard；combined/remerge空。 | 顺序父为407+84；CI skip不妨碍生产guard移植，不把整个merge当无关CI跳过。 |
| 14 `ef24b4316629a9cc966c8688abafe85eea280a88` | range分支合CI+batch guard主线；first-parent含workflow及2行，combined/remerge空。 | range parser仍不被旧slotbatch吸收；前面生产guard与本地barrier都保留；无独有冲突hunk。 |
| 15 `190400dc76641b463a9feaf4afbbe58c32f8797a` | Cmd RunBind 取消预取unless_null后清零CB offsets/sizes。 | 必须带到最终range实现；WARP先为某槽SetConstantBuffers1(FirstConstant非0/NumConstants)，pool某源null+unless_null后真实GetConstantBuffers1窗口完全不变；其他槽可更新。copy选项也测skip，不能仅比较buffer identity。 |
| 16 `c45db630757f99f6f56aa0dcfc6f60621416e800` | Cmd RunBind/Fetch pool元素GetResource的use_ring_index false→true，docs声明所有pool类型physical index、keys不动，fetch算更新。 | 不是已实现normal FIFO lookup的重复项。本地GetElementIndex已有use_ring_index override，可复用；range须用true，proxy应先ResolvePool。真实RING/STATIC/FIFO/SPATIAL按非0key占槽，range读写0/负wrap准确作用物理槽，keys与FIFO淘汰顺序不被新UID污染，assignment更新expiration/type而read按read_refresh设置。 |
| 17 `5a852ccac15325dd31e3c4ddf7a5505ed3a6340d` | 8 docs：新增pool variables.md、per-object-state.md，declaration列全部11个pool_设置，补STATIC/SPATIAL/120FPS radius/missing/expiry/type/default/persist，pool metadata改->Index/->Size含sentinel，修资源链接及fullrange reset。 | 多数生产特性本地早已实现，可同步文档不能重复搬实现。表defaults以真实IniHandler读取为准，去未测500B/“同全局变量成本”声称；本地CopyMetadataFrom实际还复制read-refresh/allocate-missing/type-switch等，后续pool-operations表别漏。此commit仍说Pool[*]仅null，但本地已支持全range资源copy，且本轮type-filter reset要按当前生产描述。per-object例未声明$object_id和引用的shader/commandlist是待补fixture，不称可直接运行。 |
| 18 `7800bd060b118dee3991566821fc3d92473299e3` | 新pools/pool-operations.md(代理/descriptor/reset)、resources index/hash metadata页补HashRegion/SpatialHash编码/参数/跟踪要求。 | 与本地pool proxies/copy_desc/reset及HashRegion/SpatialHash已等价的大部分是文档补全，不能据此重写实现。保留hash碰撞/30bit编码并非唯一ID、实际有限参数/物理buffer/CB窗口约束；空间32bit key按bit保存(含NaN)，不转numeric。copy_desc完整字段按真实CopyMetadataFrom，size缩小/扩大不凭旧文案断言全部contents不变。proxy flatten具体是设定时root引用，运行后上游root换源如何观察要以真实ResolvePool为准。 |
| 19 `83bcd3f2a5bfda09399656a5ca1e5b14b2d9f4c6` | commands/README增store同步readback成本及gpu-readback例链接。 | 可保留有条件同步等待说明，删无测量“一帧一次必严重掉帧/杀帧率”。现有本地Store安全规则保留：absolute backing float index、finite/nonnegative/overflow/ByteWidth、device identity、immediate-only、LOCKED、bitwise dirty|=1、structured完整元素复制。文档checker只有后续例页加齐才跑最终链接。 |
| 20 `893e3072bf5b7841151d6007c6bb252bed23a768` | 12 docs：examples index+7例(含两个后来删除的mesh/material例)，callbacks/Region/slot-indexing反链，root index。保留最终例：slot-range-capture、slot-scan、command-list-callbacks、gpu-readback、constant-buffer-region。 | 按278最终删除去重。修gpu-readback错误“store相对visible CB窗口”：本地和原API均absolute backing float index；真实visible第4float应($base/4)+4。每条store独立复制/Map，不因同structured元素就宣称多条共享一次readback。identity-change不能当内容不变保证；%30 gate可能同帧多draw重复执行。slot-scan 128层外还含If/linked list深度，不能只128<256便断言全扫总栈必安全；实际32/128扫描用真实完整Run链验证。CB例保256B/64KiB及copy物理边界；direct ref whole和Region保留不同语义；补所有未声明globals和不可独立运行外部shader依赖标识。 |
| 21 `278c4e63b7f82bfe4c024693aaf3bef996d35bc9` | 删除accessory-mesh.md/material-borrowing.md及examples/pools/Region相关链接。 | 是最终文档范围收缩；本地目前没有这两页，不新增再删除即可，记录与893合并后的净效果。不能把两页旧游戏附件/材质借用叙述作为已支持或已实机验证证据。检查final树绝无死链。 |
| 22 `91d4aae517b48228f3494be7bbe10bb1dc521069` | Cmd RunFetch 在取得assignment pool element前检查null+unless_null；docs明确skip不算更新/不延expiration。 | 与c45同range最终组，详第8项。真实uninitialized element、expired FIFO/SPATIAL、已有resource+variable并存、persist dirty，null source skip前后状态全相同；后续非null source确实初始化/更新，copy/ref/descriptor路径各保语义。 |
| 23 `b693992829dddaddb4ee1bf149e3853cf1d07be8` | Cmd SetSlotRange 新每slot cb_first/cb_counts数组，any region时一次XXSetConstantBuffers1；没有region仍XXSetConstantBuffers。whole slot在mixed batch用0/4096。 | 依赖190 CB window保存；不能保8fe早期错误“SetBuffers1无range API”及逐slot Set说法。真实六stage混合whole/nonzero window/null/skip GetCB1回查及可选实际调用计数，超物理合法window、small copiedCB whole保留；无offsetting支持设备保持原whole API路径。上游页到此仍旧称copyCB逐slot，文档必须随实际修正。 |
| 24 `99db72f363cd8023644dc42e572b851608989603` | Cmd删掉资源copy parser中没有slotrange配对的poolrange特定警告/return，后续标准pool/source分支处理拒绝。 | 这是去重特殊诊断，不等于新增任意Pool[0:1]=Pool[2:3]、变量子range、store/TO ranges支持。最终生产必须明确失败，避免残留POOL_RANGE被错当POOL对象copy_desc/ref。真实拒绝单poolrange对普通resource/独立pool、$Pool range、metadata range与bare-bare无bounds，保错误后无注册悬垂。 |
| 25 `ac9947bf122137f9f6aba56c25c0f200c4b149ef` | Cmd.cpp/h 去_ParseTarget二层wrapper，在各ParsePool/Custom/Pipeline返回处统一AcceptParsedTarget(ret,allow_range)，减少不应允许range处的重复警告。 | 不扩大allow_range默认false。只有resourcecopy LHS/RHS与dump显式允许，reader其他调用默认不允许；ParseTarget custom优先顺序/allow_custom=false防字面量别名遮蔽仍保留。逐入口确认early return也经过AcceptParsedTarget，failed parse后残留type/eval字段不能令caller继续创建operation。当前本地尚无range，待实现。 |

## 文档最终形态与本地兼容约束

- 新增dynamic/range slot页及最终保留的6个新增示例页（5个893例 + 5a的per-object-state）；删除的附件/材质例不落本地。现有binary-flags/pool-based-tracker保留，不回退已适配的64bit二进制解析、float24bit精度、random无distinct保证、Input scope/defaultoff、logging invalid-level fallback、color_space名称。
- 维护变量/池/Store/Region当前真实语义；`PoolFoo[*]`与`$PoolFoo[*]`不同、reset类型过滤/initialized行为以已集成代码为准，不能用这批旧文档覆盖；`->Size`在CB窗口、physical buffer/SRV之间已有区别；HashRegion偏移相对可见区域，Region本身绝对byte offset，Store本身绝对float index，三者不能混写。
- 不加入mask未实现选项，不把shader原bytecode声明误称replacement分析，不改变inputlayout缓存策略/ShaderBindings生命周期/COM WIC factory；本批Cmd/header覆盖面大，务必按hunk合并。
- 全部新代码只属DX11/C++17；没有必要升级DX12/共享thirdparty。若range helper新增文件，真实接入CMake目标与回归target；不复制vcxproj工作流。禁函数内struct/class，上游SlotTypeInfo需文件级。
- 文档验收：`python build/upstream/check-ini-docs.py`前先读脚本接受参数；检查全树精确大小写路径/anchors/fences，修`.md/#`链接，所有示例hash为有效hex、所有变量有声明或明确省略，外部资源/shader和目标游戏未验证明确标记。此审查未运行checker，因为未修改语言文档树。

## 父关系与 merge 证据

5 个 merge 的 `.cc` 和 `.remerge` 均只有commit消息，无 `diff --` patch。first-parent stat 可包含父支线功能，不能当“空merge无任何父功能”处理。普通提交均已读完整patch，最终range快照用于确认后续修复没有覆盖上述数组/metadata/转换问题。

| SHA | 有序父 SHA |
| --- | --- |
| `48f3b6db03814bac65b0ac990e0a26446c3bd73f` | `034e2875ff7b04eefa75a312144cd5338d8c6cce` |
| `fbd5f9d48c0d8066b9412102c93203050eaffe79` | `48f3b6db03814bac65b0ac990e0a26446c3bd73f`; `50c9fc8c960b8bd7e496f0ee32bfe4771587debf` |
| `c1378828238f165cec8efbaf78cab985373e7bbd` | `fbd5f9d48c0d8066b9412102c93203050eaffe79` |
| `e414c5feb37c2bc9457ebea99ba08fbef1da4a5d` | `a02418316be1c7e03d839d1322919f98b8f7ad38`; `c1378828238f165cec8efbaf78cab985373e7bbd` |
| `a4f0fe512e0e3df45f5421168bb78425a70de8b7` | `4b48720e68c12d1857825cdcce83cd0185a90964` |
| `1011b2183406868e5467b5641a864390ba2f70c3` | `e414c5feb37c2bc9457ebea99ba08fbef1da4a5d`; `a4f0fe512e0e3df45f5421168bb78425a70de8b7` |
| `b779e7e2ec9a5519ef2b00be0f89ab94aee015fa` | `1011b2183406868e5467b5641a864390ba2f70c3` |
| `595de8fadcc76c47445f4ccb5b86cd7fc25a5d31` | `b779e7e2ec9a5519ef2b00be0f89ab94aee015fa` |
| `8fe6aac1b2e93fab004cc85931dae63bc7c2768f` | `595de8fadcc76c47445f4ccb5b86cd7fc25a5d31` |
| `cf8ba704a5b295601ff40ad6caa5348e468dbcd8` | `8fe6aac1b2e93fab004cc85931dae63bc7c2768f` |
| `407920fb27e023cf2ab067f5759d1f7bff49438a` | `1011b2183406868e5467b5641a864390ba2f70c3` |
| `84c4948fca067caf3b0dbd0bb51de5fdf457be7d` | `1011b2183406868e5467b5641a864390ba2f70c3` |
| `4a20acd20d0b4733dc5e0a61061b4880be2e6306` | `407920fb27e023cf2ab067f5759d1f7bff49438a`; `84c4948fca067caf3b0dbd0bb51de5fdf457be7d` |
| `ef24b4316629a9cc966c8688abafe85eea280a88` | `cf8ba704a5b295601ff40ad6caa5348e468dbcd8`; `4a20acd20d0b4733dc5e0a61061b4880be2e6306` |
| `190400dc76641b463a9feaf4afbbe58c32f8797a` | `ef24b4316629a9cc966c8688abafe85eea280a88` |
| `c45db630757f99f6f56aa0dcfc6f60621416e800` | `190400dc76641b463a9feaf4afbbe58c32f8797a` |
| `5a852ccac15325dd31e3c4ddf7a5505ed3a6340d` | `c45db630757f99f6f56aa0dcfc6f60621416e800` |
| `7800bd060b118dee3991566821fc3d92473299e3` | `5a852ccac15325dd31e3c4ddf7a5505ed3a6340d` |
| `83bcd3f2a5bfda09399656a5ca1e5b14b2d9f4c6` | `7800bd060b118dee3991566821fc3d92473299e3` |
| `893e3072bf5b7841151d6007c6bb252bed23a768` | `83bcd3f2a5bfda09399656a5ca1e5b14b2d9f4c6` |
| `278c4e63b7f82bfe4c024693aaf3bef996d35bc9` | `893e3072bf5b7841151d6007c6bb252bed23a768` |
| `91d4aae517b48228f3494be7bbe10bb1dc521069` | `278c4e63b7f82bfe4c024693aaf3bef996d35bc9` |
| `b693992829dddaddb4ee1bf149e3853cf1d07be8` | `91d4aae517b48228f3494be7bbe10bb1dc521069` |
| `99db72f363cd8023644dc42e572b851608989603` | `b693992829dddaddb4ee1bf149e3853cf1d07be8` |
| `ac9947bf122137f9f6aba56c25c0f200c4b149ef` | `99db72f363cd8023644dc42e572b851608989603` |
