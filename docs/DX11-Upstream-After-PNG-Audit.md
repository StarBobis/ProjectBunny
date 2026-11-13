# PNG 之后 25 项提交审查

范围为同步账本中 `b3a309b7` 后的连续25项，首项 `eb80c3e6`，末项 `79b4d29f`。这是账本拓扑顺序，不是 `git rev-list b3a309b7..master` 的集合顺序；后一命令还会列出账本更早处理的资源属性分支提交。此审查只新增本文和 `build/upstream/audit-after-png/` 临时材料，没有修改生产、测试或账本，没有构建或运行回归。

已逐项读取普通提交完整 diff，所有12个 merge 检查父关系、combined diff 和 remerge diff。combined diff 全为空，但 `9c9db1b3` 的 remerge 非空，故不能只凭 combined 为空判断没有手工解决。

## 先处理的依赖与确定缺陷

1. 色彩后续组 `e321 → 1753 → d492` 应在当前安全扫描器上适配。`e321` 的提取函数在本地已由 `PngHasSrgbChunk` 达到目的，不可把旧 `fread/fseek` 代码重新搬回。`1753` 把字段从自动构造的 `wstring` 换成 enum，却没有补构造函数初始化；必须显式初始化 `CustomColorSpace::DEFAULT`。`d492` 新增 `[System] force_detect_color_space`，默认 false；显式 linear/sRGB 优先，关闭额外扫描后依旧让 WIC 自己读取元数据，不能改成 IGNORE_SRGB。
2. `440ac3a9` 日志重构存在初始化顺序问题：先 `GetPrivateProfileString` 读到新 `log_level`，但在 `ParseIniFile` 前又调用只查询 `ini_sections` 的 `GetIniEnumClass`。首加载 map 为空，重载 map 是旧配置，不能照搬。应从已经读取的文本直接解析早期等级，或正确调整解析阶段。测试首次 warning/debug 和多轮切换必须检查实际日志内容。
3. 同一日志补丁移除了各宏的 `LogFile` 非空判断，仅比较等级。适配时保留“文件存在且等级允许”的双条件，覆盖打开失败与关闭后调用。上游根 `log.h` 对应本地共享 `src/common/log.h`；DX12 的 ModRuntime 包含 common `util.h`，不能直接引入仅在 DX11 定义的全局而破坏 DX12 链接/行为。可使用明确的 DX11 编译边界或 DX11 独立日志接口，保持其他目标原有宏语义。C++17 下 va_list 同时用于文件及 overlay 格式化时用 `va_copy` 分开消费，不依赖特定 ABI。
4. `bb2cc9e6` 在每个 `action->Dispatch` 之后检查 disable 状态，依赖第一项永远为 toggle。但 `RegisterIniKeyBinding` 遇到缺失配置不注册，非法键也不插入 action；此时第一个普通绑定会在禁用时执行。应把允许禁用时运行的 toggle 明确标记/单独保存，普通 action 在执行前受 gate，不能靠 vector 首项猜测。保持只在首次加载读取初始 disable 状态，以及 reload 后用户切换状态保留。
5. `ddd144 → f59` 的最终语义不是重复 HOLD Down 全部 no-op。最终条件是 HOLD 且 `!active` 时 `Activate(true)`，重复 Down 仍走 `Activate(false)`；它避免重复覆盖保存值，但仍调度激活列表及其 post 行为。不要把中间提交的全局 `if(active) return` 留在最终实现中。

## 逐提交结论

文件名按上游位置列出；DX11 对应本地 `src/DirectX11/`，根 `log.h` 对应 `src/common/log.h`。父关系完整 SHA 见末尾表。

| 提交 SHA | 实际内容/文件 | 本地证据、适配边界与所需验证 |
| --- | --- | --- |
| `eb80c3e6e354bc78bdf3770ec290e83f7337649d` | CommandList.cpp PNG helper空格/换行；IniHandler.cpp色彩空格与IEEE注释编码。 | 完整补丁无控制流/值变化。当前本地使用独立安全helper、`.c_str()`日志和ASCII/既有注释；保留即可作为无新增功能处理，不覆盖本地浮点下溢/空值修复。无需新增测试；不表示当前构建已验证。 |
| `e321caba107770d52f51322cfe058efa2ce6fdef` | CommandList.cpp/h把扫描提为 `HasPNGsRGBChunk`，GetWICFlags调用。 | 本地 `PngColorSpace.h/.cpp::PngHasSrgbChunk` 已独立实现，同等分类目的且有RAII、精确读取、长度边界、64位seek保护。本地 API 无需复制私有同名方法。维持 IDAT 前 sRGB，iCCP不推断；沿用真实PNG malformed/不存在/重复文件关闭测试，不降回上游只检查非零read。 |
| `1753a19a2fd02b901fcd4c876e64cb4469607e11` | CommandList.cpp/h新增CustomColorSpace enum+名字表，IniHandler改GetIniEnumClass，DDS/WIC比较enum。 | 当前字段仍wstring，内部表示尚不等价。C++17可用enum，不必升标准；必须ctor DEFAULT、复制metadata、pool template/lazy/copy_desc一起转换。上游缺ctor初始化已确认。保持大小写不敏感/未知回默认；默认构造、非法值、显式覆盖、PNG与DDS实际format/pixels和池复制回归均需重跑。 |
| `fe943be87e4ea8ea6ec60a6fba1f75df1b240b8d` | CommandList.cpp仅两个helper函数开括号换行。 | 纯格式，无独立语义；本地helper已经使用独立行大括号。避免为此替换安全扫描器。 |
| `9c9db1b3570340586b1a33bcab765f94e410b2a6` | PNG分支合80edf0a3资源属性分支；combined空，但remerge有注释冲突解决。 | 唯一remerge选择 `FindTextureOverrides` 注释en dash，另父使用em dash。无可执行差异，本地该注释ASCII保留即可；资源属性父已前批适配，不能重新覆盖FillInMissingInfo/Region安全检查。必须以此证据登记merge，不能称remerge空。 |
| `c9bb920194257d271eb37512993e2d94092a0dc5` | PR16 PNG分支合主线。 | combined/remerge均空；承接父色彩后续和777格式枚举，无独有解决。父适配后无需重复移植整文件。 |
| `d492122f8e70e7a8b131a81d14cea97a1cdadb45` | Dependencies/d3dx.ini、CommandList.cpp、IniHandler.cpp、globals.h新增可选force_detect_color_space，默认false。 | 本地当前GetWICFlags默认执行PngHasSrgbChunk，暂无开关，因此不是已等价。只gate自动额外扫描，显式linear/sRGB优先和WIC_DEFAULT保留。Globals初始化+每次config加载都赋值，示例配置说明用途；真实config absent/false/true/reload，配合PNG标签/覆盖/失败和COM线程回归。Windows WIC两种开关可能都输出sRGB，单看最终format不足以证明scan门控；可以由helper选择flags的真实入口直接验证，勿以源码匹配代替测试。 |
| `4867f53c87c8d615c4bbe736ea91a306a1dff50f` | 删除无用input日志配置读取、Globals字段/构造项、Dependencies注释。 | 本地 `gLogInput` 只查到字段/初始化/读取，无消费端，未删除。可小范围清理，不影响真正input dispatch。检查全引用及旧input选项不会改变键行为；无需专门性能/交互测试。 |
| `440ac3a99d20cbcbaf81e08ea4790bea7571e08a` | 新log_level disabled/warning/info/debug、旧calls/debug兼容；警告写文件独立于overlay显示；D3D11Wrapper/IniHandler/Overlay/globals/log.h/Dependencies。 | 本地仍旧FILE/gLogDebug宏及调用，未等价。除上述顺序/空FILE/共享DX12边界，还需保持LogFile重用时降低等级立即生效、invalid回disabled、debug=1仅在legacy calls=1生效；新的无配置默认disabled是上游有意行为变化，不能偷偷维持旧calls默认1。LogInfo默认允许到info，overlay消息写warning；show_warnings控制屏幕/beep而不阻止warning文件。测试真实输出含/不含标记、首加载/重载/文件打开失败、legacy组合、invalid、show_warnings=0；构建DX11 DLL与实际共享头消费目标。 |
| `c4e3a4499d05268bfef3896ff4eeee8ed18a34be` | transform_operators_visit空tokens抛语法错误。 | 本地已提前加 `!tree || tokens.size()<2` guard，finalise对empty抛错；`ini_expression_regression.cpp`已有空串、空白、`()`, `( )`, `1 + ()`负例。本地已消除相同崩溃并拒绝相同无效输入，可保留已有实现而非把singleton guard删掉；精确报错文本/位置不同，应记录适配。此审查未执行现有测试，不报PASS。 |
| `661dde0be647166b60b2ee01a9593091f290408b` | group_parenthesis改正向iterator向左查找，去reverse_iterator+goto，节点/异常语义相同。 | 本地仍旧结构，属于可读性重构而非已移植。可最小承接但保留提前空树/singleton保护与finalise空错误；C++17 `std::next`引入iterator头。真实嵌套/相邻括号/不匹配/空括号、结合优先级、长表达式与函数参数/资源索引回归。无需新parser架构。 |
| `a24d95f23bfc92a7f9c08956f563e2471152f1c7` | DestroyDLL删除LogInfo、SavePersistentSettings和fclose。 | 本地DestroyDLL仍有三者，非等价。移除loader-lock路径的保存/stdio清理，保留Present自动保存、显式保存/Reload事务失败重试；不要把持久化安全修复退回旧保存实现。进程终止由系统关闭句柄，主动FreeLibrary与进程终止不同，不能以该理由宣称任意卸载资源均已清理。验证正常安全点保存/失败保留dirty及实际DLL加载卸载无保存死锁；若未实测卸载须记录待验证。 |
| `ddd144d7bad35ae6a252adf564ff495a92c55eaa` | Override.cpp中Activate增加active早退，试修多键hold重复Save。 | 旧分支独立父e3b66a44，不应整文件复制。中间逻辑随后f59撤销，应与下一提交成组记最终效果，避免暂态限制所有Activate调用。普通合法hold/TOGGLE/Preset/Cycle及LOCKED writer路径必须保留。 |
| `f59f8e5d6a6f66884fbd82131ab971edd9028d65` | 撤销Activate全局早退；仅KeyOverride::DownEvent的HOLD首次走Activate(true)。 | 本地DownEvent仍所有HOLD都Activate(true)，尚未等价。重复Down保持原始saved值、仍执行Activate(false)；任何Up使active hold恢复一次，后续重复Up无操作。用真实WARP wrapper与KeyOverride、实际变量/命令列表、transition时间0测Down/Down/Up/Up及两种绑定组合，观察保存值、active与post列表；勿mock OverrideSave。 |
| `c9ddab824be6f8fec0016a3bf6f6aa6290cd42a1` | hold分支合cde6bab4主线。 | combined/remerge均空，无独有解决；保留主线pool/index/LOCKED后续，不回滚旧分支文件。 |
| `da6eed94e32e99bc333e2e35c4a67f583e974a54` | PR4合入hold修复。 | combined/remerge均空，只承接上述最终hold功能；无独有解决。 |
| `bb2cc9e6526e2243a2b8e32cbf8ddf3a4f1718a7` | ToggleInput公开回调、System toggle_input/disable_input、Globals状态、Dispatch gate。 | 本地无disable_input/ToggleInput，未等价。需修缺失/非法toggle时首普通action漏执行；toggle必须禁用时仍可恢复，reload不重读初始disable状态。CheckForegroundWindow仍应先执行；不得改变游戏原生输入，仅XXMI绑定。真实LoadConfig+Dispatch覆盖有/无/非法toggle、初始disabled、反复切换、reload、HOLD释放与repeat/delay状态；输入时序须真实生产action，合成按键/实际窗前台条件需注明验证环境。 |
| `4782b0b3af5aa4e92a16466fe980c7e02bd71c44` | input-toggle合8c1d58b7主线。 | combined/remerge均空；父含pool metadata功能，保留当前CopyMetadataFrom全字段/失效，不重复导入旧pool。 |
| `f6f7999077bb7ac92bdf2b4a0d485c8ea3fae2a7` | input-toggle合f8a50f93主线。 | combined/remerge均空，无独有解决，父此前已单独审查/适配；不可整目录覆盖。 |
| `a7abfd581ddca7ff7a324b054543401a8d7c8a75` | input-toggle合609244f4主线。 | combined/remerge均空，无独有解决；保留当前expression/static/parser安全适配。 |
| `7663882e383102be7336b3f99e5aaaba197db2cf` | input-toggle合84a7d071主线。 | combined/remerge均空，无独有解决；保留Reader/LOCKED、空间哈希与空值行为。 |
| `6fdab35efb54cae84db879883363aaa784cc6b19` | input-toggle合24e2cff7主线。 | combined/remerge均空，无独有解决；尤其保留structured Store完整结构复制/设备匹配/immediate-only/readback池，不复制旧Store。 |
| `c99039021b8787f3d1f858720ee398a4dd40b7ef` | input-toggle合a9d7c89b主线。 | combined/remerge均空，无独有解决；保留ShaderBindings坏码边界/原子发布/缓存同步、游戏layout uncached/custom full-key策略及ComScope/owning WIC factory。 |
| `0db50f3ca9c0247fa0869bafb912167bcd050c88` | input-toggle合6c9c2ac0主线。 | combined/remerge均空，无独有解决；保留steady ticks/首帧delta/FPS/安全半径封顶和callback跨namespace限制。 |
| `79b4d29f8001e3293b2cfcf83bae02a6bb800018` | input-toggle合80edf0a3资源属性主线。 | combined/remerge均空，无独有解决；保留真实format/width/height及buffer-view union限制、Region物理copy与CB窗口区别。 |

## 验证边界与推进建议

本批不引入新库/全局标准升级，DX11继续CMake/C++17，DX12保持现有标准。PNG helper已独立编译接线的事实不等于新enum/gate已完成；log_level涉及共享头、DLL初始化和overlay，宜独立批次验证。hold两提交作为最终单一行为组，input-toggle在补明确action分类后验证；其后合并提交必须按本表父证据去重，不重新恢复旧生产代码。

本文所有测试均为建议或指向已有测试覆盖，不是本轮执行结果。当前主代理正在统一构建，本文没有使用尚未收集的构建结果作为结论。游戏前台/按键时序、DLL实际卸载以及Wine/Linux WIC兼容效果未在本审查实测。

## 父提交精确关系

以下使用 `git show -s --format=%P` 原始顺序，merge第一父/第二父不互换。

| 提交 | 父提交（按顺序） |
| --- | --- |
| `eb80c3e6e354bc78bdf3770ec290e83f7337649d` | `b3a309b7c702bec944b8456f235ca4f84b9b9505` |
| `e321caba107770d52f51322cfe058efa2ce6fdef` | `eb80c3e6e354bc78bdf3770ec290e83f7337649d` |
| `1753a19a2fd02b901fcd4c876e64cb4469607e11` | `e321caba107770d52f51322cfe058efa2ce6fdef` |
| `fe943be87e4ea8ea6ec60a6fba1f75df1b240b8d` | `1753a19a2fd02b901fcd4c876e64cb4469607e11` |
| `9c9db1b3570340586b1a33bcab765f94e410b2a6` | `fe943be87e4ea8ea6ec60a6fba1f75df1b240b8d`; `80edf0a3e006fdced665b2314ccc02fc8c385bb2` |
| `c9bb920194257d271eb37512993e2d94092a0dc5` | `777141aaf378c2c371c611adc4821508899645fc`; `9c9db1b3570340586b1a33bcab765f94e410b2a6` |
| `d492122f8e70e7a8b131a81d14cea97a1cdadb45` | `c9bb920194257d271eb37512993e2d94092a0dc5` |
| `4867f53c87c8d615c4bbe736ea91a306a1dff50f` | `d492122f8e70e7a8b131a81d14cea97a1cdadb45` |
| `440ac3a99d20cbcbaf81e08ea4790bea7571e08a` | `4867f53c87c8d615c4bbe736ea91a306a1dff50f` |
| `c4e3a4499d05268bfef3896ff4eeee8ed18a34be` | `440ac3a99d20cbcbaf81e08ea4790bea7571e08a` |
| `661dde0be647166b60b2ee01a9593091f290408b` | `c4e3a4499d05268bfef3896ff4eeee8ed18a34be` |
| `a24d95f23bfc92a7f9c08956f563e2471152f1c7` | `661dde0be647166b60b2ee01a9593091f290408b` |
| `ddd144d7bad35ae6a252adf564ff495a92c55eaa` | `e3b66a44cf092aa94d8565bc98bb336f70057aa5` |
| `f59f8e5d6a6f66884fbd82131ab971edd9028d65` | `ddd144d7bad35ae6a252adf564ff495a92c55eaa` |
| `c9ddab824be6f8fec0016a3bf6f6aa6290cd42a1` | `f59f8e5d6a6f66884fbd82131ab971edd9028d65`; `cde6bab4a5b730e1a50552a66c62d291f9133735` |
| `da6eed94e32e99bc333e2e35c4a67f583e974a54` | `a24d95f23bfc92a7f9c08956f563e2471152f1c7`; `c9ddab824be6f8fec0016a3bf6f6aa6290cd42a1` |
| `bb2cc9e6526e2243a2b8e32cbf8ddf3a4f1718a7` | `af232a90fda4e5fc8799f854f07787a3bc33bcbe` |
| `4782b0b3af5aa4e92a16466fe980c7e02bd71c44` | `bb2cc9e6526e2243a2b8e32cbf8ddf3a4f1718a7`; `8c1d58b7d3365c8b4afd969a6b16ec7584eb8d47` |
| `f6f7999077bb7ac92bdf2b4a0d485c8ea3fae2a7` | `4782b0b3af5aa4e92a16466fe980c7e02bd71c44`; `f8a50f93bde03cbf0d5fda05d3b45e7910c2fc79` |
| `a7abfd581ddca7ff7a324b054543401a8d7c8a75` | `f6f7999077bb7ac92bdf2b4a0d485c8ea3fae2a7`; `609244f435bcdabd65ac0a2e8060f242ac4cc467` |
| `7663882e383102be7336b3f99e5aaaba197db2cf` | `a7abfd581ddca7ff7a324b054543401a8d7c8a75`; `84a7d07104516620ef003905bf7bf60f7f1913fb` |
| `6fdab35efb54cae84db879883363aaa784cc6b19` | `7663882e383102be7336b3f99e5aaaba197db2cf`; `24e2cff7b2bbfde31a8882c06d178fc008152997` |
| `c99039021b8787f3d1f858720ee398a4dd40b7ef` | `6fdab35efb54cae84db879883363aaa784cc6b19`; `a9d7c89b1ba957ac8a65a8bbf72acefa86eec544` |
| `0db50f3ca9c0247fa0869bafb912167bcd050c88` | `c99039021b8787f3d1f858720ee398a4dd40b7ef`; `6c9c2ac032a4002f78f3e39cf2893eb212fd177d` |
| `79b4d29f8001e3293b2cfcf83bae02a6bb800018` | `0db50f3ca9c0247fa0869bafb912167bcd050c88`; `80edf0a3e006fdced665b2314ccc02fc8c385bb2` |
