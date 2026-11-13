# DX11：84a7d071之后20项审查

范围按docs/dx11-upstream-sync.json中84a7d071后20项pending拓扑顺序选取，不按日期或first-parent简单截取。已逐项读取生产diff和父提交；这20项都只有一个parent，没有merge/combined diff。注意7d60→dd1a→6d6d→2d6a→c05是从f8e7118a分叉的持久化支线；随后bf929也从f8e7118a继续另一条主线。账本排列不是一条可盲目连续套补丁的链。未来合并二分支的merge仍需另审冲突解决内容。

只新增本文件，不改生产、测试、账本或既有文档，不构建。生产diff完整审阅；80481d19新增2443行token格式声明头已核对文件性质、版权、使用的token/宏及构建引用，未将其全部枚举作为需要执行的算法。所有结论均为源码审查，下面的回归场景尚未执行。

## 实施优先级与分组

1. 先补bf3/b01的缺失custom资源null防御；本地GetResource仍有空对象直接expire，不可把外围null guard当完整修复。
2. db810/43391/9fc83/f8e7118a/fdab/4e7988是相互关联的分词/reader兼容组，适合一组测试；9fc新增的空var解引用必须带f8的修复，资源member不得被前置slot匹配截断。
3. 持久化5提交应按各自功能逐项记账，以最终生命周期设计落地，不能重复覆盖中间SavePersistentSettings实现；保留本地失败不清dirty、locale恢复、LOCKED序列化策略。
4. 98d0+326f合为pool读miss/邻居重绑定验证组；保留本地occupied/updated、proxy安全及32位grid。
5. 0d93+1bf+403形成SyntaxTarget/shader member组；804是独立bytecode/cache前置。1bf空成员数组不符合标准C++17，应避免中间零长数组或与403非空表共同编译。

## 逐提交审查

### 1. `db810e550691ff85444e718a03166d909405dd96`

CommandList.cpp/h：CommandArgumentReader::GetVariable/GetTarget新增默认PeekMode::Token参数，调用PeekToken时传入；目标is_source=false不做source scanner标识符验证，以保留LHS宽松命名。依赖reader前组，无新CMake文件。不能将“LHS允许空格/UTF”等价成“任何尾随垃圾都接受”：Argument模式消费完整LHS后仍需实际registry精确查找和Finished。保留本地LOCKED只限制目标，Peek切模式失败清cache、成功才consume的修复。回归Token/Argument两模式、含空格namespace目标、源严格扫描、未知变量、locked源可读目标拒绝。

### 2. `43391f7b10bd3e7d87d680dba945507c97f62a4d`

CommandList.cpp赋值LHS改Argument模式，local keyword仍先按Token消耗，再把整个余下变量名作为Argument。解决namespace包含空格时LHS被截断。依赖db810。保持本地完整local token、空line/name检查、未初始化声明尾部检查及pool fallback；不能机械保留旧PeekToken后调用GetVariable默认Token。回归`$\namespace with spaces\x = ...`、local声明/赋值、namespace中文、source表达式的不同边界、target多个词但无对应registry拒绝。实质兼容功能，不是格式skip。

### 3. `9fc83bc0889b8982c04b6b7bbf189d62dc122bc5`

CommandList.cpp/h：引入is_operator_char，ResourceTarget scanner允许非operator UTF字符和连字符；分词先精确无hyphen slot/INI param/keyword/specialfloat/scissor，再宽松resource，最后shader；新增parse_slot与ParseTarget allow_custom参数。实际要兼容`ib-1`可做减法、`vs-cb0-1`不能、资源尾hyphen保留的历史不对称。

危险：同提交把失败GetVariable分支改成`if(var->flags & LOCKED)`，unknown变量var空会crash，必须带f8e7118a的var判断。本地ParseTarget null前wcslen及每次reset修复也不能回退。新scanner扩大字符集，必须带后续fdab的括号边界及4e7988的member前缀优先修复，否则vb0->stride被先吃vb0，括号也可能变identifier字符。回归UTF资源、namespaced pool、ib-1、vs-cb0-1、资源名尾-和`ResourceFoo- -1`、特殊float后减法、slot成员、空target、交叉/未闭括号、失败后重复parse清旧状态。

### 4. `bf3eddfbd64bb0c05d11203cda2ea8ed96ab859b`

CommandList.cpp GetResource CUSTOM_RESOURCE分支在GetCustomResource之后检查null再expire/Substantiate。本地审查时仍缺这条，不能skip；GetCustomResource自身允许返回nullptr并不保护调用方。回归空pool、动态索引missing、未绑定custom对象、GetResource返回null且out view无悬挂，普通存在对象仍正确Substantiate。依赖现有helper即可，建议尽早补防御。

### 5. `b01bead3e38bd5ac446613880c12cb7702683635`

CommandList.cpp GetResourceStride/GetResourceSize CUSTOM_RESOURCE路径找不到对象时立即RESOURCE_NOT_FOUND，而非fallback进入GetResource。本地目前fallback，且会触发上一项空解引用；需同步两个层次。上游Stride多套一层`type==CUSTOM_RESOURCE`冗余if无需照抄。回归无对象size/stride都-2，已声明无override继续查真实buffer，override_stride/override_format优先，以及非buffer原错误值。无新文件或标准变化。

### 6. `f8e7118a1220e43546e546efa31892b8304358df`

CommandList.cpp失败赋值var检查补`var &&`，针对9fc新回归。若移植9fc时已保留本地null安全则该hunk已提前等价，但必须账本指出关联修复并用真实unknown-variable赋值失败测试证明，不能只记空改动。只有LOCKED错误调用args.Fail，unknown user config抑制重复提示的意图不能变成吞掉所有其它语法错误。

### 7. `7d60f507eb4302f3a274b62c3fe8cd5c8629cc49`

IniHandler.cpp：SavePersistentSettings在非reload autosave读现有user文件的$变量到map，再写当前persist及未识别旧值；reload时不保留，防止autosave提前删掉禁用mod的值。中间版本在非reload分支不清dirty，可能持续重复写，由后续组修正。读取是1024 wchar逐行、只识别首字符$、swscanf未消费尾部，locale/编码和长namespace易丢数据，不建议照搬最终已弃用的磁盘reader。

回归真实文件：停用一个mod后首次reload、autosave多次、再次reload清理的时间点；同时其它persist变量更新必须保存。保留本地打开/写/fclose失败不清dirty，不能引入早清或漏清。此提交属于功能前导，最终实现承接它的“autosave保留未知值”而不是反复引入/删除临时reader。

### 8. `dd1a1a5770e8905c3a8a5ef71c0569224a787666`

Dependencies/d3dx.ini、CommandList.cpp/h、Hunting.cpp、IniHandler.cpp、globals.h：saved_variables从函数local移为全局map；新增auto_clear_persist_vars默认true（最初Hunting配置），首次加载/clean save填map；改变未知设置提示、重启用mod前保留旧值、统一dirty清零。依赖7d60。最终配置节由c05迁移到System，CMake不照搬Dependencies目录：查本地实际发布ini模板并同步最终配置/说明；没有外部依赖。

危险：全局saved map需明确跨config路径、wipe、重启、reload的归属与清空时机；不能从不相关游戏/配置路径复用旧数据。失败写不能先erase旧saved项而丢唯一副本。回归auto_clear两值、mod重新启用且值恢复、wipe保证旧map不复活、多个路径切换、未知行提示只对应真实策略。

### 9. `6d6dfb609eafef7c66317d50ef11aad3144e66b5`

IniHandler.cpp、globals.h：不用磁盘逐行reader，clean save阶段从仍存活persistent_variables捕获旧值；新增clear_saved_persist_vars bool以两轮reload延后清saved map。引用必须在ParseConstants清global registry前转成wstring/float拥有副本；不能保存CommandListVariable*跨清理。初始bool=false。回归捕获时序、mod禁用/再启用、save多次不推进reload阶段、失败保存阶段不提前推进。

### 10. `2d6af7226c9346a7a8795a76a14a74e5dbe65f2e`

IniHandler.cpp：统一两分支重复序列化逻辑，先写全部current并从saved剔除同名，再写剩余saved；只有pending reload且auto_clear才翻转clear_saved标记；提示文案明确设置0/1。不是纯格式，因为清理标记推进条件改变。注意代码先把saved写入文件再clear内存，所以第二次reload内存清理不自动等于同次文件已删；必须以真实后续保存/加载轨迹验证“两次F10”的可见行为，不能只断言map为空。

### 11. `c05b4cae770bcba965e5a1d8511ea8a1f7446080`

Dependencies/d3dx.ini、CommandList.cpp、Hunting.cpp、IniHandler.cpp：auto_clear从Hunting移到System；首次frame_no==0且assignment ini_namespace等于G->user_config时，把user配置值保存到saved map，即使变量尚未知；Save early条件去掉!gConfigInitialized。依赖前4项支线，不依赖bf929主线。

风险：frame_no0不是唯一可靠的首次配置标志，多次无Present Load会重复；namespace路径等价/大小写要核对；GetIniFloat默认0且无found参数可能把非法未知值写成0；重建namespace时name需与registry一致normalization。保留本地LOCKED变量不序列化策略，不把它重新放进saved map导致每次加载都有拒绝赋值。真实跨进程初始文件有unknown值→auto-save→重新启用mod应恢复；System配置生效，旧Hunting如何兼容需说明。最终发布模板只需System，不重复保留中间两节。

### 12. `bf92947536b4b620ccb0d18cfc0881b51ffb65a5`

CommandList.cpp/h：callback/pool copy的sticky bool failed改failed_root；若source解析root变化自动恢复，避免一次cycle永久停用；顺带修wstring日志.c_str。实际callback失败记录只看src pre root，声称两半同步，但pre/post可以不同，仍保留上游半更新风险。

本地callback已提前实现每次预检双root/两边原子更新且失败后可重试，功能恢复主体等价；不能套回上游只看pre root。pool setter为void且自行拒cycle，当前无sticky状态所以也能重试，但warning节流/root缓存接口尚不同；可记录提前适配依据，不为相同功能加重复状态。回归同一command先cycle失败、改变source后成功、单post变化恢复、pool三节点链、非法deepcopy日志类型保持。无需新CMake。

### 13. `fdab51e2f26438a3e9ea6b3dcc34c5a84de550f5`

CommandList.cpp is_operator_char补()[]，修复9fc扩大资源字符集之后括号被当identifier。仅改边界分类，不应把内部balanced bracket scanner取消。回归空/嵌套括号、Pool[id]成员、紧邻括号的资源表达式与交叉括号失败。本地此前严格stack有效性/嵌套参数保护必须保留。可随9fc提前实现并单独记证据，不能独立将所有括号都截断破坏pool token。

### 14. `4e7988e768414958df1bdc825231a86105fa84b4`

CommandList.cpp前置短identifier匹配后若接->就把len归0，让完整ResourceTarget scanner处理。如vb0->stride不能先认vb0。依赖9fc。回归vb/ib/cb成员、Pool.Size、未来vs->cb_size、普通ib-1仍减法；不能只用字符串包含getter作为测试。若主代理tokenizer早已不抢占成员可等价标注，需生产parse/eval证据。

### 15. `98d0d98d051d9414843295e25910e88cc8b6f665`

CommandList.cpp/h、IniHandler.cpp：FIFO/SPATIAL缺key读默认不分配slot、不evict；GetElementIndex返回SIZE_MAX，GetResource/GetVariable改回模板；pool_allocate_slot_on_missing默认false可恢复旧行为；LastFrame缺key返回0。已有key读仍可按refresh策略更新expiration。

本地SIZE_MAX目前用于**空池/invalid RING非有限**并返回nullptr，不能把所有SIZE_MAX一律当合法missing模板，否则会回退原安全测试并让invalid assignment写模板。区分合法FIFO/SPATIAL read miss与错误/empty/assignment失败；template返回只能read，write永远不能修改模板。CopyMetadataFrom需复制新配置，上游diff未补。保留occupied（UINT_MAX可作实际key）、32位空间grid、有效NaN bitkey、proxy链、updated状态；不得照搬key==UINT_MAX空slot。

回归read miss不改变index_map/lastReplacement/已有value，默认template值/资源、分配选项true旧行为、write创建/evict，empty与NaN RING仍失败、FIFO NaN bitkey正常、proxy/copy_desc/expiration。SPATIAL邻居路径需带下一项修复。

### 16. `326f64cf9ab565aa7cd3d50e002270994b8d16c6`

CommandList.cpp SPATIAL命中近邻后AssignSlot新key，替代只PostponeExpiration。98d0早return丢了key重绑，导致物体移动逐帧远离原cell后不再稳定复用。本地旧SPATIAL尾部已有AssignSlot，不应用98时丢掉后再当新功能重复算；保留近邻读取是否改变mapping的上游语义并测试。

回归轨迹cell0→1→2→3（每步radius1，起终距离超radius），同一slot/value保持、新旧key映射恰好更新、expiration更新、read miss不分配、wrapped X/Y/Z边界、非finite key按bit处理。不可仅测一次邻居命中。

### 17. `0d93c3620e42925955d0eaa7bceb84922e3c0dc8`

CommandList.cpp/h：抽出SyntaxTarget基类持有member_args及模板化MemberInfo/ParseTargetMember/ParseMemberArguments；ResourceCopyTarget继承用于后续shader target复用。不是用户新增语法，但是shader组依赖。

保留本地member expression shared_ptr/ResourceCopyTarget可复制、成员数上限、状态reset；抽取时将本地平衡外层括号提取一同迁移，不能回到上游wcsrchr最后左括号。模板定义若仅同TU使用留cpp可行，跨TU公开则要头定义或显式实例。不要把file-level模板描述定义塞进函数。完整重跑现有HashRegion嵌套、spatial、layout、Region、池索引、失败后再parse与copy-target语义。无新源文件/CMake变更。

### 18. `80481d190f13d3e717e3655effd0158f6183af84`

HackerContext.cpp、HackerDevice.cpp、ShaderRegex.cpp/h、globals.h、新d3d11TokenizedProgramFormat.hpp，另vcxproj/filters仅登记头。功能是DXBC SHDR/SHEX chunk扫描SM4.x/SM5.0声明，收集14 CB与128 SRV的类型/大小/stride/dimension，并在CreateShader按原shader hash缓存。CMake加入新头（保留Microsoft版权），不复制vcxproj，不影响DX12标准。

解析审查：检查DXBC最小头/num_chunks表、chunk offset/size、版本、dword_count、instruction length/customdata block；目前大量直接uint32*解引用，对任意unaligned输入缺可移植安全性，应用memcpy/受限读取器；out复用时不清旧声明，且false可留下部分写入，建议局部零初始化结果成功后提交。extended opcode/operand未处理，未来版本/SM5.1不在支持范围，不得误判其空声明为解析成功。重复/非法declaration slot、truncated operand也应明确处理，不越界。

CacheShaderBindings在CreateShader原hash后、replacement前裸find/emplace，现有CreateShader入口无全局锁；多线程创shader需查找/发布同步（解析可在锁外），getter查同map也要锁或稳定snapshot。失败仍缓存空bindings是上游策略，不能将解析失败当确实无声明；最好有状态供诊断。CreateGeometryShaderWithStreamOutput未接通该通用CreateShader，所以cache不保证所有shader都有，getter不能assert其一定存在。

缓存依据为**原始shader bytecode**，替换shader的实际声明可不同；需准确说明查询对象，不能报告“GPU当前replacement声明”。回归D3DCompile真实typed/raw/structured资源和静态/动态CB、所有支持stage，重复hash/并发创建、GS stream-output cache缺失fallback、SM4/5.0、坏magic/offset/length/unaligned/customdata、复用out与失败不泄漏部分结果；对照D3DReflect作外部语义oracle，避免测试复制parser。

### 19. `1bf2d27ed99c7ec25e514399b97f3ef441074a2a`

CommandList.cpp/h：ShaderTarget继承SyntaxTarget，shader_filter_target字符改完整target，parse_shader经新ParseTarget，tokenizer在宽松target后尝试shader member，process_shader_filter保留原SHADER分支语义。依赖0d93，之后403提供实际member表。

安全/兼容：上游ShaderTarget::ParseTarget先wcslen再判null，应沿用本地先null检查；每次reset shader_type/mode/memberargs。空`MemberInfo members[]={}`不是标准C++17数组，移植时明确无成员直接TOKEN_NOT_FOUND或与403共同编译。不要改掉普通shader过滤的负零“无shader”、正零“有shader无override”、filter_index/默认1语义。回归6stage token、invalid stage、vs-cb0优先resource、无scope拒绝、null/empty/repeated parse、原signed-zero语义。不能当仅重命名skip。

### 20. `403e3a860511ac085f06dbd5dae25e115ad88f4e`

CommandList.cpp/h增加shader member cb_type/cb_size/srv_type/srv_stride/srv_dimension；slot由动态member_arg求值，查mShaderBindingsCache数组。依赖804、1bf、0d93。CB size单位为16-byte常量数量，不是bytes；type=NONE时size/stride/dimension=-1，type本身0。无shader沿用原负零返回，cache缺失上游assert后0，应安全处理实际GS/未知shader路径。

**上游严重边界**：float直接UINT转换并无数组bounds检查，负/NaN/inf/CB14+及SRV128+可越界。先finite/非负/可表示检查，再按实际14/128容量检查；缺声明与invalid slot返回策略明确。**cb_mask/srv_mask只有enum和语法表，没有switch处理**，实际落到0，不得按名称报告已实现mask功能。可以保留上游未实现状态并记录，或等后续实际mask提交，不能自行制造API语义。

回归真实bound shader的typed/raw/structured三SRV、CB两索引类型及size、最后合法slot与越界、动态slot变化、无声明/type0与属性-1、nullcontext/state、no-shader负零、cache miss、replacement原hash语义、nested member表达式。保留本地scanner的->优先、shared expression以及数学/随机defaultstatic规则。

## Skip证据强度

本范围没有整项纯格式或无关提交。f8e空var修复可随9fc预先覆盖；bf929 callback恢复由本地提前适配覆盖、pool仍需核对；fdab/4e可在新scanner整组提前承接；326f是防止98移植回退的配套，原本已AssignSlot不代表98功能也已具备。持久化中间实现虽会被后提交替换，相关新增行为/配置/清理时机仍需逐项账本对应，不简单按“最后没有这些代码”全skip。
