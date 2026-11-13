# DX11 后续20项逐提交审查

审查来源：账本中 `37635f6e` 之后按已有拓扑顺序的20项pending；逐项完整读取 `git show <SHA> --`，并检查 `git rev-list --parents`：本范围20项均为单亲连续提交，没有merge需审查combined diff。未修改生产代码、账本、现有文档或测试，未构建。本地核对代码包括CommandList.cpp/h、HackerContext.cpp、IniHandler.cpp、ResourceHash.cpp/h；行号随主代理并行移植变化，以下用符号定位。

## 先决定的实施组

1. **共享readback组**：`7cf7c34d/4a6062d1/246bd0a4/1b9df486`。7cf析构已经调用 `FlatHashMap::for_each`，该方法直到1b9才出现，单独7cf不能编译。可提前引入已审查helper或作为同一构建组，每个提交仍独立记账。
2. **CommandList callback** `be77b27e`：独立较大状态机变化，要覆盖解析、运行解析链、noop优化、reload清理和原有pre/post行为；不能当作普通ResourcePool proxy处理。
3. **CB Region** `ba335207`：与本地CB窗口绑定、metadata、hash cache语义交叉，必须在真实WARP集成验证，勿仅加parser。
4. **Reader LOCKED组** `49596946/37a4bb26/ce1ceea4/71c46f54/84a7d071`：可一起验证parser边界，保留目前更早加入的LOCKED生产保护；495源码未使用is_source，需适配修正。

所有20项没有新增源文件、资源或外部依赖，现有DX11 CMake文件集合足够；均可适配C++17。不得照搬额外的全局标准调整。ResourceHash const find_ptr、最小容量2、erase，以及字节偏移/CPU snapshot ownership/缓存失效修复必须保留。

## 逐提交细节

### 1. `7cf7c34d47cdc6016f016c092721d81b7d8fd9fd`

- 实际变化：HackerContext.cpp/h新增 `FlatHashMap<UINT, ID3D11Buffer*> mReadbackBuffers`，GetReadbackBuffer按2次幂分桶创建CPU_READ staging，返回池持有的借用指针；析构逐项Release。
- 依赖：1b9的for_each；现有ResourceHash类型可见性。保留本地virtual析构和ResetTrackedInputLayout清理，不能整段替换析构。
- 危险点：NextPow2对size>2^31会回绕0，0请求也会建1字节；应拒绝无效大小和设备上限/溢出，不缓存失败。池非线程安全，同一context同一bucket并发Map/Copy会冲突，核对context的调用序列而非声称device creation锁保护整个使用期。借用指针调用者不可Release。
- 验证：同bucket复用、跨bucket不同、非2次幂请求内容拷贝、0/极大请求、CreateBuffer失败重试、多个context/device隔离、析构释放及保留布局引用清理。不可skip。

### 2. `4a6062d14e00b75b06b44f6e670f31b61e353f7b`

- 文件：CommandList.cpp、FrameAnalysis.cpp、HackerContext.cpp、ResourceHash.cpp/h。
- 实际变化：GetRegionHash/GetSpatialHash改收HackerContext，生产各调用点随之改；CacheBufferData通过共享staging、CopySubresourceRegion和CPU-owned malloc副本回读，而非每次创建buffer。
- 本地适配：本地早已修复复制mapped内存再Unmap，不能将这部分重复算新功能，但共享staging仍未实现。保留TryGetSpatialHash的bool有效性、零packed origin、字节偏移、L2/L3安全语义；所有生产和测试调用签名需一并适配。可以保留接收原生context的明确兼容包装供既有调用，但不能因此绕过真实池验证。
- 危险点：桶大于源，不能CopyResource；box只复制desc.ByteWidth有效区。上游用context->Map而Copy用原生context，核对Map hook是否造成hash递归/污染；调用orig Map可减少副作用但应验证语义。Deferred context不能像immediate那样同步read Map，失败不得发布旧/空snapshot。malloc/Map失败释放CPU副本，不释放borrowed staging。
- 验证：非2次幂buffer、连续两个同bucket不同内容无串数据、region/空间hash原有suite、Map失败不替换原cache、FrameAnalysis/VB/IB各调用链构建。不可完整skip。

### 3. `246bd0a4226bb78a7506b422dabc712c34235075`

- 文件CommandList.cpp/h。Store使用CommandArgumentReader解析目标变量、source target和动态offset expression；只复制4字节到共享staging，offset_expression替代int loc。
- 单位证据：`UINT offset = expression.evaluate(state); box.left = offset * sizeof(float)`，因此offset仍是**float元素索引**，不是提交描述中的字节offset。source->Region/当前CB view是否计入需按真实GetResource输出明确，不能自行混为字节。
- 保留LOCKED检查和PERSIST dirty标记（新上游run直接写fval，没有补dirty）；持久化代理已补的检查不能被重写消失。unique_ptr expression在Store自身不可复制的语义下可用，不必把ResourceCopyTarget shared_ptr安全设计倒退。
- 危险点：float→UINT遇NaN/负数/越界UB；乘4与box.right可溢出；必须先GetType验证BUFFER、获取ByteWidth，确保index在可读完整float范围；无效source/offset不覆盖变量。src_view必须Release（上游仅释放src_resource，可能漏view）；Map成功才Unmap，读数据用memcpy避对齐假设。
- 回归：真实WARP默认buffer多处值、动态索引变化、目标member内嵌表达式、重复使用4字节bucket、边界最后一个float、缺失/负数/NaN/inf/越界、texture source拒绝、locked拒绝、persist dirty仅值改变。不可skip。

### 4. `1b9df4862bf2e2848e08c4ebfa143bf7cf39f55c`

- ResourceHash.h新增for_each遍历当前generation有效项；构造参数capacity改名initial_capacity。
- 只移植遍历和必要命名，保留本地minimum capacity2与const find_ptr/erase。若回调修改map导致rehash，迭代失效，应限定回调只处理value所持对象。析构Release回调满足该约定。
- 回归：空map、插入/覆盖、erase、clear换generation后只访问当前项，各key恰一次，const find_ptr仍编译。改名本身可不照搬，整个提交不能因含改名而skip。

### 5. `be77b27e97fab209ed210244f1ce2188b6c405c3`

- CommandList.cpp/h、ShaderRegex.cpp。新增CopyCommandListCommand、source_command_list、Resolve/SetSource、runtime_populated、noop；更新custom/explicit/linked/if/texture/regex优化入口；MAX_RECURSION从64增256。
- 解析风险：CopyCommandListCommand::src未默认初始化，非null但也非commandlist的输入可能带未初始化指针入运行；应初始化nullptr并拒绝未知源。`wcsncmp("null",4)`接受nulljunk；选项无效只警告却继续；须严格匹配并禁止无效options。
- 状态风险：SetSource把src解析到root保存，设置时已经proxy的中间节点会被flatten，之后该节点重定向不会影响旧绑定；按实际上游行为写用例，不把它误报“任意时刻链上修改全部传播”。Resolve递归无深度保护，setter虽拒绝root==this，也要保证所有修改只经setter。pre/post两次SetSource应先检查两边后提交，避免第一边改变、第二边拒绝留下半更新。failed一旦true永不重试，后续解除循环后命令仍禁用，需明确恢复策略。
- clear/reload：现有clear只清commands/static_vars；新source/runtime_populated也需重置，避免旧proxy越reload生命周期。noop必须保留runtime潜在回调，不能把当前空命令裁掉。头中`bool CommandList::noop();`类内多余限定需改`bool noop();`以符合标准C++17。
- 回归：ref赋值/运行、null恢复原内容、pre/post、自引用/两节点/三节点循环、重指向、空callback经优化后仍可填充、If/ShaderRegex链接、reload清除旧target；递归上限明确验证。不可skip。

### 6. `76a4193982027c47497c2c59ec4b8bc70d424c78`

- CommandList.cpp/h、HackerContext.cpp/h、HackerDXGI.cpp。新增draw_number/dispatch_number，在BeforeDraw/BeforeDispatch自增，Present的frame_no++之后清0，表达式只runtime读取context计数。
- **上游字段未初始化**：必须构造置0，首次Present前可能已有draw；GetDraw/Dispatch表达式要有有效state/context，配置static不得静默读取空context。两计数按context持有，Present只清swapchain对应context，不自动保证所有deferred contexts全局归零。
- 回归首次draw前0、每类调用独立增长、被skip调用计数语义、Present清0、两context隔离、常量表达式不折叠。不可skip。

### 7. `ba335207af6a6253ac28b33896c88f60901188f6`

- CommandList.cpp/h新增->Region(offset,size)及RESOURCE_REGION；evaluation enum拓宽uint16→uint32且VARIABLE/LAYOUT bit位迁移，必须统一声明/mask使用。run取得源后覆盖绝对byte offset/size；ref分支调整destination view并走SetConstantBuffers1。
- 语义：Region绝对底层buffer字节位置，不相对当前绑定view。保持普通copy/ref旧行为。上游最后CB if判断与注释“非ref绝不SetCB1”并不完全一致，需按真实copy分配buffer和窗口发布核对，不能只读注释。
- 安全：参数finite/非负/UINT界限、offset+size溢出、物理ByteWidth范围、CB 16字节常量单位和256字节窗口约束、源view/目标view元数据及hash cache要一致。本地CopyResourceToResource的destination参数与batch局部target不可倒退。
- 回归同CB不同view，Region绝对基址、copy生成准确字节、ref实际IA/CB绑定窗口回查、边界/零/不对齐/越界失败、custom资源描述/哈希、batch。不可skip。

### 8. `4f3722cafe71f002e48a5a621bdff3b31290ef1d`

- CommandList.cpp/h新增SetSourcePool bool结果、递归Resolve、cycle拒绝、PoolCopyOperation sticky failed。
- 本地已提前实现迭代链解析、完整链cycle拒绝、effective metadata复制，功能主体等价且更安全。仍缺bool返回/failed，因此不能不加说明就当完全相同。建议保留本地迭代链与动态代理节点，不照搬递归/flatten root；用失败返回避免重复无意义日志但不要让一次临时cycle永久禁止后来有效操作（若保留sticky upstream行为则明确测试）。
- 回归三节点代理、循环保持旧source、解除proxy、copy_desc源有效描述、同命令失败后再成功策略。可记录“链和cycle主体已由早期本地适配覆盖”，sticky控制需明确移植/不适用依据。

### 9. `ab7b929e4f979a8657392468b464e7e72dd1da42`

- CommandList.cpp/h、IniHandler.cpp。read_refreshes_expiration默认false；PostponeExpiration允许read刷新；新INI pool_expiration_refresh_on_read。
- 除三处diff，还应CopyMetadataFrom复制该配置，否则copy_desc遗失新策略。保留本地PoolSlot.updated、expiration_checked，且read刷新不应给原本不存在的元素分配新slot。读取发生在Expire之前/之后的边界需按现有调用顺序验证。
- 回归默认read不过期延迟、启用read延寿、write始终刷新、RING/FIFO/SPATIAL、proxy、copy_desc/重载、帧号回绕。不可skip。

### 10. `5accb5a611ceab4a595bb3a16f2e53ba49940168`

- CommandList.cpp GetPoolObject：缺静态object且无dynamic index时，type非POOL立即nullptr，避免继续调用getter空pool。
- 本地公开GetCustomResource和GetPoolVariable已提前检查`!static_object && !custom_resource_pool`返回nullptr，当前调用表现等价，但通用GetPoolObject内部仍只日志后调用getter。建议仍把防御下沉，不仅凭两个外层guard就宣称helper已相同；如果skip必须限定“当前两个生产入口已覆盖”并保留测试证据。
- 回归非池常规target getter返回null、正常pool模板、静态/动态索引及null state。无构建适配。

### 11. `03d562402de74e726503455be48f33643001167a`

- CommandList.cpp把CB spatial region_offset由offset改offset/4，原因是上游GetSpatialHash按float下标。
- 本地已统一**字节偏移**：GetResourceSpatialHash用region_offset加member bytes，TryGetSpatialHash按BYTE位置memcpy float。保持region_offset=offset恰好正确，照搬/4会回退。可按“本地字节模型已等价修复”跳过代码hunk；账本附真实CB窗口非零offset空间哈希测试，不只以源码推断完成。

### 12. `aa3cad5b03a6c99955e350eda7c638982d98c73e`

- CommandList.cpp/h新增POOL_LAST_FRAME、->lastframe、CustomResourcePool::GetLastUpdateFrame、operand getter。
- **错误索引**：GetPoolElementLastFrame读member_args[0]，但->LastFrame是零参数getter，Pool[id]索引存pool_dynamic_index_expression/静态pool对象，不在member args。必须复用目标index机制，区分静态逻辑key/物理slot及proxy。
- **越界**：GetLastUpdateFrame未经检查用GetElementIndex结果访问index_table；空池/未命中SIZE_MAX、未启expiration的RING空table会崩。定义安全未更新结果，不能以0伪装确实frame0。读取refresh_on_read=true本身会更新时间，文档需准确说明。
- 回归两个不同id记录、静态/动态/NaN key、空池/未存在id、禁用expiration、proxy、read刷新、UINT_MAX更新时间与本地updated bool。不可skip。

### 13. `f8a50f93bde03cbf0d5fda05d3b45e7910c2fc79`

- IniHandler.cpp GetIniBoolIntOrEnum先试GetIniEnum，再试BoolOrInt，避免新expression fallback在合法enum `overrule`上发未识别警告。本地目前仍旧顺序。
- 保持bool/int合法范围及found/default语义，不能只让结果正确却仍先触发错误日志。验证allow_duplicate_hash=overrule/true/false/0/1/2/表达式/未知值，合法enum不产生syntax notice。不可skip。

### 14. `609244f435bcdabd65ac0a2e8060f242ac4cc467`

- CommandList.cpp tokenise Other Tokens调用FindIdentifierTokenEnd从HYPHEN改NONE，修复`time-1`等被误当整标识符；保留ResourceCopyTarget资源slot中连字符。
- 依赖51a新分词器，改动虽一行却是行为修复。回归time-1、frame_number-1、x-1、sin(0)-1与ps-t0/命名空间resource连字符。不能整体去掉所有scanner的hyphen。若主代理51a已先修本点，应记提前覆盖的证据。

### 15. `2c5b135428cabc72b66d658beca661aef64af409`

- ResourceHash.cpp空间grid从10/10/10改12/8/12（4096×256×4096），pack/unpack、各轴wrapped distance同步。
- **本地关键冲突**：WorldToCell为避免int32 UB先fmod1024；不能仅照搬mask和shift，否则X/Z即使12bit仍先折到1024，新增分辨率无效。改成每轴size的安全double floor/fmod，再pack；保留finite/cell_size检查、TryGetSpatialHash零与NaN bit模式有效性。
- 回归X/Z 1023/1024/4095/4096，Y255/256、负位置/大finite、各轴环距、32位全1/NaN key可占用与复用、旧spatial tests新grid预期。新表示可能产生IEEE NaN，用bitwise比较，不用float==。不可skip。

### 16. `4959694666e1af3477aef92d5309362c3c883397`

- CommandList.cpp/h reader GetVariable增加bool is_source；Store传false；解析锁定变量时发错。
- 上游新增is_source却**没有使用**，实际所有LOCKED变量均拒绝。正确检查应`!is_source && (flags&LOCKED)`，允许源读取。保留主代理更早修的Store/Override locked规则。全局变量源/目标、local shadowing、unknown/locked诊断和reader不消费失败token均应测。
- 此项可与后4项共同集成；若已有相同保护不是理由直接丢reader接口功能。

### 17. `37a4bb26e6a7bc803a18b64ce55693461fa47f5b`

- CommandList.cpp变量赋值用reader读取目标，local声明支持分隔符并跳过$Pool[..]让后续pool parser处理。
- `line[0]`在空line上需明确检查；`compare(0,5,"local")`把localfoo也当local，必须按完整token匹配。赋值目标读取后未Finished会接受尾随垃圾，应严格检查；声明后早return无initializer也要确保line无多余token。保留pre/post共享local scope，失败声明不要留下半注册污染。
- 回归local声明有/无initializer、tab、多余token、空/未知/locked、局部shadow、池目标fallback；不能以重构标题skip。

### 18. `ce1ceea40562ba8496c6306eb444a3ba7eeebfc5`

- CommandList.cpp GetVariable在m_scope空时不调用find_local_variable，只查global并改善错误提示。为Key/Preset无scope调用准备。
- 本地find_local_variable本就处理scope空，但接口新路径仍需准确区分源/目标；可提前在reader实现时带入该guard并逐项记录。回归nullptr scope global成功、unknown global失败、有scope local优先。不能说此项只改文案而忽略null调用约定。

### 19. `71c46f549a9564043d78b9cf46f019696b90335c`

- Override.cpp KeyOverrideCycle读取变量改用reader.GetVariable(var,false)，拒绝locked目标。依赖495、ce1。临时command diagnostic误写store，由下一项修key。
- 主代理早期LOCKED全写入路径修复若已覆盖cycle，需要保留现有行为；reader严格消费可进一步拒绝尾随垃圾。真实Key cycle fixture必须证明拒绝locked且未锁cycle照常。

### 20. `84a7d07104516620ef003905bf7bf60f7f1913fb`

- Override.cpp Override::ParseIniSection（Preset/普通Key）同样用reader拒绝locked；把cycle diagnostic的store改key。
- 主代理早期LOCKED保护可能已等价覆盖拒绝，但仍需核对reader变量语法/namespace/完整消费。若提前覆盖，不要重复迁移或删除runtime防御。回归Preset/Key的locked目标忽略/诊断、unlocked即时/渐变、namespace、cycle诊断名key，不触发无scope崩溃。

## 可跳过的强度与验证

明确可不照搬代码的hunk是03d56240的/4（与本地字节模型冲突）；4f的chain/cycle主体已存在但还有bool/失败状态差异；5acc仅当前公开入口等价而helper未同等防御；71/84可能由当前并行LOCKED适配提前实现，需合并后核对。其余不存在仅凭标题即可skip的纯格式/无关提交。

本文件给出了回归场景，没有执行它们。落地每组应跑真实生产DX11/WARP回归与集成构建，资源池和readback还需检查失败、重载、复用与析构；游戏验证仍独立记录。性能上只证明减少创建/复制范围的实现，不把上游宣称的“数千至数百万倍带宽改善”当成本地测量结果。
