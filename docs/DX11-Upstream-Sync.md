# DX11 上游同步记录

上游：https://github.com/SpectrumQT/XXMI-Libs-Package，master。

## 基线与范围

本地 DX11 在上游 `7ee05173` / `44378b1b` 的 45 个 C/C++ 文件中，43 个 Git blob 完全相同。其余差异已逐份核对：ResourceHash.h 增加 const find_ptr；ResourceHash.cpp 修正两处 printf 参数格式。这两个上游提交间仅发布工作流变化，因此采用 `44378b1b3694bc28c0488f78f2d346f38a4f50d0` 为有效代码基线。这是内容证据推定的导入基线，不是原作者提供的来源记录。

首次检查 tip 为 `eae473b3c3b833380e70ba9dc61a45c9937ef924`，基线之后拓扑序共 336 个提交，含分支与合并。完整清单和每项状态见 `dx11-upstream-sync.json`。共享代码还包含本地 DX12 扩展，不能直接覆盖。

## 第一批

- `4269c75b`：资源池模板及实例保留所属池指针。
- `572f5b29`：模板与已创建实例同步 bind/misc 标志，新实例继承模板标志，解析期动态索引返回模板；间接绘制和引用复制接入统一传播。将 BindFlags 的输出计算拆成独立语句，避免 C++17 参数求值顺序造成 misc 标志丢失。

验证：MSVC x64 / CMake Ninja / Release 的真实 DirectX11 目标构建成功（71 步），保持 C++17。运行 `scripts/test-dx11-pool-flags.py` 成功，覆盖独立资源、池模板、已有实例、空槽、原标志保留、重复传播和空池。该 CPU 测试抽取两个生产方法执行，以精简资源模型替代 COM 资源；不覆盖真实 INI 解析、设备资源分配及重载生命周期，因此这两项仍为 implemented-awaiting-validation。

尚未进行目标游戏 DX11 验证，不能宣称功能回归全部通过。CTest 实际运行报告 No tests were found。

## 第二批

- `b94661f3`：接入 Size、Stride、SourceStride 属性查询、表达式后缀识别、文件缓冲区大小元数据和复制时的源 stride 记录。SuffixInfo 移到函数外并命名为 ResourceTargetSuffixInfo。
- `3894a79f`：资源/资源池标识改用 float30 编码；保留 C++17 的 memcpy 转换，不升级标准。
- `e61bb1e4`：同步结构化缓冲区转换函数拼写、区域复制参数命名和说明整理。另将原有注释中的非 UTF-8 横线改为 ASCII，消除本次编译观察到的 C4828。

真实 DX11 Release 目标增量构建通过。新增 `scripts/test-dx11-resource-properties.py`，抽取生产 getter、格式大小函数与哈希函数，使用真实 WARP 设备、结构化缓冲区、纹理和 SRV；覆盖大小/stride、绑定元数据、显式 stride/格式覆盖、资源缺失、非缓冲区错误、1000 次调用后的 COM 引用平衡及 float30 边界编码。测试中的精简 target 适配器替代管线槽位查找；资源本身和 COM 引用是真实对象。

两份回归脚本均通过；INI 后缀解析、SourceStride 赋值的真实命令链、游戏内重载尚待验证，所以保持 implemented-awaiting-validation。当前共实现 5 项，331 项 pending。下一项 `a5d769e1` 增加常量缓冲区绑定标志冲突检测，需同步更新标志测试签名和失败路径。

## 第三批

- `a5d769e1`：常量缓冲区标志冲突检测与解析期失败返回；保留先计算 BindFlags 的适配，修正日志将 wstring 指针误当宽字符串的参数错误。
- `3e214ffb`：区域哈希越界区间截到可用缓存范围；保留本地日志格式修正。该范围变化尚需专项数据哈希测试。
- `3570d4f7`：拆分前缀、成员、管线槽、自定义资源、池的解析；辅助结构位于函数外。此上游提交本身缺少 IniParserResult 声明，提前加入后续 `dceb864a` 使用的三态枚举至生产头文件；后续移植需去重。空指针检查移至 wcslen 前。
- `1cc273f4`：Offset 属性查询及表达式分发、解析接入。

新增 BUILD_TESTING 下的 `dx11_upstream_regression` CTest 目标，编译并链接完整 DX11 生产源码，复用真实头文件、全局资源表、解析器和资源池方法，测试中不补造生产依赖。验证空输入、槽位边界、无效 shader/UAV、只读来源、backbuffer 禁止视图缓存、自定义资源查找与 size/stride、池实例创建/复用/标志继承、静态求值模板、冲突返回、Offset 与 SourceStride 解析及销毁。已有隔离标志测试扩展常量缓冲区冲突与 misc 标志清理。

DX11 DLL、回归可执行文件构建通过，CTest 1/1 通过，两份隔离脚本通过。GPU offset 值、区域哈希截断的专项验证及游戏内重载仍待完成。累计 9 项已实现，327 项 pending；下一项 `8d9841a7` 为常量缓冲区窗口支持。

## 第四批

- `8d9841a7`：六阶段 Get/SetConstantBuffers1 的窗口偏移和大小转换，复制链传递常量缓冲区区域信息。
- `ea29f9a5`：常量缓冲区 stride 固定 16 字节；补上 custom resource 分支 break，避免普通自定义资源错误返回 16。
- `61f5ce25`：缩短冲突警告并修正上游文本调整后未同步的格式参数顺序。
- `046c7bfe`：常量缓冲区纳入区域哈希 Map/Unmap 数据跟踪。

WARP 集成测试在六阶段分别验证真实窗口绑定、256 字节偏移/512 字节长度回读、stride/offset/size getter、整缓冲区回退、解除绑定及资源引用释放。补测真实区域哈希：4096 字节缓冲区尾部超长请求截断、重复命中、末尾/超大 offset/空范围、清缓存后 GPU 数据更新与快照重取。由此也补齐第三批区域截断的专项证据。

该测试暴露了原 CacheBufferData 将驱动 Map 指针转交 free 所有权且 Unmap 后继续使用的问题。现改为先复制到 malloc 内存再转交缓存，失败时执行 Unmap/Release 并返回失败；GPU 快照测试与释放路径通过。后续缓存 shared_ptr 重构必须保留这个修复。

DX11 DLL、完整生产源码回归目标构建通过，CTest 1/1 通过，两份隔离测试通过。实际 hook 的 Map/Unmap 跟踪、复制命令执行与目标游戏重载仍未验证。累计 13 项已实现，323 项 pending。下一组 `8d4dcb2f` 与 `451b9d96` 包含缓存共享所有权及 view 元数据：前一个提交提前引用了后一个才声明的 cached_data_offset，需作为依赖组适配并检查初始化、边界与快照所有权。

## 第五批

- `8d4dcb2f`：缓存改为 shared_ptr 所有权，支持调整大小和 GetCachedData；保留 malloc 自有 GPU 快照及日志参数类型修正。
- `451b9d96`：自定义资源缓存视图与复制链接入。与前一提交配对适配，补齐 cached_data_offset 的声明依赖。
- `1419048c`：自定义资源区域哈希支持与缓存命中路径调整。

适配修复：初始化并维护 RegionHashesCache::data_size；GetCachedData 在空缓存时返回 nullptr；视图默认长度扣除偏移，过长长度截断、无效来源清旧状态，重绑定重建视图哈希缓存；自定义视图绕开仅按 GPU buffer 地址索引的 L3 缓存，避免相同缓冲区的不同视图互相污染。

新增生产源码测试验证不同视图哈希隔离、重绑定、末尾默认/超长长度、溢出偏移、空来源、源缓存销毁后的 shared_ptr 生命周期，以及缓存部分写入、扩容与清空；此前 WARP 窗口和 GPU 快照测试全部保留。

本轮发现中文 MSVC 的 /showIncludes 前缀被 CMake 错解码，导致旧目录增量构建缺失头文件依赖。CompilerOptions.cmake 针对已观察的乱码前缀修正；在新目录 build/dx11-upstream-en 完整重建后再 clean-first 重建（100 步），DLL 和测试目标均通过，CTest 1/1 通过。Ninja -t deps 确认 FrameAnalysis.cpp.obj 的 CommandList.h/ResourceHash.h 依赖有效；前几轮增量证据由本轮完整重建取代。两份隔离回归也通过。

累计 16 项已实现，320 项 pending。共享视图在源数据部分更新时的缓存失效、真实 hook Map/Unmap、复制命令和游戏重载仍待验证。下一项 dceb864a 为成员参数解析，需去重此前提前加入的 IniParserResult。

## 第六批

- `dceb864a`：成员常量/变量参数解析；保留已有 IniParserResult 单一定义，修正参数个数越界、尾逗号及缺少右括号路径。
- `c0c9c881`：HashRegion(offset,size) 与表达式求值接入。增加负数、NaN/Inf、UINT 范围和基址加法溢出检查，避免浮点转整数未定义行为。
- `e9cef6ff`：管线固定名称按完整 token 匹配，不再错误接受 prefixib 等后缀匹配。
- `c88a9e0b`：单 token 空白与空参数拒绝，保留空指针前置检查；分词器区分表达式分组括号和成员参数括号。

生产源码回归覆盖动态 HashRegion 变量、CB 窗口叠加参数偏移的实际编码结果、分组 Size()、缺括号、过多参数/空参数/尾逗号、精确槽名、非有限/负数/超范围参数。

第五批的共享视图部分写入验证已补齐：写入共享数据时先分离源快照，使已捕获视图保留原数据；同时清除源区域哈希和全局缓存。原 L2 只记录起始页版本，无法发现区域后续页写入，因此部分写入时按资源清除 L2 缓存（clear 为代际更新）；新增跨页哈希回归。共享时首次部分写入需要复制可见缓存，后续独占写入仍原地更新。

扩展测试曾出现 30 秒超时；日志定位并非括号解析，而是小缓冲区 FlatHashMap 初始容量为 1，首次插入后缺失键查询无限探测。最小容量改为 2，并补 tiny-map miss 测试；修复后完整 CTest 1/1 通过（0.19 秒），DLL/测试构建通过，两份隔离脚本通过。超时结果不计作通过。

累计 20 项已实现，316 项 pending。下一组是 `0c734124` 的 WIC 加载修复、`2f7436ac` 合并审查和 `11b2a1da` 的惰性 COM 初始化；需一起核对线程初始化/释放和 CMake/C++17 兼容。真实 hook、复制命令、配置重载和目标游戏验证仍未完成。

## 第七批

- `0c734124`：WIC 文件加载前准备 COM，明确 FORCE_SRGB 标志；Windows WARP 实测 PNG 解码。
- `2f7436ac`：合并审查完成，combined diff 无冲突解决内容；分支补丁已单独移植，因此该合并记 skipped，避免重复应用。
- `11b2a1da`：WIC 载入/截图/帧分析/Shell 操作的 COM 使用准备。上游持久 TLS 的 COM 标志未提供释放链，本地 TLS 在 DllMain 销毁，不适合增加 COM 清理。适配为 ComScope：在使用处初始化，S_OK/S_FALSE 配对释放，RPC_E_CHANGED_MODE 借用宿主 STA，所有 COM 调用在 DllMain 外。依据：[Microsoft CoUninitialize 要求](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-couninitialize)。不保留上游长期缓存 COM 状态的优化，也不声称等价性能。
- `89acf4df`：同步 CopySourceToDestination 拆分，保留复制链已有适配。
- `3c55747f`：InitializeDataCache 增加 offset 参数，SetHandleInfo 使用该入口；此前已补的长度截断和哈希缓存初始化保持。

作用域 COM 集成首次触发第二条线程加载崩溃：DirectXTK 的 InitOnce 永久保存首个 apartment 创建的裸 WIC factory，COM 结束后后续使用不安全。内部 _GetWIC 改为操作内拥有的 ComPtr，WIC2 能力标记按线程保存；公开纹理加载和截图接口不变。代价是重复创建 factory，未做性能收益声明。因修改共享 DirectXTK，额外构建 DirectX12 目标（33 步）通过，仅构建未部署。

回归验证：新线程自动初始化/嵌套 S_FALSE 配对；宿主 STA/MTA 不被提前释放且无残余 COM 引用；三条新线程通过公开 CustomResource::Substantiate 加载真实 PNG，检查 sRGB 格式与 RGBA 像素，执行真实 WIC 截图保存；缺失文件失败路径也不遗留 COM 初始化。首次测试误限定 RGBA，但 WIC 实际返回合法 BGRA sRGB(91)，修正为支持的 sRGB 格式加精确像素检查，未放宽内容断言。

DX11 DLL、完整生产源码测试和共享依赖的 DX12 目标构建通过；最终 CTest 1/1（0.23 秒）、两份隔离回归通过。Wine/Linux、实际 FrameAnalysis/Shell 调用、完整复制命令与游戏重载仍待验证。累计 24 项已实现、1 项合并 skipped、311 项 pending；下一项 `9fbc28d0` 是资源池过期机制。

## 第八批

- `9fbc28d0`：资源池索引表重构、keep-alive 过期、可选置空及 INI 配置接入。
- `189db802`：Pool=null 重置，Pool=ref/copy 对所有物理槽批量执行。
- `ce5da20b`：BitCastToFloat/EncodeFloat30/HashPointer/HashUnsigned32 移至 ResourceHash，隔离回归跟随真实代码位置更新。
- `af232a90`：提前纳入依赖修复，移除 9fbc28d0 中残留的 previous_uid/fifo_index_map 等已删除字段引用；账本标注提前应用，后续遇到该 SHA 不重复执行。

适配：空池 replacement 指针不下溢；RING 负索引用有符号数值取模，修复非 2 次幂池大小时的错误映射；拒绝 NaN/Inf 和 FIFO 保留哨兵键；物理遍历不改 FIFO 键映射。批量命令通过局部 destination 参数执行，不临时修改可复用命令对象。过期/重置清除 SourceStride 和缓存快照，GPU 分配缓存按上游设计保留供复用。

真实生产源码回归覆盖 keep-alive 的包含边界、过期置空与查询元数据、FIFO 替换/重置、物理遍历不污染索引、负 RING 索引、帧号回绕、空池和 eager 初始化。新增真实 HackerContext+WARP 的 ResourceCopyOperation 执行：所有槽引用同一源、所有槽完整复制且逐一 staging 回读 4096 字节一致、Pool=null 清空索引和状态、命令目的地不变，最终资源引用释放。

初版批量测试直接指定资源指针但漏了 device，生产 expire 正确进入跨设备复制，导致指针相等断言失败。现测试准备通过生产 SetResource 完成设备归属和引用计数设置；未改低断言标准。最终 DX11 DLL/测试目标构建通过，CTest 1/1（0.37 秒测试时间）、两份隔离脚本通过。

累计 28 项已实现、1 项合并 skipped、307 项 pending。过期配置的真实 INI 加载/重载、游戏内效果仍待验证。下一项 `3f79f697` 为 HackerInputLayout 包装，`af232a90` 已提前处理。

## 第九批

- `3f79f697`：新增 HackerInputLayout，接入设备创建、管线绑定和 FrameAnalysis 元数据缓存；源文件注册到 CMake，未引入上游 vcxproj。
- `b577dc92`：纯 pragma/格式与构造析构位置调整，记 skipped。移除 pragma/空白并扣除重定位的构造析构后，父/子版本 token 序列相同；没有遗漏功能改动。
- `3508283e`：复制分发拆分为 CopyResourceToResource/CopyResourceToPool，保留局部 destination 适配；上游此时引用尚不存在的 pool_size 和四参数 GetResource，本地使用当前真实字段与接口。
- `d977c10f`：FlatHashMap 后移删除，保留 const find_ptr 和最小容量修复。

InputLayout 适配：私有 QI 标识识别包装对象，拒绝空 QI 输出并清理失败输出；重绑先获取新引用再释放旧引用；原生布局直接传递，FrameAnalysis 使用 QI 检查后读取元数据；getter 核对实际原生绑定状态，ClearState/上下文销毁释放跟踪引用。增加虚析构以确保派生 FrameAnalysisContext 的清理链被执行。

WARP 测试编译真实 VS shader并创建原生布局，验证语义字符串深拷贝、IUnknown 身份、无效接口、同对象重复绑定、调用方释放后存活、ClearState 的包装/原生引用释放，以及绕过包装的原生绑定和清空。哈希表删除测试用强制碰撞并跨数组末尾的哈希器执行 2000 步，与 std::unordered_map 比较每一步删除结果、大小及所有候选键值；保留批量复制完整回读回归。

DX11 DLL/完整源码回归构建通过，CTest 1/1（0.23 秒），两份隔离回归通过。设备包装 CreateInputLayout 入口、deferred command list 状态恢复及真实 FrameAnalysis 转储仍待集成验证；原生回退对象没有包装元数据，未宣称覆盖这类转储。

累计 31 项已实现、2 项 skipped、303 项 pending。下一项 `9e5ab2c9` 是资源池功能重构，含 INI 变量池、表达式及范围操作，需按完整声明/解析/执行链适配。

## 第十批

`9e5ab2c9`（约 1200 行新增的功能重构）：INI 变量池、递归索引表达式、整池变量/资源操作、持久变量注册、pool ref/copy_desc/null、STATIC 索引和 FIFO 浮点位键。声明、解析、变量注册及执行链一并移植；旧过期和批量复制测试迁移到新真实接口，没有留下旧实现或取消断言。

逐文件三方合并保留原有修复，10 个 CommandList.cpp 冲突逐项处理，比较材料在 build/upstream/pool-v2。修复上游此提交的重复字段、未定义的后续 Layout 枚举项、缺失前向声明；动态索引表达式改用共享所有权以支持 ResourceCopyTarget 的值复制。保留局部 batch destination、范围检查、缓存 COW 等适配。

索引/生命周期适配：FIFO 使用显式 occupancy/update 标记，支持 UINT32_MAX 位模式的 NaN 键和实际 UINT_MAX 帧号；RING 保持负索引和非有限数检查。代理链完整解析并拒绝环，copy_desc 使用最终来源的描述；混合模式不抹掉另一类型的值；持久变量重置标脏。重复解析清理旧 target 状态，非池对象查询不再解引用空 pool。复制选项字符串由调用方持有，修正上游返回局部栈数组内部指针的问题。

回归通过真实指令解析与执行验证：递归 `$pool[$pool[...]]`、范围 RHS 只求值一次、持久变量注册/更新/重置、代理写入/多跳/解引用、描述复制、拒绝深拷贝、混合变量和资源。语义按本提交区分 `Pool=null`（索引/代理）与 `Pool[*]=null`（内容）；测试分别验证两条真实指令。迁移时旧测试发现非池 getter 空指针和 plain Pool=null 分发缺失，均已修正后重跑。

DX11 构建、完整 CTest 1/1（0.23 秒）及两份隔离回归通过，之前 WARP/InputLayout/哈希/整池复制回读测试保持。持久变量实际写盘/读取、真实 INI 选项加载与配置重载、目标游戏仍待验证；本项不标为完全 validated。

累计 32 项已实现、2 项 skipped、302 项 pending。下一项 `cc4a7ac2` 为 CustomResource::expire 整理，之后 `0b9e66bc` 撤回此前 Linux PNG 加载改动，需核对本地 COM 生命周期适配与色彩格式预期。

## 第十一批

- `cc4a7ac2`：ResetRuntimeState 抽出资源失效清理。额外清理 CPU 快照和查询元数据；跨设备转移成功时恢复仍有效的 stride/offset/size/format/source-stride，失败则不残留旧对象状态。
- `0b9e66bc`：按上游撤回 FORCE_SRGB，恢复 WIC_LOADER_DEFAULT。保留此前验证过的作用域 COM/factory 所有权，撤回仅改变纹理色彩格式选择。无 sRGB 元数据的测试 PNG 现在检查普通 RGBA/BGRA UNORM，仍逐通道回读像素并验证截图保存。
- `ac1ba594`：成员参数支持表达式。MemberArg 的表达式使用共享所有权以保持 target 可复制；成员描述结构仍在函数外。保留非有限/负数/整数越界验证。参数扫描使用配对外括号和嵌套分隔符，避免原 wcsrchr('(')/首个逗号策略无法解析嵌套算式的问题。

回归验证嵌套 `HashRegion(($offset+4)*2,(8+8))` 的实际字节区间和编码结果；重置清理及重复调用；两台真实 WARP 设备的跨设备资源转移，staging 回读全部 4096 字节与源一致，并检查转移后的元数据。保留原 WIC/COM/池/InputLayout/哈希回归。

DX11 构建、CTest 1/1（0.23 秒）及两份隔离测试通过。转移失败注入、持久化重载和目标游戏仍待验证。累计 35 项已实现、2 项 skipped、299 项 pending。下一项 `5dab0f1f` 是空间哈希计算，之后是 SPATIAL 池索引。

## 第十二批：并行移植与回归

`5dab0f1f`、`1cc9c0c6`：空间哈希和 SPATIAL 资源池索引。坐标参数按字节偏移读取，使用 memcpy 支持非对齐数据；验证有限坐标、正 cell size 和最后一个完整 float 的边界，原点哈希零作为有效值。移除上游只包含部分参数的结果缓存键，保留 CPU 快照复用。池查找使用环绕 Chebyshev 距离、显式占用标记、空位优先及 FIFO 淘汰。WARP 回归覆盖交换坐标/不同 cell size、非对齐自定义数据、无效范围及原点，另验证空间邻居/环绕/淘汰；构建和 CTest 通过。

`271e97df`、`e4ae63da`：64 位布局与签名哈希、语义和签名深拷贝、完整键碰撞复核、设备级布局缓存，以及布局覆盖的延迟应用/恢复。采用带共享状态的弱缓存，查找和最后 Release 在同一锁内串行，避免上游强缓存阻止原生设备归零。覆盖引用由上下文持有，保留原生/包装/空布局；ClearState、析构和延迟上下文状态转换清理引用。新增缓存并发和状态恢复的 WARP 测试。

真实 INI 持久化回归已接入：在 build/ 测试可执行文件旁独占创建配置，使用真实 LoadConfigFile、SavePersistentSettings、ReloadConfig 连续加载并检查持久变量所有者、混合池、代理、懒分配和重置。修复保存失败提前清 dirty 的问题，失败保留待保存状态，成功关闭文件才清除；打开失败恢复 locale。首轮集成 47/47 构建、CTest 1/1（0.58 秒）通过，两份隔离测试通过。

`76e6edad`、`de03a3c9`：静态变量求值和配置数值表达式，默认优化仍保留动态变量；支持空输出探测。保留 wcstof 下溢及有符号零，拒绝未消费字符的空串，整数饱和转换。新增失败测试揭示通用解析器对空 token 数组做越界迭代，修复后空白/空括号/嵌套空树均返回语法错误；同时修复旧 identifier 的 npos 越界访问。

`cde6bab4`：布局 ElementFormat/ElementOffset 指令，修正上游偏移边界误用枚举值的问题，使用私有 QI 识别布局元数据，在前置命令执行后和自定义 draw 前刷新。真实 WARP 测试覆盖格式缩小/增大、语义 index、不同 slot、APPEND、嵌套命令、创建失败、非法参数、原生/空布局和 post 列表及恢复。

保持上游设计边界：同一 draw 的多个独立前置列表各自从尚未绑定的原布局构造，后一次 defer 可能替换前一次变更；嵌套 RunLinked 共享 state 可合并，已测试。磁盘中途写失败未注入，打开失败重试已验证。

最终统一构建47/47通过，CTest 1/1（0.29秒）通过，包含全部追加断言，之前的空表达式崩溃已修复。两份隔离回归通过。累计42项 implemented-awaiting-validation、2项 skipped、292项 pending；目标游戏验证仍待进行，goal保持进行中。后续18项的逐提交审查见 DX11-Upstream-Next-Batch-Audit.md；下一依赖组是 `51a23856`/`da86b1b9` tokenizer 与二进制字面量，前者上游单独不可编译，需要共同适配但分别登记。

## 第十三批：表达式分词、二进制、时间与函数

逐提交移植 `51a23856`、`da86b1b9`、`d654cf76`、`6a77c5ac`、`db64f5b1`、`057b4bce`，保持 CMake/C++17。前两个提交共同编译，修复上游中间版本缺声明的依赖；账本分别记录。工作由分词回归、时间/锁定实现、函数回归与后续审计并行推进，主代理适配分词/函数并集成验证。

分词器按真实语法调度专用 operand parsers，保留原直接 parse 入口兼容现有调用方。保留配对括号、共享表达式所有权和空树保护；ASCII 数字判断另支持前导小数点。修复上游 Unicode/连字符路径命名空间和变量减法边界，支持带 resource 前缀的显式命名空间。wcstof 下溢保留有符号零；二进制 scanner 支持64位并拒绝65位/无数字，完整表达式和配置入口拒绝尾垃圾。

新增 frame_number 与显式 time/frame 静态快照，默认优化不冻结动态值。LOCKED 拒绝运行赋值，补齐 Store、Key/Preset/Cycle 和渐变写入旁路，保留声明初始化。persist+locked 由声明恢复，保存器不生成必然被锁拒绝的赋值；遗留锁定持久项拒绝覆写声明并仅清理一次，避免无限标脏。原持久池、注册数量、dirty 和连续重载断言全部保留。

新增上游实际实现的一元数学函数。countbits 对有限输入截断并模2^32，非有限返回0，定义原来未定义的转换边界。random 保留上游 hash/counter 公式，在DX11独占 helper中使用原子计数与一致 frame/tick 快照，避免共享 util 引入 DX11 G 依赖；对浮点舍入后的排除端点使用 nextafter 修正。默认静态求值/优化不折叠随机结果，显式快照仍允许；空输出探测不消耗随机调用。noise 只有上游词法名称、没有实现，按实际能力拒绝，不宣称可用。

新增真实生产回归覆盖64/65位二进制、Unicode显隐命名空间、递归池/成员实参、IEEE边界、全部数学函数、随机正负/最小subnormal/最大finite严格范围、并发发布seed与随机调用、锁定旁路及遗留持久项清理。初次测试发现并修复命名空间问题与锁定持久项反复标脏，全部原断言保留。最终 DLL/完整回归构建成功，CTest 1/1（0.30秒）通过，两份隔离回归通过；并发测试是行为覆盖，不宣称替代竞态检测器。未部署或启动目标游戏。

累计48项 implemented-awaiting-validation、2项 skipped、286项 pending。下一项 `4e624ddc` 是池 Size/Index 语义，随后为目标空格与参数 reader 依赖组。下一20项审查见 DX11-Upstream-Batch-Next20-Audit.md；各项审计不能替代移植和验证，goal继续。

## 第十四批：参数 reader、池别名与共享回读

逐项审查并适配 `4e624ddc`、`d76ef27c`、`296e988a`、`26312d6c`、`8c1d58b7`、`8f423e07`、`4041d689`、`d89560b7`、`37635f6e`，另完成 `7cf7c34d`、`4a6062d1`、`1b9df486` 共享回读依赖组。每项独立登记；296/263的中间terminated-helper能力由最终reader承接，376已在上游删除helper，不留下重复/无调用共享util代码。7cf析构依赖1b9的for_each，因此提前引入已审查helper；中间246的Store接入仍待处理，未视为完成。

参数 reader 持有自己的输入，支持token/argument模式、前后混合copy/ref选项、tab、嵌套表达式/成员。修正上游交叉括号、失败peek残留、试探错误残留和单未知/尾未知选项吞掉问题，任何无效指令均不发布半成品。保留shared_ptr表达式所有权、目标重复解析重置及配对外括号提取；新增测试确认带空格的显式命名空间仍作为完整target。移除全局空格拒绝后，现有回归发现scanf接受`vb 0`，将限制收敛到pipeline槽名称，括号内空格继续允许。

池支持 `Pool->Size`、`Pool[id]->Index`，ref解析把目标所需bind/misc标志传播到最终源池的已有和后建元素，保留代理环保护。新INI名 `pool_expiration_timeout_frames`、`pool_expiration_reset_elements` 优先，兼容旧名；新名存在但无效则使用新名默认值，不静默退回旧名。保留内部旧C++成员名及占用/回绕修复，仅不同步无语义pragma/搬表。

共享staging池按2次幂分桶、context持有并在既有虚析构中释放，调用方只借用。零/上溢请求拒绝、失败分配不缓存。哈希入口及FrameAnalysis/BeforeDraw/CommandList调用统一使用HackerContext；只复制源有效ByteWidth，映射数据在Unmap前复制到独立CPU快照，成功才发布。保留字节偏移、原点零值、快照COW和缓存失效修复。同步回读使用原生immediate接口避免Map hook重入，拒绝deferred及错误设备；CPU缓存已存在时仍可直接求哈希。

真实生产回归覆盖reader正常/边界/失败、池别名和代理标志、真实INI新旧/冲突/无效优先、WARP桶复用/析构/上下文设备隔离，以及4100/4104字节共享8192桶的内容与哈希、刷新、跨资源快照不串数据、错误设备/延迟回读失败后immediate重试。初次编译修复旧Store只读指针类型，原有所有断言保留。最终DLL/回归构建通过，CTest 1/1（0.34秒，总计0.50秒）通过，两份隔离回归通过。有效大小CreateBuffer内存耗尽、驱动Map真实失败未强制注入；未部署或运行目标游戏。

累计60项 implemented-awaiting-validation、2项 skipped、274项 pending。下一项 `246bd0a4` 是Store的reader/动态偏移/共享回读，需保留锁定和持久化语义，随后为CommandList回调代理等后续20项审查中的功能。Goal继续。

## 第十五批：Store、回调、计数与网格

逐项移植 `246bd0a4`、`be77b27e`、`76a41939`、`f8a50f93`、`609244f4`、`2c5b1354`、`5accb5a6`。Store在独立副本移植后仅按三处声明/方法锚点合并，避免与callback代理共同覆盖同一生产文件；测试、后续审计并行，主代理负责网格/枚举与整合。仍保持CMake/C++17。

Store保持历史与上游的底层buffer绝对float元素索引（分数截断），不是字节或CB/SRV view相对索引。只复制4字节到上下文共享bucket，检查finite/负数/物理ByteWidth、资源维度、immediate上下文及device一致性；失败不覆盖变量，资源和view均RAII释放，borrowed staging不Release。保留LOCKED双层拒绝，PERSIST按位变化时dirty|=1，不丢已存在的清理bit2。WARP验证动态/最后元素、同bucket复用、非零CB/SRV窗口仍绝对索引、NaN数据、引用平衡、错误设备及deferred不录制回读。

CommandList runtime callback支持ref/null、双phase转发和上游flatten root语义。严格拒绝未知/无效输入，预先检查pre/post两个根再原子提交，循环解除后同一命令可重试；clear重置proxy/runtime。空runtime callback的explicit/linked noop路径保留，填充后原链接实际执行。独立调用深度上限为256次实际调用（不再用缩进recursion充当深度），覆盖linked recursive=false路径并在退出时恢复。真实LoadConfig/ReloadConfig删除原source配置后确认无旧proxy、目标自身命令恢复运行。If/ShaderRegex调用点已适配并构建，未声称实际生成regex回调已实测。

Draw/dispatch计数从构造零开始，按context隔离，production Before*增长，被skip的尝试也计数；Present递增frame后调用生产ResetCallCounters。真实WARP draw/dispatch、skip、runtime/static/context缺失及reset回归通过；没有调用完整SwapChain Present，因此只确认其清零接线与实际reset方法，完整Present仍待目标环境验证。

枚举名先安静匹配，避免合法overrule触发数值表达式报错；boolean别名与integer/expression各保留原意义，2不被强制成true，超范围回默认。GetIniBoolOrInt继续服务Rendering的整数模式，亦修正此前通用bool表达式造成的模式折叠。真实配置在warnings启用时捕获日志，所有合法enum/boolean/int/表达式值无失败probe诊断；无效值默认与Rendering mode2均通过。609同时修复无空格time-1等减法，保留资源slot和namespace连字符。

空间网格改为4096×256×4096（12/8/12），大坐标安全取模同步使用各轴大小，不能继续先模1024。更新旧网格预期并验证1024新增范围、各轴wrap、负/大坐标、独立环距及UINT32_MAX位模式的NaN key。`03d56240`的上游CB /4 hunk不照搬：本地已统一字节语义，真实CB byte256窗口坐标返回准确packed值，等价证明原修复目的并避免回退；账本标skipped且保留证据。

最初集成发现counter空state被旧generic no-device分支提前返回0，已将counter NaN保护前置，原断言保留。另修集成时遗漏的旧bool/int helper声明及测试对private字段的直接访问，测试改走现有public setter。最终DLL/完整回归构建通过，CTest 1/1（0.36秒，总计0.58秒）通过，两份隔离回归通过。没有部署或启动目标游戏。

累计67项 implemented-awaiting-validation、3项 skipped、266项 pending。下一项 `ba335207` 是资源Region/CB窗口复制，随后是池cycle/read-expiration/LastFrame及reader LOCKED组。84之后20项审查已落到 DX11-Upstream-Batch-After84-Audit.md，持久化支线与主线交错须按拓扑逐项适配，不能盲套连续patch。Goal继续。

## 第十六批：Region、池生命周期与变量 reader

逐项适配 `ba335207`、`4f3722ca`、`ab7b929e`、`aa3cad5b`、`49596946`、`37a4bb26`、`ce1ceea4`、`71c46f54`、`84a7d071`；为当前空池/缺失对象失败路径提前引入已独立审查的 `bf3eddfb`、`b01bead3`。reader和pool代理在独立before/after副本生产修改，主代理只合入差异并保留Region新枚举/成员表，避免同时覆盖真实CommandList。保持CMake/C++17，新增真实测试均接入完整生产源码回归。

Region使用底层绝对byte offset/size，显式选择只求值一次，保留byte0而不被旧view偏移补全覆盖。复制和引用分别验证：copy必须在实际分配内，可从byte3/20等任意起点复制；CB ref遵守256byte first/count粒度及最大64KiB窗口，允许窗口超过物理buffer（原API语义）。copy始终绑定新buffer自身而不是误走SetConstantBuffers1，避免小于256B复制失效；自定义alias offset/size、CPU快照和哈希正确更新，缓存view不会沿用旧区域，batch保留局部destination。

CB窗口约束及合法out-of-allocation ref来自已核对的[Microsoft VSSetConstantBuffers1 文档](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-vssetconstantbuffers1)。WARP实际回查身份/First/Count，包括1792+512超过2048B源和最大65536B窗口；不宣称实测shader对补零区域的读取。

池SetSourcePool返回bool并按上游绑定时捕获已proxy源的root，使用迭代解析避免深递归。撤回早期本地自选的“始终保留中间proxy节点”差异，真实测试区分两种语义。循环拒绝不半提交，解除临时循环后同一指令可重试；不照搬上游中间sticky failure永久失效的问题（后续已审恢复修复也要求可重试）。

read-refresh默认false，配置pool_expiration_refresh_on_read启用后普通read和LastFrame read续期；copy_desc一并复制策略。保留普通懒读默认对象语义。LastFrame复用真实static/dynamic pool索引和SPATIAL邻近选择，修上游错读member_args[0]及keyword长度13（实际11）的错误。缺失查询不分配/evict/rekey或伪造时间；bool+unsigned内部接口和显式updated保留合法frame0/UINT_MAX，表达式缺失返回-1。禁过期RING也维护写时间，因此新增每槽timestamp跟踪成本；不把功能变化包装为性能提升。

变量reader只锁destination，locked source仍可读；nullptr scope查global，普通scope保留shadow。local必须完整token，空输入/尾垃圾/无效initializer安全拒绝并回滚未成功的声明；池变量交给已有目标parser。Store、Key cycle、Key/Preset调用方完整接入GetVariable(false)，保留runtime保护，真实INI多余尾词目标不影响合法cycle及persist/locked/reload验收。

缺custom对象的GetResource在expire前返回null并清view，Stride/Size提前返回-2，真实存在texture仍为-3。既有隔离fixture曾把custom owner=null与backing texture!=null组合成生产中不存在的状态，新null规则使该旧测试失败；修正为有效custom owner且清override元数据，保留原-3断言，并在真实WIC custom texture路径重复验证，未放宽断言或补造生产依赖。

最终完整DLL/回归构建通过，CTest 1/1（0.39秒，总计0.53秒）通过；全部新增Region/pool/reader/config/WIC断言已执行，两份隔离测试通过。未部署或启动目标游戏。累计78项 implemented-awaiting-validation、3项 skipped、255项 pending。下一批为84之后的reader/分词兼容组及持久化支线；需要保留本地所有权、失败重试、锁定与缓存修复，goal继续。

## 第十七批：UTF reader、持久变量保留与缺失键读取

逐项适配 `db810e55`、`43391f7b`、`9fc83bc0`、`f8e7118a`、`7d60f507`、`dd1a1a57`、`6d6dfb60`、`2d6af722`、`c05b4cae`、`bf929475`、`fdab51e2`、`4e7988e7`、`98d0d98d`、`326f64cf`、`0d93c362`。用户已明确授权子代理并行：reader/语法和持久化分文件实现，资源池在隔离副本移植后应用差异，新测试/审查独立执行，主代理统一接入CMake与集成验证。保持CMake/C++17。

Reader支持完整Argument模式，空格/Unicode namespace LHS精确查找，资源名恢复Unicode和历史hyphen行为；短slot后接member时交给完整target parser。保留括号栈、null安全、local声明失败rollback、LOCKED destination检查及source可读。namespace子目录含空格仍受上游9fc相同限制，本轮未扩语法。SyntaxTarget抽取仅复用member描述/参数解析，保留shared_ptr表达式、Region、最外层配对参数提取、范围校验与reset/null保护。

持久化采用最终分支的owning saved-values map，不沿用已被替换的多次磁盘reader中间方案。初载未知完整数字literal按实际user-config归属捕获；名称大小写正规化，未知非法literal/表达式不保存为0，LOCKED不序列化。System.auto_clear_persist_vars默认true：autosave不推进清理，第一次成功explicit reload arm，第二次先把旧unknown写入文件再清内存，后续save才删除文件中unknown。关闭选项保留unknown；live persist优先恢复；切换owner路径/wipe清旧值，规范化绝对路径避免等价路径重复初载。实际初载frame0且gConfigInitialized=false、停启include mod、两轮文件行为、普通/空格/Unicode namespace均通过真实Load/Save/Reload测试。最终c05在池变量早return之后才捕获，本轮冷启动未知池槽同上游未覆盖，不能把普通unknown测试算作此场景。

保存失败保留dirty、locale、map及清理阶段；reload保存失败保留当前live registry/pending供重试；wipe失败不先清内存，成功wipe跳过旧live值快照，防复活。打开失败、持有真实拒写/拒删句柄导致的reload/wipe失败及解除后重试均通过；未注入磁盘写入中途或fclose失败，代码仍检查ferror/fclose才提交。tracked Dependencies/d3dx.ini只插入最终System说明，保留原文件编码，不改部署配置。

FIFO/SPATIAL默认read miss返回template，不分配/淘汰/推进replacement；pool_allocate_slot_on_missing=true恢复旧读分配。write永不退到模板；空池、invalid/nonfinite RING仍失败。copy_desc复制新策略，proxy按resolved源策略。合法默认missing LastFrame为0，invalid/empty/未写existing为-1；raw bool查询失败不变out、不分配。SPATIAL近邻AssignSlot保留新key重绑，连续read/write cell0→1→2→3复用同槽和内容，并验证12/8/12 wrap、NaN bitkey、read-refresh和过期。真实parsed PoolVariableOperation与同一ResourceCopyTarget依次读missing/写入/evict后分别看到template、实际slot、template，防缓存旧模板。

恢复型cycle缓存仅抑制同root重复失败，root变化重试。callback同时跟踪pre/post两root及destination，保留双phase原子检查，post-only变化也可恢复；pool copy_desc/reset清除旧失败状态。真实parsed命令回归通过。shader后续24项审查在DX11-Upstream-After-Shader-Audit.md；隔离shader解析器准备产物尚未并入生产/计入本轮账本。

首轮构建发现新测试漏PackCellCoords真实extern，修正声明；审查发现变量命令需要HackerContext，测试改用真实WARP上下文。持久化新fixture首个整行断言曾因binary读取保留CRLF而失败，实际saved文件完整含预期值；改为标准文本读取正规化CRLF，保留所有变量/值断言。没有删断言或放宽生产验证。

最终DirectX11 DLL与完整生产回归构建通过，CTest 1/1（0.56秒，总计0.72秒）通过；现有两份隔离回归通过，git diff --check无错误。未部署或启动目标游戏。累计93项implemented-awaiting-validation、3项skipped、240项pending；下一项80481d19是shader声明读取/cache组，需处理并发、DXBC边界及getter安全，goal继续。

## 第十八批：shader声明、Structured Store、布局策略及语言文档

按账本处理下一组23项：`80481d19`、`1bf2d27e`、`403e3a86`、`ad264db8`、`24e2cff7`、`64bcd002`、`fdc2539c`、`b2e57f12`、`3572ec09`、`067f4a45`、`3f2f3700`、`3b53481c`、`c530d130`、`e8ea2075`、`6bd65ce5`、`ceede416`、`a9d7c89b`、`bb6dc33c`、`ba1fb5d6`、`2d9f7505`、`471bed39`、`0df206b6`、`9e75c140`。21项implemented-awaiting-validation；b2e/357临时禁用/精确恢复净差为空，分别skipped并记证据，没有先关闭本地功能再打开。上游tip仍eae473b3、336项，无新增。

Shader解析器采用最终独立ShaderBindings.cpp/h，CMake完整登记；Microsoft token头逐字节匹配fdc上游，SHA256为4fb5abdb76bc1c6d27be10379c82391cd8747820f484f4256f40e247ec4d6dd0，版权保留。逐读804/fdc/c530后直接落最终安全版本，memcpy有界读取避免unaligned，校验container/table/chunk区间和指令长度，局部结果成功后发布、失败清out。支持SM4.0/4.1/5.0的CB/SRV immediate声明；拒绝SM5.1/relative/ranged和API范围外slot。只是声明提取器，不验证checksum/所有opcode/控制流；允许声明container/program范围后的尾部，不称“输入全消费”。extension链实现尚无真实compiler fixture。

Cache在通用CreateShader原hash后、replacement前接入；两阶段std::mutex/lock_guard查与发布，解析锁外，所有getter同锁复制快照，避免unordered_map并发rehash与iterator越锁。std::mutex自动构造/析构，避免新增CRITICAL_SECTION遗漏生产初始化；cache不持COM引用，reload不清不可变声明。解析失败仍缓存全零，optional metadata bad_alloc被限制在cache内，不跳过native CreateShader（未注入allocation失败）。shader handle→hash及普通override查询另用G critical section复制值后解锁，再求动态member表达式，保留原signed-zero/filter_index语义。

ShaderTarget复用上批SyntaxTarget；resource slot优先、null/reset及shared member表达式保留。cb_type/cb_size/srv_type/srv_stride/srv_dimension合法索引先finite/非负/14或128容量检查再截断；fraction仍允许13.5→13。无声明type0/属性-1，无shader负零，缺mapping/cache正零，invalid slot在有cache时-1。mask两名称只有上游语法未实现求值，明确保留0而不声称mask可用。CB size是DXBC声明的已用16byte寄存器数，可能不同于reflection完整分配；RAW维度按上游BUFFEREX约定（reflection给BUFFER）。实际GS stream-output沿上游不入通用cache，缺记录安全返回正零，无assert。

真实六stage Compile/Create/Bind与D3DReflect对照通过，覆盖typed全部维度、RAW、structured stride12、静态/动态CB及b13/t127，SM4.0/4.1、每字节截断、unaligned、坏表/区间/长度/slot、复用out、failed cache、动态/nested/fraction/非有限member索引、nullcontext、普通shader过滤和GS SO。4线程32次实际CreateVS对两个不同CB尺寸hash从miss并行发布，快照完整且不泄漏可变cache引用。没有加载replacement文件或启动游戏，不能把原hash接线验证扩大为不同replacement实际运行验证。

Structured Store按实际StructureByteStride向下/向上对齐完整结构复制，再从内部value_offset取float，保留底层绝对float索引、物理界限、同设备/立即context、LOCKED、RAII、失败不改值和bitwise dirty。真实WARP stride12/16首中末、动态index、非0 SRV窗口、越界/非有限、更新和复用均通过，实际debug layer InfoQueue无ERROR/CORRUPTION。新RAW fixture最初FirstElement1被实际debug诊断为未16byte对齐，改为8float backing中FirstElement4的合法非0窗口，保留窗口外absolute index0/末index7/解绑失败断言，生产未因此放宽。

游戏CreateInputLayout改为不复用wrapper；自定义布局走CreateCustomInputLayout，保留本地full-key/weak-entry cache和COM修复。实际测试区分重复game wrapper、custom缓存命中、game不命中custom、native validation-only HRESULT、bind/get与现有override/restore。原生driver可能复用底层对象，不把wrapper分开等同底层对象唯一。RAII CriticalSectionGuard修LINE两层展开，同scope双guard、递归、return/throw释放后另一线程TryEnter均通过；未启用process hook/TLS的lock-dependency诊断模式。QI失败trace改Debug，控制流/HRESULT不变，未做专门日志级别capture。

Legacy copy source只在特定GetTarget(validate=false)入口恢复operator字符资源名，表达式保持strict；真实registry/command parser验证+*|>=名称、prefix/suffix选项、未知/重复source及括号失败。noise未实现token删除，random动态不变。Pool=null和前导.5已经有真实等价实现，引用旧/新回归，不重复改生产。

导入最终结构的[DX11语言文档](ini-language-extensions/README.md)，22页。逐项保留五个doc提交效果，修正binary64/float24、random碰撞、hash非唯一、无实测性能数字、Region byte/CB窗口、Store/持久化/缓存策略；修死链接、大小写、错误hash和剩余围栏。root复核shader页补负零/正零及CB实际used-register语义。`python build/upstream/check-ini-docs.py`通过：22文件、36相对链接，大小写/锚点/fence零错误；未执行完整游戏示例或校验外部URL。

最终完整DLL/生产回归构建通过，CTest1/1（0.59秒，总0.76秒）通过，两份隔离测试通过。首次store新test的Windows max宏编译错误已改为(max)()，未运行旧exe；RAW失败是实际非法fixture，按debug证据修正并重跑。未部署。累计114项implemented-awaiting-validation、5项skipped、217项pending。下一项e850bd7b开始微秒TIME/FRAME_TIME/FPS/空间半径组，后25项已审查于DX11-Upstream-After-Language-Docs-Audit.md，含一个非空PNG merge resolution，goal继续。

## 第十九批：时间/FPS、资源属性与 PNG 分支

按拓扑处理下一组25项，首e850bd7b、末b3a309b7。16项适配；7个空remerge的merge、b7c2e77b纯NULL写法和393b396f纯注释共9项skipped，各项完整SHA、文件、理由和证据均在账本。root重新运行全部8个merge的remerge检查：80edf0a3/7576bdab/26ec4b1a/3c8d8f18/19664553/a3f014e5/b3a309b7零字节；d1deb9d2非空9718字节，单独承接GetWICFlags、override_color_space metadata和保留新pool实现，不把merge全部视为无改动。上游tip仍eae473b3，共336项，无新增。

时间四项e850/474/be6/ca4采用DX11自有FrameTiming.cpp/h，C++17 steady_clock微秒tick，真实Globals constructor把previous设launch，避免首帧uptime。Present原入口调用UpdateFrameTiming发布TIME/FRAME_TIME/EMA FPS，64bit tick读写完整接线，double求秒后仍返回float；不声称长期float具微秒数值精度。FPS保留首调用baseline、零delta/长暂停忽略及EMA帧耗时平均，倒退tick不会unsigned下溢。FRAME_TIME支持显式snapshot而默认动态，FPS仍runtime-only，沿上游无scope限制；本地随机数独立同步状态未回退，仅取64tick低32位seed。共享util/DX12未改。

空间重用使用统一GetSpatialReuseRadius：reference120FPS，有限安全计算后ceil，12/8/12实际Chebyshev上限2048，非法/非正frame interval为0。保留上游有界float四舍五入，避免1/120浮点转纯double后把2误ceil3；60/120/240实际阈值4/2/1通过。GetElementIndex、LastFrame和missing-template三处同规则，连续重绑/read miss不分配不回退。旧spatial fixture明确设置并恢复1/120基准，未偷改生产zero-frame默认；新阈值测试每轮从相同原key重建，因为之前60FPS读取会重绑定，不能把此后的exact hit当阈值比较。

4cd跨namespace规则在队列和runtime标志修改之前拒绝：有source且目的namespaced list任一pre/post非空时，caller必须同namespace。空callback跨namespace可用，null清除仍允许，循环检测/双phase原子性保留。真实include INI验证同namespace/外部/global caller、pre/post-only、旧引用不变、empty和null路径。root新增fixture最初直接传nullptr namespace导致既有FindExplicit路径崩溃；日志定位后改为实际INI调用约定的有效空wstring，未为无效fixture改生产helper。崩溃残留的自建build配置核对内容和绝对路径后清理；临时trace已从测试删除，最终无残留d3dx.ini。

资源五项9a/b0/862/158/777重构file-scope ViewInfo/GetViewInfo/GetTextureFormat，只在真正buffer view读取First/Num，generic custom view用安全QI，字节乘法64位并受物理界限约束。新format/width/height位0x20000/40000/80000不撞Region/pool/variable/layout；格式优先override/cached→view→native，extent返回整个1D/2D/3D资源而非mip/slice，1D height1，nontexture -4/missing -2。DXGI_FORMAT_*需完整12字符前缀和完整合法名称，UNKNOWN合法，错误名称不落0。最小GetResource输出保留IB stride必要format和Region/spatial界限参数，ComPtr/QI引用平衡。

真实WARP覆盖数组/mip/SRV/RTV/UAV/DSV typed/typeless、尺寸/格式优先、missing/buffer、enum静态与INI大小写、ref平衡及CB窗口。新fixture误把普通ref发布size0期待成128字段、又期待普通ref传Region窗口；按实际契约修正为size0+真实size getter128，显式Region的First/Count断言保留。额外真实copy+staging验证UINT_MAX元数据受物理128限制且offset12后112个完整stride字节正确读取；原stride8截完整元素语义保留，不用改生产来满足错误fixture。既有Region byte0/绝对offset/合法超物理CB ref测试完整通过。

PNG/color适配bfbd/a2/863/d1/311：color_space大小写正规化，srgb/linear/default配置、日志c_str，metadata模板/懒池/copy_desc传播。WIC显式force/ignore优先，自动IDAT前sRGB安全扫描，其余WIC_DEFAULT；DDS只forceSRGB，不承诺linear去除自带sRGB格式。DX11 PngColorSpace.cpp/h用RAII FILE、完整读、31bit/实际长度约束、64bit绝对seek；缺文件/短读/溢出正常返回，无fclose泄漏。扫描不验证CRC checksum或完整PNG，不从iCCP推断。共享DirectXTK/COM factory和已有池实现未覆盖。

实际PNG/DDS format与原始像素回读、plain/tagged/explicit覆盖、非法配置、metadata/池copy_desc、缺文件/畸形、重复fresh/STA/MTA COM生命周期均通过。short signature及全部13字节sRGB块截断（含仅缺最后CRC字节）通过。直接parser用例原大写pool名改为正常INI预正规化约定；完整配置大小写覆盖仍保留。首次新test编译因Windows far/byte宏，改distant_key/sample_byte，未运行旧exe。

6c9时间/参数/callback文档按本地浮点与counter契约适配，并补资源属性、空间缩放及texture加载说明。精确大小写/锚点/fence检查：23页、37相对链接、零错误；完整游戏示例及外部URL未执行。独立只读复核在DX11-PNG-Timing-Integration-Review.md和DX11-Resource-Properties-Review.md；后25项审查在DX11-Upstream-After-PNG-Audit.md，已标enum初始化、默认检测gate、日志早期解析/共享DX12边界及输入gate/HOLD语义。

最终DLL/完整生产回归构建通过，CTest1/1（0.76秒，总0.91秒）通过；两份隔离回归通过，git diff --check无错误。完整swap-chain Present/目标游戏未执行。累计130项implemented-awaiting-validation、14项skipped、192项pending。下一项eb80c3e6之后是色彩enum/可选检测、日志和输入组，goal继续。

## 第二十批：可选色彩检测、日志等级和输入控制

按拓扑处理eb80c3e6至79b4d29f的25项，11项适配、14项skipped。跳过两项纯格式及12个merge：root逐个重跑remerge，11个零字节；9c9db1b3非空1006字节仅FindTextureOverrides注释dash/空格解决，保留本地ASCII，无执行差异。父功能分开承接，不重复覆盖当前资源、shader cache、Store和COM修复。上游仍eae473b3/336项，无新增。

色彩e321/1753/d492保留先前DX11安全PngHasSrgbChunk，完成CustomColorSpace enum及构造DEFAULT初始化（补上游遗漏），metadata/懒池/copy_desc/旧PNG测试完整迁移。System.force_detect_color_space默认false，ctor和每次Load/Reload赋值；只gate额外scan，WIC_DEFAULT仍让系统decoder处理元数据，显式srgb/linear优先。生产调用同一公开const GetWICFlags，真实flags验证tagged PNG关闭→DEFAULT、开启→FORCE，避免两种最终format都sRGB造成假通过；WARP像素/COM、配置大小写/invalid/absent及五次reload通过。

4867移除确无消费端gLogInput及模板选项。440日志引入DX11-only LogVerbosity/Logging.cpp/h，shared log.h仅MIGOTO_DX==11走等级宏，else保留DX12原FILE/gLogDebug行为。新默认disabled，warning/info/debug层级与legacy calls/debug保持上游；有意义非空新值优先，空/缺失走legacy，非空invalid在本提交范围disabled。root先前临时指定显式空disabled不符合440的buffer[0]规则，已恢复上游行为，而非保留无必要策略差异。InitializeLogging从当前文件直接读取，不访问未更新的ini_sections；既有LogFile复用时等级立即变，空FILE/打开失败安全。所有窄/宽/va_list gate同时检查FILE和等级。

Overlay文件输出不依赖show_warnings屏幕开关；窄/宽文件与屏幕参数分别va_copy。审查又发现early语法warning沿用旧GShowWarnings，main/profile均在ParseIniFile前通过同源动态文本+真实ParseBoolValue设新开关（含合法bool/静态表达式），再保留普通加载读取。新增同锁notice消息快照用于实测排队，不伪造UI依赖。实际main首load/reload旧true→新false不入队且文件有warning，profile双向true/false/表达式的唯一syntax marker入队/不入队通过。主main show=true畸形语法会真实Beep，为不干扰桌面未执行该提示音分支；不称屏幕/音频已验证。

新Logging.h最初把SDK包含放在using namespace std之后触发std::byte歧义，已Windows头优先，完整构建修复。日志capture的wide stdio实际含UTF16LE片段，测试精确识别ASCII/UTF16LE标记而不改变生产输出编码或删除warning断言。旧enum capture改显式debug并要求正标记存在，防等级关闭后“没有失败日志”假通过。真实首load/Reload/legacy/empty/invalid/nullFILE/打开失败/窄宽va_list/overlay格式与profile解析通过。

a24清除DestroyDLL loader-lock路径LogInfo/SavePersistentSettings/fclose，安全点/显式/重载事务保存保留。测试直接执行真实hook体确认dirty不被清、FILE仍有效，但未操作系统实际加载卸载DLL，不能宣称loader-lock实测完成。

ddd/f59组合承接最终语义：仅HOLD且!active保存原值，重复Down仍Activate(false)执行pre及排队post，不保留中间所有Activate的active早退。bb2显式enabled_when_disabled分类、调用前gate，不猜vector第一项；缺失/非法/非首toggle不漏普通action。首次disable读取标志和runtime状态分开，reload保留切换，foreground/XInput采样入口保留，InputButton虚析构支持正确owned backend清理。真实registry/action/保存/transition/queued lists配内存button验证，不发桌面按键。

重复HOLD Down/Down/Up/Up保存原值一次且pre/post副作用正确，TOGGLE/preset/cycle/LOCKED保留；追加active→disable→backend释放→gate零poll且不恢复→reenable→真实Up仅恢复一次的组合验收。Repeat/Delayed只验证gate暂停/恢复（0-delay样例），未把它当正延迟时间算法、真实OS采样或前台游戏验证。

c4空括号崩溃在本地提前由null/singleton guard及finalise空错误覆盖；保留更强边界与真实空/嵌套负例。661移植forward iterator/std::next分组、删除reverse/goto，所有现有函数参数/namespace/资源嵌套解析回归通过。新增文档描述当前日志、Input和可选色彩开关，不宣称完整示例已在游戏执行；25页/39相对链接、大小写/锚点/fence零错误。

最终DX11 DLL与回归构建通过。为保证新HOLD组合用例未受并行编译时间影响，root在代理构建结束后更新该test时间并单独增量重编，完整CTest1/1（1.31秒，总1.48秒）通过。共享头实际DX12兼容构建9/9成功并链接d3d12.dll；未部署。两份隔离回归通过，git diff --check无错误，无遗留fixture d3dx.ini。独立审查记录Log-Level和Input-Toggle-Hold，后25项After-Input-Toggle审查已列初始化/ASCII Unicode边界/持久化新支线适配。

累计141项implemented-awaiting-validation、28项skipped、167项pending，合计336；goal继续。下一项48c24之后包含Input范围迁移、日志fallback及初始化/表达式优化、持久化后续，不能用本地安全适配代替后续真实语义。

## 第二十一批：Input scope、初始化、持久化与表达式优化（25项）

连续处理 `48c24f8d` 至 `2806c7a5`，18项适配、6项无独有解决的merge跳过、1项纯格式提交跳过。上游检查仍为 eae473b3、336项，无新增。完整逐项记录在JSON账本，merge父关系/字节证据与后续25项审查见 DX11-Upstream-Input-Scope-Integration-Review.md。

310将输入配置迁入[Input]，toggle显式NONE、递归include实际来源MODS、普通工具/直接include为ALL；按实际文件路径、Windows大小写与目录边界记录来源，namespace别名不参与分类，cycle Back同scope。初次缺省off是本提交的上游语义，模板仍on；下一批45a会再改缺省，不能提前混淆。reload可改mode而保留runtime开关，独立initialized标志不依赖enum哨兵。真实fixture覆盖直接include位于Mods目录、相邻ModsOther、嵌套递归/显式namespace及注册数量；memory button替代硬件采样，实际registry/dispatch/action执行。

f8b适配完整初始化发布：生产InitializationGate覆盖配置与native导出解析，其他线程等待、同线程递归失败，三处criticalsection通过InitOnce初始化。必要两个创建导出先验证、缺失释放本次库引用且不发布指针；target mismatch仍nativeforward。独立配置checkpoint阻止导出失败重试重复注册旧绑定：静态复核查出原方案会再次LoadConfigFile，旧action持有指向已清global的变量指针，另有decompiler列表重复append；现已只重试库解析。配置解析异常明确进入FAILED并拒绝重放部分配置，不伪称完整回滚。真实helper八线程/失败/异常/递归与系统d3d11/kernel32导出表测试通过；整个DLL/InitOnce实际hook首调用及异常注入未执行。详见 DX11-Initialization-Review.md。

82删除旧profile加载定义声明及未编译nvprofile里唯一依赖的废弃rundll入口，不引入NVAPI目标。a756非空invalid log_level改回legacy calls/debug，显式disabled仍优先关闭；启动banner/path提升warning。日志回归迁到真实Load/Reload，保留全部等级/va_list/文件/queue正负断言；show=true用干净配置后实际显式overlay warning，show=false仍真实badsyntax验证早读开关，未运行parser beep。390提取ParsePersistentSettings，保持user最后覆盖及owner捕获。

79fd/efb/dbf/8c接未知池槽捕获、真实unknown数量、逐项通知和exact-set清理。保留路径所有权、完整literal验证、LOCKED排除、dirty位及保存失败重试事务。autosave不推进清理，explicit reload确认相同集合后只在写入/关闭成功时提交删除；相同数量不同keys不误清。旧测试按第二次成功reload当次已清理迁移，后续保存须真正改变变量，不以clean autosave写默认值为假预期。写入仍直接覆盖文件，不称原子文件替换。

新回归确实发现并修复lazy池问题：Constants创建/恢复slot之前按globals表统计会把已声明PERSIST槽误算unknown。现在只读pool声明/物理slot/持久标志分类、不提前分配；reload通知确认移至Constants之后的短临界区。此为下一批015时序的必要局部提前承接，015其余hash/append/事务仍pending。真实Load前后断言确认分类不提前注册lazy变量，禁用mod再启用恢复、count变化、路径切换、wipe与锁文件save失败保留binding/OverrideSave/pending全部通过。

7ea改Constants稳定压缩，普通命令顺序不变，坏/重复/失败global在首pass诊断后消费；这是上游诊断行为变化。d893/0271缓存If static结果并按weak root与optimise变化失效；1ff十二类operator mask跳过无关pass；87直接DFS tokens/lhs/rhs；ec9/5c77提供头内可达ASCII helper，保留Unicode namespace/公共uppercase调用/显式qualified路径。新测试最初误将reseed当作重置随机counter，已依据真实算法改为只读实际sample counter前后相等，保留动态性断言，未改变随机语义。

另一失败来自真实UCRT tmpfile混合ASCII与UTF16LE：实际4696字节证据三个完整正标记在UTF16LE偏移3442/3696/4194而ASCII找不到；capture现在完整匹配两种编码，全部正向断言保留，临时dump代码已移除。原始诊断证据仅留build。

2806文档补DXGI literals、资源属性与实际color_space选项页，保留binary64/float24/random碰撞/Region/WIC/DDS边界。26页43相对链接、精确大小写/锚点/fence零错误；示例未在游戏执行。未测量优化性能，不声明提速百分比。

最终统一MSVC/CMake C++17 DLL与完整生产回归构建4/4通过（此前完整83/83构建亦通过），完整CTest1/1，test1.41秒/total1.56秒。已读取 build/upstream/persist-unknown-integrated-build.log、persist-unknown-integrated-ctest.log及完整LastTest证据；所有既有及四个新入口均通过。两份隔离资源脚本通过，DX12兼容目标成功且本批无共享变更、ninja无待编项；未部署/启动游戏。无遗留fixture d3dx.ini。

累计159项implemented-awaiting-validation、35项skipped、142项pending，共336项，已处理194项。目标游戏、硬件输入/overlay/audio及完整DLL初始化异常管线仍待相应环境验证，goal保持active。下一连续25项从45a8ff4e开始，包含输入缺省on、未知配置hash/append与nonpersist校验、新资源属性及INI指针/解析次序优化，不能以本批提前覆盖局部为由跳过审查。

## 第二十二批：持久化 V2、资源属性与 INI 解析（25项）

连续处理 `45a8ff4e` 至 `8d4d2cb3`，21项适配、4项无独有解决的merge跳过。上游检查仍为eae473b3、336项，无新增。resource两项merge父表及空diff证据在build/upstream/resource-metadata-v2-port/NOTES.md，INI两项在ini-parser-v2-port/README.txt；逐项适配与验证在JSON账本。

45a修正缺失[Input]/input默认enabled，4e模板改input=1；runtime toggle仍只首次读、reload改scope不改开关。原scope测试需在新的enabled默认下先用真实ToggleInput建立off，再保留双向reload强断言，另补fresh numeric0；不能沿用旧off前提误报生产回退。

4bd/13f/c067/015/60f接持久化V2：仅读clear_unknown_settings（代码缺省1，模板0），不留旧key隐式别名；旧字段/fixture引用已迁移。拒绝失persist普通/池变量的旧user赋值，保留合法literal为unknown以待mod再次识别，LOCKED仍排除。unknown按owner内名字+float位模式完整映射比较，承接上游包含值的hash意图且避免碰撞；Constants后确认，同key改值/±0重获宽限。保留本地dirty retry bit而不照搬13f曾覆盖已有dirty的中间bug，validunknown不再强迫dirty2写盘。

015的rewrite+append改为一次known+retainedunknown合并事务：同目录CREATE_NEW/既有安全属性的temp，检查ferror/fclose后原子替换，成功才提交values/清dirty。失败只处理自己temp，不预删原文件。预保存失败保旧registry/bindings/OverrideSave，晚cleanup失败保新加载registry+pending/dirty，不伪称整个Load回滚。关闭cleanup时未变collection且known clean跳IO，独立review另发现unknown从非空变空也应skip，已加条件与真实mtime/binary断言。真实目标file-share锁分别触发前后两阶段替换失败，原文件完整字节不变，释放后retry成功。详见DX11-Persistence-V2-Review.md。

fa3共享工具新增exclusive nonconcurrent append，OPEN_ALWAYS、64bit seek、_O_APPEND保障seek后仍尾写，binary/text显式保留；新增同安全属性CREATE_NEW接口服务事务，w/wb/wt以外mode在创建前拒绝，捕获Windows错误先于LocalFree以映射EEXIST。独立review提前发现上游新增LogWarning在共享DX12宏分支未定义，已局部#if保留DX12原LogInfo；未扩大全局日志接口。真实file regression覆盖append创建/旧字节/seek/CRLF/共享失败重试、exclusive碰撞不截断、非法mode不改/不创建及原w截断。

13cc至849最终资源组新增Array/Mips/BindFlags及D3D11_BIND literals，分配空闲bit20/21/22，mask007e01ff，保留Region/Pool/Variable/Layout既有位。suffix按长度排序，BindFlags/LastFrame均11。custom Array/Mips默认-1走实际资源、显式0也优先；Array只1D/2D，3D按上游-4；Mips含3D，查询完整allocation而不是SRV窗口。BindFlags custom读已存metadata，native读physicaldesc，missing0且不用不存在的D3D11_BIND_NONE。GetResource只取必要输出并RAII释放，WARP 1D/2D/3D、12face cube、受限SRV、buffer/CB窗口、覆盖/失败及128轮COM balance通过。812原完整12位DXGI前缀已等价，新边界测试保留。

资源测试初次直接将uppercase传内部expression/target API，绕过生产INI先lower的既有契约。未为测试擅改词法；helper调用按相同归一化，新增真实LoadConfig Constants中uppercase D3D11_BIND组合与DXGI两种值验证用户语法，全部原static/value/负例/COM断言保留。编译阶段同时修正测试用CommandListOperand需pos构造、forward_list无size的标准接口错误。

f9优先用balanced resource scanner处理$pool candidates，支持Unicode/空格/hyphen namespace与嵌套index，temporary operand失败后仍有普通$pool前缀global回退；保留$a-1、坏括号安全。069/8d156直接传稳定map section节点，拒绝/空/重复header先清pointer，跨file不误附上一section，保留input实际来源及user最后覆盖。8d4 include长度分派保留recursive_root、seen/exclude和owner。e624解析次序改变量/flow/general/IniParam/resource；If/ElseIf先parse再构造注册branch lists，修失败probe留下裸指针问题，保留local rollback。真实完整load/include/namespace/scope、unknownpool保存、local/If执行与失败注册集断言通过。

旧unknown pool测试一项假设每次reload必将原user文本重写成带空格输出；真实诊断count1/dirty0/known_b22正确，原$unknown_a=11仍在。按015 skip-IO目标迁为完整原文件/数量/值强断言，并保留下一轮清理验证，没有强迫多余I/O来让旧格式假设通过。

96文档补BindFlags/Array/Mips/literals并修links、实际新key/默认，保留binary64/float24/Region/色彩边界。26页45相对链接、大小写/anchor/fence零错；未在游戏执行示例或测量提速。两份隔离资源脚本通过，diffcheck无错误，无fixture d3dx.ini残留。

最终DX11完整构建86/86及后续增量通过，完整CTest1/1，test1.57秒/total1.58秒；已读build/upstream/persist-v2-integrated-{build,ctest,LastTest}.log。共享util影响的DX12真实重编/链接9/9、exit0通过，build-dx12-compat.log；未部署。累计180项implemented-awaiting-validation、39项skipped、117项pending，共336项，已处理219项。目标游戏/硬件input/完整loader/deferred concurrency仍待验证，goal保持active。

下一25项6bf6a746至034e2875已完整审查，见DX11-Upstream-After-Metadata-V2-Audit.md。涉及pool类型范围、引用flags传播、profiler、slot batching、TO候选缓存及版本生成；审查已发现实际执行顺序/共享sink/分辨率失效等风险，需适配及回归，不能照搬性能标题或将文档描述当作实现。

## 第二十三批：池过滤、flags 传播、SRV 批处理、候选缓存与版本（25项）

连续处理6bf6a746至034e2875，15项适配，7项空merge、1项重复fuzzy补丁、2项互相撤销文档共10项跳过。上游仍eae473b3/336项，无新增。累计195项implemented-awaiting-validation、49项skipped、92项pending，已处理244/336；游戏验证未执行，goal继续。

f02/4b池范围区分resource/variable，变量fullrange新用0x00800000避免与本地property/Region/Layout冲突，Mixed过滤保另一类型，LOCKED/dirty/proxy保持，资源RHS非法不再当变量回退。逻辑null保留native缓存而不暴露给读取，清region/CPU metadata；测试初稿错误期望销毁native缓存，已按实际Getter/null契约增强断言。038记录ref图，清owner前清edges、配置全解析后固定点传播，覆盖逆序多hop/环/pool现有/懒元素和真实WIC。AddFlags及整pool先验证后提交，CB冲突不留半修改且保诊断；misc归一化按实际变化收敛。5aa复用已解析destination记录物理/需求flags，开analysis不额外消耗动态random索引；fixture由不存在的bare random改为真实random(1000)。隔离脚本同步抽取真实validation helper，两份隔离回归通过。

9f/c3/c5/ef批处理覆盖六stage静态SRV全部既有copy选项，按原INI执行顺序处理，修上游sorted跨gap重排；dynamic pool/viewport作barrier仍照常执行。明确per-call参数+ComPtr deferred payload，禁止共享operation指向栈。成功prefix异常时flush再throw，未注入bad_alloc，不宣称已实测异常。独立review发现alwaysGet令常见2bind仍2calls，改assigned-mask lazyseed：连续成功0Get/1Set、全skip0/0，有洞/失败才Get保旧view，重复成功后skip不丢新view。真实WARP API计数/身份/数据覆盖0与127/gap/dup/unlessnull/ref/copy/mono/copy_desc/raw/no_view_cache/MSAA resolve/cache复用/静动态pool/barrier；计数仅batch helpers，不冒称包含普通barrier或测试检查API。overwrite原本未实现，仍reject。c6文档被dc2完全撤销，不恢复无对应实现的If分支批处理声明。

9b与6cbd同patch-id069d73aa4845a6d0abd348236ddfde4508abd99f，统计只接一次。a6 collection用shared ownership identity/weak stamps避免有限epoch回绕；mode原子读写，start/end绑定进入时mode与window，clear退休旧window，注册/更新G锁同步且不把整个通用GPU命令放锁里。summary完整分候选rebuild/lookup，hits/count及clear都有实际断言；两个时间行嵌套不能相加。旧其它裸overhead全链并发未迁移或验证。

fb2/76/034候选缓存保当前修订的无handle fallback，hash有效位支持0/MAX，draw_info每次过滤，动态resolution matcher每次复判并把resolution纳入失效key。G→Resources统一lookup/invalidate，清配置maps前invalidate，资源AddRef保护handle；GetDesc/GetType不在Resources锁，四创建路径Tracker安装移到Resources外以避免COM锁倒序。实际filter/CheckTO/UAV/CopyResource消费裸override也持同G锁，后两者只在锁内复制整数再锁外GPU调用。真实WARP+事件/TryEnter握手验证filter ±0/priority、CheckTO pre/post与子列表寿命、UAV FirstElement及Copy读回，并发deferred context lookup+重建/真实Reload通过。

aba的buffer contamination早退回归发现独立真实缓存缺口：同帧写仅清L1/L2，L3旧结果继续命中。现连L3一同失效，其find/insert/clear使用独立mutex，初始read复制后放锁再进G，不跨COM/GPU/其它锁。实值tracking1/updates0时CRC dd54c4ab→c4b224f9，保非零/变化强断言；warm CPU/L2后并发2000read与1000clear通过。不能通过关buffer追踪让早退测试过。

d92版本生成走CMake目标和build内header：三/四段tag、距tag提交数、builder/commit/dirty、16位bounds，缺Git/tag/archive有1.3.16.0回退，dirty tag不标release，remote去userinfo/query/fragment。锁内比较+原子发布保mtime并发完整；RC OBJECT_DEPENDS及C++依赖齐备。DX11使用ProjectBunny DX11(XXMI/3Dmigoto)身份、DLL类型/PrivateBuild资源，MIGOTO12仍原3Dmigoto1.3.16；没有给本地不存在的.github添加上游workflow。新增独立legacy/version generator CTest及真实Windows版本API读取DLL四段/字符串/flag，6并发生成、无Git/tag、escape、越界/非ancestor拒绝均通过。Python测试改用relative_to兼容3.8。

最终DLL/完整生产回归构建通过，CTest3/3（legacy0.02秒、generator11.32秒、主回归1.67秒，总13.17秒），证据cache-batch-integrated-{build,ctest,LastTest}.log；共享version条件分流下真实DX12重编/链接9/9通过，cache-batch-integrated-dx12-build.log。diffcheck、两份隔离资源回归及INI文档检查通过，未部署/启动游戏。新consumer fixture误用GeneralCommands解析赋值已迁真正VariableAssignment入口，所有断言保留。

用户要求本goal直接向main建立阶段提交和推送；本轮起将验证后的累计检查点及时备份，不再等整个336项完成。推送前核对远端、保留其提交，禁止force push。此前未形成逐批Git提交，当前保存真实累计快照，不伪造已不存在的阶段历史。

下一25项48f3b6db至ac9947bf已逐项审查，见DX11-Upstream-After-Cache-Batch-Audit.md。包括无handle不匹配的新语义、动态slot/ranges、CB窗口/physical pool index；发现上游NaN整数转换、SRV/UAV访问14元素CB数组越界及无cachedview丢buffer metadata等问题，需适配和实际回归。

## 第二十四批：动态槽位、范围复制与未跟踪资源（25项）

按账本顺序处理 `48f3b6db` 至 `ac9947bf`：19项实现，5项无独有解决的merge和1项本地不存在的GitHub workflow更新跳过。保持DX11 CMake/C++17；每项适配和验证记录在JSON账本。上游仍为eae473b3、336项。

c137删除未跟踪资源的普通/fuzzy候选回退，不清空调用方已有matches；独立region exact路径仍保留，真实tracker卸载后的暖L3只保region exact。保留本地0/MAX有效位、COM活期、G→Resources锁序和缓存失效。回归覆盖真实tracker卸载/销毁、自定义资源引用、两个真实buffer区域和候选计数。

a4/84接运行时动态槽位及旧静态batch屏障，包含常量方括号表达式。非法源按null传播，普通赋值会清目标，unless_null保目标；非法目标不改绑定。finite/截断前边界检查避免NaN和越界转换，真实WARP覆盖六阶段SRV/CB及VB/RT/SO/pixel和compute UAV的首末槽、动态重用和失败路径。

b779至ac范围组支持包含端点的表达式边界、物理pool索引、当前上游的单侧继承和普通copy/ref选项。double检查后归一化环绕，超大相等pool端点仍计1项；截断后计数另受capacity限制。128容量的ComPtr数组替代上游14项CB数组被SRV/UAV路径访问的越界。每次调用拥有target和deferred sink，短mutex独占借用缓存，不在native/G调用时持cache锁；异常保留成功prefix、旧holes和尾部，缩进及COM引用通过真实虚拟日志抛错验证。视图key包含resource/stride/offset/size/format/options，验证首次无view的typed/structured/raw创建、metadata变化、原SRV子视图与no_view_cache真实释放。

范围fetch复用真实scalar复制管线，更新CPU snapshot/source_stride、目的override和max_copies；不以直接SetResource留下旧A快照。引用快照测试先通过真实GetRegionHash→GPU readback建立生产前置快照，再验证A→B切换，未补造handle数据。自定义资源可没有pipeline view，override通过最终真实槽位view验证。raw慢copy测试发现已有自动structured推断会生成RAW+STRUCTURED互斥flags；现保留raw表示，真实descriptor与全部64字节Map读回通过。动态SO测试另提前补未来1d9403的caller引用释放修正；该提交的offset行为尚未移植，仍为pending，账本单独注明前置修复。

CB范围保unless_null旧窗口，包括超物理存储但合法的CB1窗口；mixed group一次SetCB1，whole项0/4096。普通ref重绑whole，与本地scalar及该版上游一致；copy按物理范围，六阶段真实1024字节完整读回。空源fetch在assignment lookup前退出，不分配、不切类型、不延期或制造写入记录。FIFO/Spatial等范围保物理key；不支持的未配对pool范围仍拒绝。

文档同步最终6个提交，36页79本地链接/锚点/fence零错误，保Input默认on、typed reset、Store绝对float索引/独立readback、SpatialHash字节单位、CB ref/copy边界和取消示例。完整游戏示例及外部shader/assets未执行。后续67项已分组只读审查，见After-No-Handle、After-Store及Final-Feature审查文档；审查不等于实现。

实际最终验证：MSVC x64 DLL/完整生产回归构建通过；完整CTest **3/3**，legacy0.02秒、version generator11.88秒、主回归1.68秒，共13.60秒。证据为build/upstream/range-batch-final-{build,ctest}.log；两个隔离资源脚本通过，range-batch-isolated.log；DX12兼容目标成功且无待编项，range-batch-dx12-build.log。D3D11延迟销毁测试先ClearState/释放观察引用/Flush，再检查命令仍存活时view释放，未降低生命周期断言。未部署或启动游戏。

累计214项implemented-awaiting-validation、55项skipped、67项pending，已处理269/336。目标游戏与设备特定行为仍待验证，goal保持active；下一连续批从99751321开始，包含显式范围语法迁移、CPU数据缓存、Store和CheckTO/dump范围扩展。

## 当前复现命令

1. `python scripts/check-dx11-upstream.py`：拉取远端，报告新提交，不改生产代码或同步状态。
2. 在 x64 MSVC 开发环境中：`cmake -S . -B build/dx11-upstream-en -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`，然后 `cmake --build build/dx11-upstream-en --target DirectX11 dx11_upstream_regression -j 6`。首次验证已有旧缓存时添加 `--clean-first`。
3. 同一环境：`python scripts/test-dx11-pool-flags.py`。
4. 同一环境：`python scripts/test-dx11-resource-properties.py`。
5. `ctest --test-dir build/dx11-upstream-en --output-on-failure --timeout 30`。构建失败时不得继续运行旧测试可执行文件。

现有 build_release_timing_x64.ps1 调用的脚本只构建 DirectX12 与注入器，不能替代 DX11 构建验证。此次隔离构建未执行部署或启动游戏。

AGENTS.md 已要求代理在 DX11 工作开始时检查更新并按账本继续；它不提供无人运行时的后台调度器。
