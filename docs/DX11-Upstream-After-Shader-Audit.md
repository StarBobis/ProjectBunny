# Shader 组及其后20项审查

选取账本尚pending的0d93/804/1bf/403四项及其后20项，共24项，到9e75c140。跳过本轮另派的UTF6、persist5、bf929、98/326；按账本顺序而非first-parent。逐项核对父SHA，24项都单亲，本范围没有merge。未修改生产、测试、账本或已有文档，未构建，不记录未经运行的PASS。前四项在此前审查基础上复核依赖及后续修复，不重复声称已实现。

## 组合落地建议

- Shader功能应以最终ByteCodeReader.cpp/h布局、c530安全读取、3b534缓存同步落地，再承接四项语法功能，逐项记账。不要先复制不对齐读取的初始parser、临时禁用、再启用、再搬文件。
- b2e禁用/357启用完全互逆：实际执行`git diff b2e57f12^ 3572ec09 --stat`为空。最终状态不需要临时关闭功能；两项可用“临时回退/随后恢复，范围内净差空”分别说明处理，不把upstream报告的目标游戏崩溃当成本地已复现或已验证解决。
- 初始403的shader slot无界索引、初始1bf先wcslen后null判断、0d93抽取恢复wcsrchr等问题必须在移植时适配；不能为了逐项编译恢复已修UB。
- 本地已实现`.5`识别、validation-only inputlayout、空signature保护、Pool=null解析等，需逐项核对差异，避免重复移植；游戏layout缓存政策和structured Store仍是实际未覆盖功能。

## 逐项结果

| 完整SHA | 实际文件/目的 | 适配边界与验证方式 |
| --- | --- | --- |
| `0d93c3620e42925955d0eaa7bceb84922e3c0dc8` | CommandList.cpp/h，member_args和模板化MemberInfo/parser移至SyntaxTarget，ResourceCopyTarget继承 | 后续shader member前置。保留本地shared_ptr表达式可复制、所有reset、参数上限及平衡外层括号；不能回到wcsrchr最后左括号。模板目前同TU定义可行，若跨TU用再调整。重跑nested HashRegion/Region/Spatial/Layout/Pool的真实parse/eval以及失败后重用target。不是仅格式skip。 |
| `80481d190f13d3e717e3655effd0158f6183af84` | HackerDevice.cpp、ShaderRegex.cpp/h、globals.h、HackerContext.cpp、新d3d11TokenizedProgramFormat.hpp及vcxproj登记；解析SM4.x/5.0 DXBC声明并按原shader hash缓存 | 最终整合fdc/3f/c530/3b。新增header保留Microsoft版权，CMake登记，不复制vcxproj。原hash反映原始shader声明，replacement可能不同；GS stream-output未走通用CreateShader所以缺cache不可assert。getter slot先finite/范围再索引。D3DCompile+D3DReflect对照typed/raw/structured SRV、CB类型/大小，坏bytecode/复用out、并发CreateShader、原hash与replacement语义。 |
| `1bf2d27ed99c7ec25e514399b97f3ef441074a2a` | CommandList.cpp/h，ShaderTarget继承SyntaxTarget，完整shader token替代单字符filter_target | 依赖0d93；空MemberInfo数组不符合标准C++17，可直接无成员分支或与403一起编译。保留no-shader负零、有shader无override正零/filter_index。先null后wcslen、每次reset；6stage token、vs-cb0资源优先、无scope拒绝、重复parse。 |
| `403e3a860511ac085f06dbd5dae25e115ad88f4e` | CommandList.cpp/h，cb_type/cb_size/srv_type/srv_stride/srv_dimension shader getter | 依赖804/1bf。CB size是16-byte常量数量，type NONE=0、无声明属性=-1。float slot直接UINT和数组索引是UB风险，容量14/128要先检查。cb_mask/srv_mask只有enum/语法表无switch处理，不能宣称已实现mask。无shader/cache缺失/负或非有限或超界slot、动态slot和真实shader声明回归；缓存读也须与创建写同步。 |
| `ad264db86ee7d4870215edce95287614713f2a41` | CommandList.cpp，Pool目的赋值源EMPTY时也进入parse_pool_copy_operation，恢复PoolFoo=null | 本地较早已支持普通Pool=null解除proxy与Pool[*]=null内容重置，应核对最终source parser重构没再次遗漏EMPTY；可按提前等价记账，附真实proxy→null恢复自己内容及两种null区别测试。无新CMake。 |
| `24e2cff7b2bbfde31a8882c06d178fc008152997` | CommandList.cpp，structured Store改复制完整结构边界，映射后从value_offset取4bytes | 实质功能未由初始4byte Store覆盖。索引仍底层float index，UINT64计算byteOffset、首结构floor、末结构ceil；staging大小用整个结构范围。保留本地finite/非负/UINT边界、物理ByteWidth、立即context/设备一致、view/resource RAII、LOCKED、bitwise dirty、失败维持旧值；不要照搬上游stride0/越界时写0。读取用memcpy而非unaligned float*。真实structured stride16/12的首/中/末float、重复bucket、不同source；还要测raw/plain原路径。理论stride<4的跨结构分支不能用D3D运行时不允许创建的假资源冒充真实覆盖。 |
| `64bcd002316352b9d79884c3d25617f5375fae04` | CommandList.cpp、HackerDevice.cpp/h、HackerInputLayout.cpp；分开游戏CreateInputLayout和缓存的CreateCustomInputLayout，validation-only/native错误/异常安全与cache释放 | 本地validation-only转native、非法输入转native、空signature copy及cache full-key/线程安全已先做；但本地游戏CreateInputLayout仍统一走mInputLayoutCache->Create，不能整项skip。应保留安全cache实现给自定义覆盖，游戏请求单独创建wrapper且native决定HRESULT；不能为复用上游hash-only cache倒退碰撞保护。移植类内多余HackerDevice::限定需去掉以标准C++17。测两个游戏布局不被cache强行归同、自定义命中、validation/null/error、COM identity与引用、badalloc返回和清cache后活引用。 |
| `fdc2539cc624f4708abfdba719b98461d5120491` | ShaderRegex.cpp删除parser，新ByteCodeReader.cpp承接原212行，vcxproj/filters登记 | 实际搬移无新算法，但本地采用最终文件布局需将新.cpp完整加入DX11 CMake和真实回归链接，不能只复制文件导致未链接。可804落地直接最终布局并逐项记录；验证符号唯一、无重复definition、正常/坏DXBC回归同一路生产实现。 |
| `b2e57f124835f7319d9c062663e7605a078cbd05` | ByteCodeReader.cpp与HackerDevice.cpp把parser/cache定义和调用整段注释 | 是临时功能禁用，提交说明提Linux/WuWa/Optiscaler冻结退出异常等，属上游观察而非本地证据。357完全恢复，二者最终净空；建议不在本地最终产物引入禁用中间状态，账本说明被紧随恢复抵消。游戏问题解决仍需游戏环境证据。 |
| `3572ec09c79a3ed84f59063c281e4191d01b83a7` | 同两文件恢复b2e全部被注释代码与CacheShaderBindings调用 | 与前项空净diff已有git证据；最终启用功能必须带后续3b并发修复和c530读取安全，不将“取消注释”当功能验证。 |
| `067f4a45905ebbc5c4749414cdc90919cab3e99e` | lock.h新增CriticalSectionLockGuard及CriticalSectionGuard宏 | C++17 RAII可用，禁止复制/移动。原宏直接`lock_critical_section_##__LINE__`会粘成未展开的__LINE__标识符，同scope两个guard重复名，需两层CONCAT或避免宏用显式局部guard名。使用现有_Pretty进入/Leave离开，不能global构造时使用未初始化锁。测early return/异常释放、同scope多个不同锁、递归critical section、诊断lockstack平衡；构建不要改变DX12共享标准。 |
| `3f2f370065efad66c615821bff95a23a2aca078e` | 新ByteCodeReader.h声明parser、HackerDevice include，ShaderRegex.h删除旧函数声明，vcxproj登记 | 只移动API声明，结构类型仍在ShaderRegex.h。CMake登记header（cpp已fdc），防头循环/依赖隐藏。若804直接最终布局可提前等价。确认production调用与tests都include真正owner header，而非测试自造实现。 |
| `3b53481caaef393c659674f5b1493241d4b94baa` | D3D11Wrapper.cpp初始化mShaderBindingsLock、globals.h字段、HackerDevice.cpp两阶段lock查/发布，解析在锁外 | 确定修复unordered_map并发rehash UB。第二次emplace必须原子竞争择一，局部bindings{}。新增锁既要在真实InitD311初始化，也要完整处理清理及真实测试初始化；不得在生产缺锁初始化时只补测试让它过。**此diff未保护403的map getter**，读也需同锁/发布snapshot，不能拿iterator出锁后假设并发容器安全。检查锁顺序及badalloc异常释放；并发同hash/异hash CreateShader+getter、shutdown/reload、缓存一次解析/失败状态。 |
| `c530d130bcadfa376b24261cac0d7e91b208730c` | ByteCodeReader.cpp read_u32用memcpy；坏chunk offset不再continue而失败；声明长度不足直接false；显式byte offsets | 不能按Refactor跳过，实质安全和错误判定变化。检查size_t32/64乘法、header/container声明size、chunk/table重叠、customdata length、extended tokens支持范围；out仍可半写且不自动清旧结果，建议局部结果成功才发布或明确清空失败。真实编译shader对照reflection；unaligned buffer、坏offset、短instruction、重复out、失败不残留声明。保留本地更严边界即可记提前适配。 |
| `e8ea20751e4e34dd85c0e9428ce8b1ddd157619b` | HackerDevice.cpp QueryInterface失败LogInfo改LogDebug；另加仅注释计时样板 | 行为变化是减少正常QI探测日志噪音，计时注释无需迁移。不可因有注释判整项无关；若本地已是Debug则等价。验证unsupported IID HRESULT/output原样、普通日志无该行、debug模式可见；无需新增数据面回归或生产性能声称。 |
| `6bd65ce55eef91317933a88a92c0e5c159b527b5` | CommandList.cpp/h GetTarget新增validate=true第4参，只在source && validate时调用token scanner验证 | 保留Token/Argument模式与精确ParseTarget查找；validate=false只绕过命名字符限制，不能绕过括号平衡、null安全或全部consume。源表达式仍严格；legacy资源赋值可以宽松。测试同一含operator资源名，普通expression拒绝、特定copy入口允许；正常pool/member源不受影响。 |
| `ceede416ae2e6e9a6ab06350b5b618928122e29c` | CommandList.cpp source resource-copy reader显式GetTarget(...Token,false) | 依赖6bd，恢复旧3dm“Resource后到空白都可作名”的赋值兼容，包括operator字符；不是把表达式语法也放宽。测试注册`ResourceFoo+Bar`后资源copy/ref成功，但算术中仍按operator分词；未知名/额外source/未知options继续拒绝，SRC view及LOCKED策略不变。 |
| `a9d7c89b1ba957ac8a65a8bbf72acefa86eec544` | CommandList.cpp numeric start增加`.`，支持.123 | 本地当前已是ASCII digit或`.`分支（51a适配时提前修），可等价记录，不能套回std::isdigit(wchar_t)。真实`.5`、`-.5`、`.5e2`、`1-.5`和单`.`/`..5`失败。不是提升float精度。 |
| `bb6dc33c8beeb15468c84d11548d289793c27d80` | 新docs/ini-language-extensions共11文件558行：表达式bit运算/资源元数据、pool声明/索引/元数据/资源、2例 | 文档也属同步范围，可按本地项目名/最终路径适配，不受CMake约束。不能复制未经本地测量的custom~1ns/slot10–100ns，也不能说30bit pointer hash“唯一”；pool FIFO例索引顺序须匹配实现（本地初始replacement下第一slot可为1），旧“soon variables”已落后现实现。示例shader hash含非hex字母s，应更正或用明确占位值。验证所有链接、fence和例子真实parser，不要求游戏验证已完成。 |
| `ba1fb5d6009c59a183b4f02f8e69bfacc8e016ba` | 文档入口两个readme.md改README.md | 纯文档大小写修复。若首次同步直接用最终README链接，记提前覆盖/无额外变更；链接检查按大小写敏感路径执行，不能仅Windows文件存在即通过。 |
| `2d9f7505ec86167f749150708dedadb8d55ee78a` | CommandList.cpp删除function_tokens中的未实现noise | 本地当前仍列noise但无operator，已测试noise拒绝；应删token使功能名单准确，不宣称删除一个可用函数。真实noise(1)仍应安全parse失败，random等其它函数保持。可随shader前功能组顺手实施但应账本逐项对应。 |
| `471bed39ef1675738d684672424c39d7e5a9d0e0` | docs资源元数据从expressions移到resources，新增resources README并改两索引链接 | 100%内容rename无算法；可文档首次落地直接用最终结构。**旧pool-metadata里的../resource-metadata.md链接仍未被这条修正**，需按最终实际路径修到../resources/resource-metadata.md。不保留死链只为逐提交外观。验证全树相对链接，避免重复文档副本漂移。 |
| `0df206b668b0b3ea7cda8fcc116a3baa029c32e2` | docs新增commandlists/callback、store、math functions/literals、inputlayouts、parameters、variables、Region等13文件413行 | 有多项文档与实际实现不一致：binary parser允许最多64bits，而文档称max24，实际24是精确整数可靠范围；random输出可碰撞，不保证同帧每次distinct；Region ref需256B窗口粒度、copy不受此限制且ref可超物理窗口，文档需补清楚；Offset的ib0例实际token是ib，IB/VB getter还可能计入call_info的draw偏移，不能只写FirstIndex/FirstVertex等同API绑定offset。pool每元素~500B是上游估计，不当本地量测。对照本地真实回归/类型边界调整，链接到适用说明，不把上游v1.0.4标作本地已全验证版本。 |
| `9e75c1404964da42ab6e5028442169ef4bbe0d20` | docs input-layout README删末尾多余```，pool declaration immideate→immediate | 纯doc修正，可首次落地直接应用。另0df literals中的4-backtick结束仍需结合最终Markdown检查；此提交没有修它，不能只认标题“typos”就声称全部fence正确。 |

## 已提前实现、必须保留的边界

本地InputLayout cache是full-key+mutex并保持活动引用生命周期，不能为64提交改回上游hash-only map。custom布局可缓存而游戏布局不缓存这一政策尚需实现。Store保留实际底层float索引、RAII、立即context、同设备、LOCKED、失败保持和bitwise dirty，structured修复只能扩大合法读取能力，不能倒退这些约束。ResourceCopyTarget的copyability/shared_ptr、平衡括号、null-before-wcslen与reset在SyntaxTarget抽取时一并带入。

实际可提供skip/提前覆盖依据：ad264的Pool=null已有真实回归；a9d的`.`分支已存在需补/引用真实数值回归；b2e/357有精确净空git证据；ba1/471/9e的中间文档状态可由首次最终结构直接覆盖。其余不能仅凭标题Refactor或“本地已有一个cache”就整体skip。此审查没有执行任何新增测试或游戏；列出的验证均待实施。
