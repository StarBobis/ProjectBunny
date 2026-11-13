# 资源属性 v2 之后 25 项审查

范围严格对应账本 `6bf6a746` 至 `034e2875` 的连续25项。读取了全部普通提交的完整diff，7个merge的combined/remerge均为空。只新增本文和 `build/upstream/audit-after-metadata-v2/` 分析材料；未修改生产/测试/账本，未构建或运行回归。下列测试都是建议，不是PASS记录。

## 分组及必须适配的问题

1. 池范围组 `f02 → 4b487` 应一起承接最终ResetType和RESOURCE/VARIABLE full-range模式。当前本地只有POOL_FULL_RANGE，ResetElement最后无条件设None；Mixed池的类型过滤尚未实现。新VARIABLE范围位不可照抄上游重编号：保留本地Region/Pool/Variable/Layout及新属性位，分配空闲位并更新mask/所有dispatch。实际语法是 `$Pool[*]`，提交说明中的 `#Pool[*]` 是笔误。
2. Bind flags组 `5aa → 038`：后者需要读取全部命令列表后固定点传播，在清resource maps前清原始指针边。当前本地AddFlags先OR misc，再检验CB冲突；上游“失败不变”的注释不符合实际。适配应先验证再提交一致metadata，并证明传播收敛；不能留下CB+raw/structured的失败残留。逆序多hop、pool root/element、环、冲突和reload都须测。分析日志不能重复求动态pool index。
3. `9b3d52f2` 与 `6cbdf23f` 补丁完全相同，稳定patch-id均为 `069d73aa4845a6d0abd348236ddfde4508abd99f`；移植一次，第二条可依此登记等价。后续TO候选缓存/分行统计依赖这一组，不要新增同名两份计数器。
4. Slot batch组必须以 `ef97` 最终prefetch命名与c3行为为准，但不能照搬其错误“顺序不变”保证。`emit_slot_batches`先按排序槽范围分组，再输出每组，跨gap会重排run。例如 `A=ref ps-t4; A=ref ps-t0; A=ref ps-t1` 会先执行0..1 batch、再4，最终A错成slot4。只要有合并使out变短，重排结果就实际生效。bind的动态pool/random索引及SET_VIEWPORT副作用也受重排影响。需保留原INI执行顺序，在原序列上划安全区间/设置barrier，不能以sorted范围替代执行顺序。
5. 同一batch延迟所有bind到末端，后续 `ps-t1 = ref Pool[ps-t0->Width]` 会读到旧ps-t0，原执行应读前一行新绑定。src是CUSTOM_RESOURCE并不证明无依赖；动态source表达式若依赖pipeline应成为barrier/按顺序执行。c3允许所有copy options后还要覆盖viewport、copy_desc/overwrite、failure等副作用。不能为优化关闭必要功能。
6. 上游 `ResourceCopyOperation::deferred` 是共享operation上的临时裸指针，指向batch run栈。多context并行会相互覆盖；异常会跳过置null，留下悬垂sink。应使用per-run state/显式参数或完整同步与RAII还原，临时resource/view引用也应RAII。保留本地设备匹配、Region选择、view/cache失败、物理buffer/CB窗口语义。
7. TO缓存 `fb2 → 76a → 034` 缓存desc候选、每draw仍过滤draw_info，但desc matcher并非只依赖immutable desc：本地FuzzyMatch::eval_field的RES_WIDTH/RES_HEIGHT读取 `G->mResolutionInfo`。必须将相关resolution值/epoch纳入候选有效条件，或把依赖动态resolution的matcher排除静态候选缓存，否则窗口/分辨率变化使同resource候选陈旧。
8. TO缓存保存指向override maps/vector的裸指针。reload清map、invalidate、读候选及hash变化必须在同一锁规则下；034的返回指针只能在Gcritical内消费，不得外泄。Invalidate当前上游仅Resources锁写，独立调用时与Gcritical读写不一致，应自身取得正确外层锁或严格封闭接口。锁序保持Gcritical→Resources；Resources锁内不要GetDesc/GetType/COM调用，本地ResourceReleaseTracker已记录驱动锁ABBA风险。资源持有有效AddRef期间访问handle，失效先于旧配置销毁/发布，不能只清bool就宣称线程安全。
9. 版本生成 `d92` 必须CMake适配。上游脚本强制origin和可达vTag，version.h又无条件include generated，与“无文件fallback”注释矛盾。输出放build目录，构建/RC include路径完整；无git/无tag/archive要可定义fallback，tag四段及build递增须满足Windows版本每段16位约束。共享src/common/version.h还被HLSLDecompiler等消费，不应全局改成XXMI身份或影响DX12。非GitHubremote可能带凭据，不能直接写入DLL Comments；仅公开sanitized URL。不要复制vcxproj/GitHub workflow替代实际构建接线。

## 逐提交

上游DirectX11对应本地src/DirectX11；上游根version.h对应本地共享src/common/version.h，但应按DX11边界适配。

| 精确 SHA | 实际功能/影响文件 | 本地证据、边界与必要验证 |
| --- | --- | --- |
| `6bf6a746a198c82fb34dbd336cac7ad8e0f4ddc9` | PR32 include优化合入；父f82与8d4。 | combined/remerge均空，无独有解决；父include token优化由前批处理，保留Input递归来源分类/namespace路径与persist所有权，不重复整文件移植。 |
| `f02bb2f1f0c547cf65fceae77b35dd515aa2d35b` | PoolElement::ResetType All/Resource/Variable；ResetElements参数透过proxy；CommandList.cpp/h。 | 本地尚只有全类型reset。Mixed过滤后保持Mixed，Resource/Variable仅对应reset时变None，None无操作。保留ResetResource清handle/cache和ResetVariable持久化/LOCKED规则；本地ResetVariable还可见 `G->user_config_dirty=true`，会覆盖bit2，适配关联路径应保持按位脏标记。真实Mixed同slot同时资源/变量、过滤不损另一类型、proxy、None、持久化默认值及dirty2→3。 |
| `4b48720e68c12d1857825cdcce83cd0185a90964` | fullrange拆RESOURCE/VARIABLE，修parser按DST选择表达式/RHS、资源写需$区分；CommandList.cpp/h。 | 本地仍单POOL_FULL_RANGE且资源fullrange的无效src会回退变量表达式，未等价。`Pool[*]=null`只资源；`$Pool[*]=null`只变量；`Pool=null`重置索引/proxy。无$变量fullrange拒绝，$表达式直接进真实变量parser，不先误解析成resource。保留user-config未知pool保存、现有locked writer、read miss模板与类型切换。真实空池/STATIC/RING/FIFO/SPATIAL、Mixed、proxy、表达式长/坏RHS、非法`#`形式、null与clear的metadata区别。 |
| `9b3d52f2e9249c35cabbd00a4b95b68899ceb042` | fuzzy扫描profiling count/hits/time与summary行、clear；ResourceHash/profiling cpp/h。 | 本地无fuzzy计数器，未等价；完整初始化、重置、SUMMARY开关全部接。hits指此次新增至少一个匹配，不是匹配条目数。snapshot mode决定start/end，避免中途mode改变结束未初始化State；计数/时间保持一致。真实fuzzy无/有match、已有matches不误计、NONE/SUMMARY、clear、可解析文本，不把微观耗时宣称游戏FPS收益。 |
| `5aa2fb35cdc3c67bb221d8bf71d004382ea79ea6` | FrameAnalysis复制日志显示src物理flags、dst需求flags和ref不兼容警告；CommandList.cpp。 | 本地无这一行。复用已解析dst_custom_resource，不能让dynamic pool key/random评估两次。`%x/%S`参数正确类型与c_str，Ganalyse_frame外不增加热路径查询；缺src时前置返回不解引用空resource。真实分析日志ref/copy、缺flags、动态slot求值一次；不把警告改成额外复制/静默回退。 |
| `038af0099ed5be1c33fe1e8a7485fffd03ed704a` | deferred custom/pool ref边固定点传播，Clear/Propagate公开函数及IniHandler两端接线。 | 本地当前仅parse一跳AddFlags，无graph。用最终config生命周期清边/全解析后传播，真实map node指针稳定但clear前必清图；pool已有/懒元素、proxy、misc同时传播。AddFlags失败原子与收敛见上。真实逆序多hop到ps-t、PNG实际创建有SRV flags、CB冲突失败不变、环、reload去旧边、copy不错误传播ref需求。 |
| `851c28cb899f6e3b60085d4871c784379049c5cb` | PR36 bind flags分支合主线。 | combined/remerge空；父还带9b fuzzy统计，承接一次即可，勿以merge统计重复插入。 |
| `6cbdf23f90e80644928e390579a281fcf9f0e38b` | 与9b相同fuzzy profiling独立分支补丁。 | patch-id完全一致见上；9b已承接后本项无新增代码，记录等价证据，验证沿共用测试。 |
| `69c26c3da29f6f7c39d76f5b20e60197627bb722` | PR40 fuzzy profiler分支合入。 | combined/remerge空；两父已有同补丁，不再移植，保留一个计数器与summary行。 |
| `abaed4e420f5aec115a169740db74876874ed629` | MarkResourceHashContaminated在锁前GetType，非2D/3D提前退；增加count/hits；ResourceHash.cpp。 | 本地仍锁后supports_hash_tracking，未等价。不要干扰buffer region缓存失效/数据更新，它们独立于texture contamination；只跳此函数本就不支持的类型。保存SUMMARY起止匹配，早退也计count/time，hit语义原hash存在。真实buffer update仍使region哈希变化，1D不追contamination，2D/3D full/partial仍记污染，锁压力收益需实测。 |
| `d6b27426894739d792edf64745949aee21f231f4` | PR44 contamination早退合入。 | combined/remerge空，无独有解决；沿aba补丁，不改本地支持hash==0、分页版本/CPU cache安全修复。 |
| `a6b30761912f46c3e97e6692d482ca8a217fde9c` | list/cmd profiling_generation每collection只注册一次；clear封装并更新reload/profiling clear；附hash hit行。 | 本地仍每次unordered_set.insert且两处直接clear，未等价。全部clear入口必须同步epoch，包括reload，避免旧pointer和计时跨窗口；字段默认0，全局epoch1。unsigned回绕不能与默认/旧epoch相撞；多context注册/计数同步须明确，epoch比较不自动线程安全。真实SUMMARY/TOP_LIST/TOP_COMMANDS模式切换、嵌套pre/post、clear后新窗口只注册一次、reload销毁、新对象、回绕边界，保留计时inclusive/exclusive口径。 |
| `43e5c9b16f12526f3dbf65355cc2b8f4f3e69138` | PR45 profiler generation合入。 | combined/remerge空；不得重复移动本地Reload保存失败前后的事务边界，clear只在保存成功的实际清registry阶段执行。 |
| `d92e5ce7e8ee8c12554e8405ed8c4f78f7f6c8ec` | tag→version.generated.h脚本，资源CompanyName/Comments/产品名/四段版本/DLL类型，vcxproj+release workflow。 | 本地共享version.h固定1.3.16、RC直接相对include、CMake无此生成，未等价。CMake只对DX11接生成wrapper/header/RC依赖，build路径多配置/并行安全、内容未变不重写。脚本git/remote/tag要求与凭据、字段16位边界见上；dirtytag不能标纯release。验证临时repo vX.Y.Z/vX.Y.Z.W、后续commits、dirty/no tag/no git、quote/backslash builder、safe remote；构建实际DLL后用版本资源API读回四段及字符串，DX12独立构建身份保持。 |
| `9f04a753391bc20f655d1e991c088718ac433896` | ShaderResourceBind/FetchBatch、merge pass及deferred结果；CommandList.cpp/h。 | 本地无batch，未等价。最初仅ref-like选项，后c3扩为全部；应最终组一次适配。修跨gap重排、动态pipeline依赖、stack sink并发，保留single op/异stage/方向切换barrier、dup槽最后写、unless_null/gap保留。真实所有六stage WARP、SRV对象身份、ref/copy/空/失败、0和127边界、单次Get/Set的实调用证据；不以source字符串断言替代实际状态。 |
| `c3ae4fd5cca9a2830b4893472505e26696bf1b4b` | 所有copy options可batch，DeferredBinding同时持resource/view/size/offset；CommandList.cpp/h。 | 最终功能必须保留每操作copy/view创建及失败行为。新增resource AddRef/Release必须RAII，调用失败不得清目标旧view（除原语义null）或留下sink。测试overwrite/copy_desc/no_view_cache/unless_null/viewport、设备切换、pool动态src、同destAlias fetch依赖；不把排除危险依赖解释为关闭copy功能。 |
| `c6b63cdd7872cb59d947ebcbe1c74c51e9838b7d` | 加55行batch文档+入口文字。 | 文档声称每branch单行的If/elif也可batch，实际merge只dynamic_cast ResourceCopyOperation，无If支持，不能据文档实现状态造结论；后dc2完整撤销，本地最终不需加入暂态文档。 |
| `dc2d565f10c0f4566b12b9649d28618de6c8edcf` | 完整撤销上述batch文档。 | c6+dc2最终文档净空，当前docs本就无该节，等价保留；不把删除文档误记删除生产功能。 |
| `c5d0caa7c166e9689366101864993caaeebbb8dc` | 拆slot_target/emit_slot_batch、解释gap/prefetch、整理merge；CommandList.cpp/h。 | 非纯格式，执行组织有改动，但仍按sorted范围输出，顺序缺陷未修。保留原序的设计必须在此最终结构上验证，不抄“order preserved”注释当证据。空run和single op不造空batch，所有slot范围在128内。 |
| `ef97cf7239aa2642b50de3fd85040028282056fa` | seed_with_current重命名prefetch_current_bindings；CommandList.cpp/h。 | 纯名称变化无新增语义。最终API采用prefetch名称；`unless_null`预取当前整个范围保留gap，但多次同槽已有赋值不能被后null覆盖。真实重复槽、初始view保留、无null时不额外预取。 |
| `50c9fc8c960b8bd7e496f0ee32bfe4771587debf` | PR46 slot batch最终分支合主线。 | combined/remerge空；只承接整个9f→ef最终组，不重复hold/资源属性等父旧代码。 |
| `a02418316be1c7e03d839d1322919f98b8f7ad38` | version分支合batch主线。 | combined/remerge空，两项独立功能按各父适配；不复制上游release workflow或源目录generated头。 |
| `fb2b5b0dc3f0456a431ec0d9a8211ba7442664d5` | ResourceHandleInfo存hash/fuzzy候选、reload失效、Cmd fuzzy入口接缓存；ResourceHash.cpp/h+IniHandler/CommandList。 | 本地无候选字段/函数，仍每次hash+desc扫描，未等价。缺handle（custom/交换链等）走原fallback；hash-first后fuzzy两者均贡献，draw_info每次过滤，hash更新含零/UINTMAX也正确。resolution动态依赖/锁/裸指针寿命见上。真实tracked native resource、untracked fallback、descriptor全维度、hash变化、fuzzy+exact顺序/优先级、多drawinfo、resize、reload oldpointer失效和多context并发。保留本地buffer region hash路径，不能用resource全hash候选替换region选择。 |
| `76a381d6c9d0bed31e86497569f208bc7868420f` | TO profiler拆候选rebuild与每draw lookup行；ResourceHash/profiling cpp/h。 | 依赖fb2/一次fuzzy计数器。lookup计时包rebuild时两行存在嵌套，不能相加算总收益；hits为此次追加至少一个match。无候选fallback、empty map早退、mode切换都保持计数含义。文本缓冲1024需确认多行未截断；真实summary输出包含完整行，clear重置全部计数。 |
| `034e2875ff7b04eefa75a312144cd5338d8c6cce` | 缓存get/read外加Gcritical，返回候选只在锁内消费；ResourceHash.cpp。 | 是必要并发修复而非纯格式，fb2不能先宣称可安全缓存。仍需统一Invalidate/发布锁、GetResourceHandleInfo生命周期、全局.empty无锁早检、fallback原函数锁规则。真实并发查找与reload/销毁、稳定资源引用、不发生ABBA；没有跑并发环境不得称线程安全已验证。 |

## 集成及验证边界

池/type filter、flags graph、profiler/contamination、version、slot batching、TO cache可以分工，但共享CommandList/ResourceHash/IniHandler段必须隔离补丁后主线程顺序合并。缓存组必须连失效/锁/动态resolution一起实现；batch必须有原序和依赖回归，不能只比较简单常量连续槽。新增文件与资源在CMake接全，DX11继续C++17，不影响DX12标准或日志/版本接口。

此审查没有测量任何速度收益。已有本地的physical-buffer、CB窗口、pool missing模板/时间、structured Store、ShaderBindings/full-key layout cache、ComScope/WIC owning factory等均须保留；它们在上游merge父出现不能作为回退旧实现的理由。版本资源检查、真实API调用计数、并发缓存及目标游戏行为均待实施验证。

## 完整父关系

按git原始顺序，merge父不互换。

| 提交 | 父提交（按顺序） |
| --- | --- |
| `6bf6a746a198c82fb34dbd336cac7ad8e0f4ddc9` | `f82a5aed52c3827aeb4396cd83ad9fafeedb351b`; `8d4d2cb3cab28fccc151b2e458c39f0a349b20ca` |
| `f02bb2f1f0c547cf65fceae77b35dd515aa2d35b` | `6bf6a746a198c82fb34dbd336cac7ad8e0f4ddc9` |
| `4b48720e68c12d1857825cdcce83cd0185a90964` | `f02bb2f1f0c547cf65fceae77b35dd515aa2d35b` |
| `9b3d52f2e9249c35cabbd00a4b95b68899ceb042` | `4b48720e68c12d1857825cdcce83cd0185a90964` |
| `5aa2fb35cdc3c67bb221d8bf71d004382ea79ea6` | `9b3d52f2e9249c35cabbd00a4b95b68899ceb042` |
| `038af0099ed5be1c33fe1e8a7485fffd03ed704a` | `5aa2fb35cdc3c67bb221d8bf71d004382ea79ea6` |
| `851c28cb899f6e3b60085d4871c784379049c5cb` | `4b48720e68c12d1857825cdcce83cd0185a90964`; `038af0099ed5be1c33fe1e8a7485fffd03ed704a` |
| `6cbdf23f90e80644928e390579a281fcf9f0e38b` | `4b48720e68c12d1857825cdcce83cd0185a90964` |
| `69c26c3da29f6f7c39d76f5b20e60197627bb722` | `851c28cb899f6e3b60085d4871c784379049c5cb`; `6cbdf23f90e80644928e390579a281fcf9f0e38b` |
| `abaed4e420f5aec115a169740db74876874ed629` | `4b48720e68c12d1857825cdcce83cd0185a90964` |
| `d6b27426894739d792edf64745949aee21f231f4` | `69c26c3da29f6f7c39d76f5b20e60197627bb722`; `abaed4e420f5aec115a169740db74876874ed629` |
| `a6b30761912f46c3e97e6692d482ca8a217fde9c` | `4b48720e68c12d1857825cdcce83cd0185a90964` |
| `43e5c9b16f12526f3dbf65355cc2b8f4f3e69138` | `d6b27426894739d792edf64745949aee21f231f4`; `a6b30761912f46c3e97e6692d482ca8a217fde9c` |
| `d92e5ce7e8ee8c12554e8405ed8c4f78f7f6c8ec` | `43e5c9b16f12526f3dbf65355cc2b8f4f3e69138` |
| `9f04a753391bc20f655d1e991c088718ac433896` | `4b48720e68c12d1857825cdcce83cd0185a90964` |
| `c3ae4fd5cca9a2830b4893472505e26696bf1b4b` | `9f04a753391bc20f655d1e991c088718ac433896` |
| `c6b63cdd7872cb59d947ebcbe1c74c51e9838b7d` | `c3ae4fd5cca9a2830b4893472505e26696bf1b4b` |
| `dc2d565f10c0f4566b12b9649d28618de6c8edcf` | `c6b63cdd7872cb59d947ebcbe1c74c51e9838b7d` |
| `c5d0caa7c166e9689366101864993caaeebbb8dc` | `dc2d565f10c0f4566b12b9649d28618de6c8edcf` |
| `ef97cf7239aa2642b50de3fd85040028282056fa` | `c5d0caa7c166e9689366101864993caaeebbb8dc` |
| `50c9fc8c960b8bd7e496f0ee32bfe4771587debf` | `43e5c9b16f12526f3dbf65355cc2b8f4f3e69138`; `ef97cf7239aa2642b50de3fd85040028282056fa` |
| `a02418316be1c7e03d839d1322919f98b8f7ad38` | `d92e5ce7e8ee8c12554e8405ed8c4f78f7f6c8ec`; `50c9fc8c960b8bd7e496f0ee32bfe4771587debf` |
| `fb2b5b0dc3f0456a431ec0d9a8211ba7442664d5` | `6cbdf23f90e80644928e390579a281fcf9f0e38b` |
| `76a381d6c9d0bed31e86497569f208bc7868420f` | `fb2b5b0dc3f0456a431ec0d9a8211ba7442664d5` |
| `034e2875ff7b04eefa75a312144cd5338d8c6cce` | `76a381d6c9d0bed31e86497569f208bc7868420f` |
