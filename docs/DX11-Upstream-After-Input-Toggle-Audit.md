# 输入开关之后 25 项提交审查

本批严格取账本 `79b4d29f` 后连续25项，首项 `48c24f8d`，末项 `2806c7a5`。普通提交已读取完整diff；7个merge均读取combined/remerge，只有 `975bba80` 有独有解决，其余6个均为空。本轮仅本文及 `build/upstream/audit-after-input-toggle/` 分析材料，没有构建、运行测试或改动生产/账本。下文“已等价”限所读源代码的行为/结构证据，不等同新增测试PASS。

## 应先决定的兼容与缺陷

- `3102185e` 是新输入协议，不是当前全局gate的格式重构：配置迁到 `[Input]`，引入 mods/all，mods按递归include来源分类。上游代码缺少input项时使用false，所以初始off；模板写on。这一差别要明确记录，不能凭模板猜默认。当前显式privileged action、调用前gate、非法注册返回false、虚析构/owning注册、reload保持运行状态都应保留；升级成scope字段时不恢复“首action永远toggle”假设。
- 上游 `starts_with_any` 比较原字符串前缀，缺目录边界和Windows大小写/路径归一化。`ModsOther`可误匹配`Mods`，嵌套include的前缀记录只存原`*val`而实际递归路径是`namespace_path + *val`，也可漏判。建议在解析递归include时记录来源，或对完整实际相对路径作一致规范化和目录边界判断；显式namespace改名时仍按ini_path来源判断。测试Mods/ModsOther、混合大小写/斜杠、嵌套include、显式namespace、根配置/直接include、cycle back绑定。
- `f8bcae61` 将initialized标志在短临界区内设true，却在解锁后才LoadConfigFile；另一个线程仍能提前返回true并读取半初始化配置。`InitD311`还在调用前初始化同一CriticalSection，不能把小补丁标题视为整个初始化路径线程安全证明。适配应定义“初始化中/已完成”，在实际完成后发布，其他线程等待，并审查同线程递归/失败重试；保留本地ComScope/WIC factory生命周期，勿引入DllMain的COM工作。
- `a756fdec` 修复日志从旧ini map提前读等级的问题，本地独立InitializeLogging已先读profile文本而提前避免；但本提交另新增invalid log_level回退legacy calls/debug，本地当前ParseLogVerbosity对任何非recognized文本返回disabled，尚不等价。要同步新的fallback并保留显式disabled压制legacy，空文本是否视缺失按上游`level[0]`条件验证。共享common/log.h的MIGOTO_DX隔离、null FILE guard和va_copy修复不能回退。
- 持久化组 `79fd → efb → dbf → 8c439` 改unknown采集/通知与清理时机，不能套回旧float map/count方案而覆盖本地路径所有权、完整literal验证、LOCKED拒绝、atomic写/失败重试/dirtybit以及autosave不推进清理。`79fd`的ParseFloatToken只看前缀成功，不核对len完整消费；`efb`在auto_clear=false时用旧unknown_persist_vars_count显示数量；`8c`只比较集合大小，不证明未知集合相同，并在写成功前清空内存。适配需用真实未知集合/事务快照并在成功后提交，不能吞掉安全分支。
- `dbf` 不是只移动日志：它将ClearKeyBindings/OverrideSave.Reset移到SavePersistentSettings之前。本地保存失败时必须保持原live registry、override状态及pending请求；不能先破坏状态再因I/O失败return。ClearNotices/log标题可提前，但所有不可恢复状态清理应在必要保存成功之后。
- `5c77f4b0` 仅在头中声明inline ascii_tolower、定义留IniHandler.cpp，却让CommandList.cpp调用，违反C++17中使用TU需可达inline定义的要求；应把完整inline定义放头或提供普通非inline函数。`ec9`仅ASCII lower整个显式变量名，会丢`$\\MÓD\\var` namespace的Unicode大小写归一化；普通变量basename可以ASCII快路径，namespace仍需原Unicode lower语义。
- `2806c7a5` 新文档错误地写 `override_color_space` INI键，真实解析键为 `color_space`；并残留binary最多24位、random distinct等旧失实描述。只能适配性合并，不能覆盖前批64位parser/24位float精度、碰撞声明、Region规则和游戏验证边界。

## 逐提交结果

上游 `DirectX11/` 对应本地 `src/DirectX11/`；父关系完整SHA在后表。

| 精确 SHA | 功能/文件 | 本地证据、适配与所需真实验证 |
| --- | --- | --- |
| `48c24f8d19b48909a2a4405c64394f42d35e9796` | PR15 input-toggle合主线。 | combined/remerge均空；当前parent已按bb2最终行为适配，merge无独有代码。不得重搬旧input/Globals，HOLD最终条件和metadata/COM保持。 |
| `3102185e7adaa2069397316f8d76df97dc173ff8` | Input scope、来源路径分类、Input新section/选项；IniHandler/input/globals/Dependencies。 | 当前input只有enabled_when_disabled bool，全局disable；并无mods/all，尚未等价。用scope enum扩展真实dispatcher，privileged NONE明确指定；普通工具动作ALL，递归mod键及cycle back MODS。读取input_disable_mode每reload可改变范围，但初始input状态只读取一次；避免依赖INVALID scope充当初始化标记导致非法值反复重读。C++17无新依赖，测试真实include fixtures+registry内存buttons、reload、禁用中hold释放一次恢复；OS前台键盘采样仍独立边界。 |
| `975bba806d833dec80bda715ae26dadfcb04d947` | pr-19持久化分支合310主线，真实冲突解决。 | combined3538字节/remerge3531字节：Dependencies同时保留auto_clear与force_detect/Input；IniHandler把auto_clear读取提前、保留force_detect和Input；Globals同时保留persist字段和64位ticks，删除旧unsigned重复。当前本地已有路径安全persist和时间字段，需合并字段/读取而非恢复旧unsigned/旧saved_variables；后续Input仍待适配。无额外算法；按父功能回归和构建验证。 |
| `79fd6edafee1242581256e6f4f98dd5fdf0f26ed` | saved_variables改unknown_variables；增加未知pool变量采集、按count清理、wipe通知；CommandList/IniHandler/globals。 | 本地PreserveUserConfigVariable是owner-scoped完整literal采集，有事务保存与两阶段清理，已提前覆盖部分目标但内部行为不完全相同。禁止用frame0/简单ParseFloatToken恢复旧实现；未知pool语法亦需合法格式/完整消费，不把`5junk`保存为5。真实未知普通/namespace/pool变量、禁用mod后保留/恢复、非法值、LOCKED、不同user路径、同size不同未知集合、failed-save/wipe重试。 |
| `f8bcae61b9b9a0c49f6bfdae4a3bff76b9d914e7` | Initialized职责移DLL入口，短临界区保护check/set；D3D11Wrapper/IniHandler。 | 本地LoadConfigFile仍设gInitialized，未等价。修复必须覆盖完整状态发布而非只移赋值；InitD311首次criticalsection初始化也需once策略。考虑递归链代理配置，未实际测试不能宣称多线程安全。实际DLL首调用并发和失败/递归配置、初始化只执行一次、后续调用看到完整配置；不在测试里补造不同锁来替生产遮盖。 |
| `82cc1dd3fcb7211373c6d614460808a4ab6c2b56` | 删除LoadProfileManagerConfig定义/声明。 | 本地函数仍在；旧nvprofile.cpp:1671仍调用，当前CMake显式源列表不编译nvprofile.cpp。因此可以在当前目标范围淘汰，但不能宣称全仓无引用。明确旧未构建profile-helper不再支持，或保留兼容stub/函数；不顺手引回NVAPI依赖。验证当前DLL/test链接和无新增export回退，保留完整LoadConfigFile流程。 |
| `a756fdec80100b5d8462a37f34a808f6da2b5f48` | 早期日志直接lookup枚举、invalid回legacy；启动身份提升warning；IniHandler。 | 本地Logging.cpp已独立直接profile读取，null FILE与DX12隔离已提前适配；invalid/empty fallback与启动warning身份尚需比对。局部保留InitializeLogging接口，在ParseLogVerbosity同步fallback，不能重引GetIniEnumClass早期map读取。真实首次/重载level有效、disabled、unknown/empty+legacy组合、warning启动身份、打开失败；此前测试有invalid→disabled断言时应按新行为更新对应预期并新增fallback，不删除测试。 |
| `390cd643b636142c948c66c6ffd41d1ae0b625cc` | 解析user settings从ParseIncludedIniFiles提独立ParsePersistentSettings，仍最后；调整Include日志。 | 本地仍在ParseIncludedIniFiles最后调用user解析，并先GetUserConfigPath设置preservation owner；提取可承接但必须保留GetUserConfigPath/捕获时机、skip_early条件、user最后覆盖顺序。真实root/include/namespace/user同变量覆盖、早期跳includes再reload、路径切换与失败。无新接口/标准。 |
| `efb0c0508eecf1dfa0104835154c6d40b05a0f1e` | unknown通知由ParseCommandList改独立DetectUnknownPersistentSettings，reload后汇总+每项warning文件。 | 本地仍原无效user行通知，owner-preservation表不能直接按unknown_variables替换。通知应仅统计当前真正未知且有效可保存的值，auto_clear=0也显示真实数量，动态路径使用实际user filename。保留dirtybit2独立于bit1和show_warnings文件输出规则。实际日志与通知数量、清理开关、known/unknown pool、初始load/reload边界验证；不能用count猜测集合相同。 |
| `dbf63358b4794ea142e31485bc76a126ed215de8` | Reload日志/clear notices提前，wipe提示下放；同时重排Save到清bindings/reset之后。 | 日志部分可最小移植；事务破坏必须适配规避，保存或wipe失败之前不清active registry/OverrideSave、不清pending标志，失败不能报“wiped”。沿用本地SavePersistentSettingsInternal成功bool及写入/关闭结果检查；当前直接覆盖文件，不是临时文件原子替换。真实锁文件save失败、DeleteFile失败、恢复后retry，旧commandlist/变量仍可用，通知准确。 |
| `8c439438aab52f907d4a189594258965738a6a3e` | ClearUnknownPersistentSettings从Save提取到Reload，保存失败日志提升warning。 | 本地已把autosave与explicit-reload清理阶段分离，部分目的提前实现；原补丁count+预清空不能覆盖当前事务。采用owner/集合适配，决定清理后在成功写才提交、正确报告实际清除数量。验证相同size但不同keys、auto_clear开关跨reload、save失败重试、unknown重新变known、autosave不推进清理。 |
| `7ea5e9306c51234301781d226766c01540270c32` | ParseConstantsSection稳定压缩替代逐global erase；IniHandler。 | 本地仍next/entry逐erase，未等价。普通行稳定顺序，global声明按原顺序注册依赖；不在指向entry的指针仍使用时移动它。补丁会连非法/重复/initializer失败global也移出第二pass，旧continue会保留这些行，故诊断行为有差别；明确保留单次合法诊断/拒绝，不伪称纯格式。真实混合global/普通赋值、namespace、locked/persist、重复/坏名/坏表达式、后声明不可提前引用、空section/全global/全ordinary与reload；不据标题承诺速度。 |
| `c4009058aa9c07c569dd5b204dc7412ca289fa98` | PR22 compaction合主线。 | combined/remerge均空；仅承接7ea，不重复主线持久化/输入/日志改动。 |
| `d89378035550033fc2949d39813ac852de1af6b6` | IfCommand::noop缓存静态求值成功/值，避免多轮重复；CommandList.cpp/h。 | 本地noop仍每次static_evaluate，未等价。缓存仅属于当前If表达式，不是全局/跨reload；表达式重parse或优化replacement后应明确失效/重算。默认dynamic变量/random不可借cache折叠；保留runtime_populated callback可达、pre/post共同分支清理和未finalised诊断。真实if常量true/false、runtime变量多次切换、random优化不消费、嵌套elseif/ShaderRegex/callback空目标、完整optimise循环与reload。 |
| `0271e6f79b78c449d6b66d1782091d34e6b24415` | If cache字段改最终is_static/static_val，去局部冗余。 | 随d893一起适配最终布局；无独立新语义/依赖升级，不保留两套cache字段。验证同上，尤其cached false不读取未初始化值。 |
| `60947bec02c5c3327184f4d5631b3646fa4991cf` | PR23 if-cache合主线。 | combined/remerge均空，无独有解决；保留当前callback runtime标记及递归深度/clear语义，不能复制旧完整If实现。 |
| `1ff00d94096fffef76122bde8c0c88eb1dc1253b` | tokenizer累计operator_mask，parse只跑存在类别的变换pass；CommandList.cpp。 | 本地每次全12pass，未等价。表覆盖===/!==、**、//、shift/logical/bitwise，函数必须设置UNARY，+/-同时标unary和add；优先级/结合性顺序保持。外层getter参数内解析自有mask；空括号即使0mask仍由finalise拒绝。C++17uint32_t可用。真实每类单独表达式、混合全部优先级、nested/functions/unary chains、resource equality/->、UTF/tail-hyphen/ib-1旧不对称、bad operators和空树。性能收益未测，不写百分比。 |
| `635a57aaea14690d857da97e312aa98b5bbb21c4` | PR24 operator mask合主线。 | combined/remerge均空，仅父补丁；保留本地Tokenizer安全/命名空间兼容，不重复旧父CommandList。 |
| `87fa9479cfc8e1f5c1ae9dcdcd3cb2a4d3a336d8` | transform_operators_recursive直接遍历SyntaxTree tokens和Operator lhs_tree/rhs_tree，免walk临时vector。 | 本地仍walk()，未等价。当前Walkable实际实现仅这两类，直接遍历与既有结构吻合；以后新增类型要扩展。必须深度优先、访问子树后才改父tokens，保留singleton/empty保护。真实深层括号、左右子树、嵌套function/不同级运算、错误表达式；不擅加与旧不同的结合顺序/静态fold。 |
| `589b7fcaaaefdae225caf5592033dfcca2c06497` | 上述遍历函数格式压缩，无控制流差异。 | 与87一起保留最终可读格式；无需独立语义测试，不用其大diff行数推断新功能。 |
| `caf94da1088a4adb902f6cc0123cc0eccacfc0d1` | PR25 direct traversal合主线。 | combined/remerge均空；不会修改mask内容，只与前组共同回归。 |
| `ec9e57136d66738e6fb9ceb7501483be4e3c119d` | 变量lookup ASCII lowercase、预小写name的namespace拼接；CommandList。 | 本地仍towlower完整low_name/helper重复lower，未等价。优化不得收窄namespace Unicode/空格支持：显式qualified名称的非ASCII namespace仍casefold，local ASCII basename可快路；namespace-local优先再globalfallback保持。真实Key/Preset未预lower输入、显式$\\UpperUnicode\\leaf、多层空格namespace、无namespace/globalfallback、不存在/非法名、LOCKED赋值拒绝、已有UTF/INI表达式回归。 |
| `4fad1e3d1efa8cc5309826515c1025e27bd9cce4` | PR26 lookup优化合主线。 | combined/remerge均空，后5c77移共享helper；无需复制临时重复函数或旧parser。 |
| `5c77f4b0907906751626bb28e831edfeb22851b6` | ascii_tolower移IniHandler、Constants亦用ASCII；namespace helper const-ref+仅lower namespace。 | 本地旧by-value helper仍完全lower，未等价。修头inline定义可达性；公共helper“必须传已lower name”的新前提要核对全部调用，本地tests也调用，保留兼容可在入口按basename快速lower但namespace仍Unicode。不要把word大小写转换变成locale相关变量声明新权限。测试含uppercaseASCII flags/name与Unicode namespace、直接helper用法及跨TU Release链接。 |
| `2806c7a507a09a34f0da88a9e58b6f07e6b7e8fb` | docs函数float/zero说明、DXGI enum literals/format-width-height、Ring默认、资源color选项新页。 | 新docs当前有binary64/float24与random0、Ring/空间/资源安全说明，部分已提前覆盖；DXGI Format/extent与color选项页仍需补。真实键是color_space，上游新页错写override_color_space且DDS Linear不能保证去掉原生SRGB；不要照抄。修相对链接md/#、错误层级/大小写并保持原bytecode/shader mask未实现、CB ref/copy等边界；运行全树本地link/fence检查，示例未执行要记录。 |

## 验证边界

本批适配均可保持DX11 CMake/C++17；没有理由升级全局标准或复制vcxproj。日志共同头隔离DX12，资源池/着色器/hash缓存/Structured Store/COM修复在多个merge父出现但不是新merge功能，必须按父去重并保留本地版本。

已有真实 `persist_preservation_regression.cpp` 检查autosave不推进cleanup、禁用mod恢复、路径/wipe/save失败与retry；输入测试使用内存按钮替换硬件采样，实际registry、action、KeyOverride/OverrideSave/transition均为生产代码。这些是后续回归入口线索，本文没有执行它们，也没有把主代理尚在运行的构建称为通过。多线程DLL首初始化、Windows真实前台按键、目标游戏/Wine兼容仍需对应环境证据。

## 完整父关系

按git原始父顺序，merge父不互换。

| 提交 | 父提交（按顺序） |
| --- | --- |
| `48c24f8d19b48909a2a4405c64394f42d35e9796` | `da6eed94e32e99bc333e2e35c4a67f583e974a54`; `79b4d29f8001e3293b2cfcf83bae02a6bb800018` |
| `3102185e7adaa2069397316f8d76df97dc173ff8` | `48c24f8d19b48909a2a4405c64394f42d35e9796` |
| `975bba806d833dec80bda715ae26dadfcb04d947` | `c05b4cae770bcba965e5a1d8511ea8a1f7446080`; `3102185e7adaa2069397316f8d76df97dc173ff8` |
| `79fd6edafee1242581256e6f4f98dd5fdf0f26ed` | `975bba806d833dec80bda715ae26dadfcb04d947` |
| `f8bcae61b9b9a0c49f6bfdae4a3bff76b9d914e7` | `79fd6edafee1242581256e6f4f98dd5fdf0f26ed` |
| `82cc1dd3fcb7211373c6d614460808a4ab6c2b56` | `f8bcae61b9b9a0c49f6bfdae4a3bff76b9d914e7` |
| `a756fdec80100b5d8462a37f34a808f6da2b5f48` | `82cc1dd3fcb7211373c6d614460808a4ab6c2b56` |
| `390cd643b636142c948c66c6ffd41d1ae0b625cc` | `a756fdec80100b5d8462a37f34a808f6da2b5f48` |
| `efb0c0508eecf1dfa0104835154c6d40b05a0f1e` | `390cd643b636142c948c66c6ffd41d1ae0b625cc` |
| `dbf63358b4794ea142e31485bc76a126ed215de8` | `efb0c0508eecf1dfa0104835154c6d40b05a0f1e` |
| `8c439438aab52f907d4a189594258965738a6a3e` | `dbf63358b4794ea142e31485bc76a126ed215de8` |
| `7ea5e9306c51234301781d226766c01540270c32` | `158c8cdc790141b6124237ee8acec2745b600635` |
| `c4009058aa9c07c569dd5b204dc7412ca289fa98` | `8c439438aab52f907d4a189594258965738a6a3e`; `7ea5e9306c51234301781d226766c01540270c32` |
| `d89378035550033fc2949d39813ac852de1af6b6` | `158c8cdc790141b6124237ee8acec2745b600635` |
| `0271e6f79b78c449d6b66d1782091d34e6b24415` | `d89378035550033fc2949d39813ac852de1af6b6` |
| `60947bec02c5c3327184f4d5631b3646fa4991cf` | `c4009058aa9c07c569dd5b204dc7412ca289fa98`; `0271e6f79b78c449d6b66d1782091d34e6b24415` |
| `1ff00d94096fffef76122bde8c0c88eb1dc1253b` | `48c24f8d19b48909a2a4405c64394f42d35e9796` |
| `635a57aaea14690d857da97e312aa98b5bbb21c4` | `60947bec02c5c3327184f4d5631b3646fa4991cf`; `1ff00d94096fffef76122bde8c0c88eb1dc1253b` |
| `87fa9479cfc8e1f5c1ae9dcdcd3cb2a4d3a336d8` | `48c24f8d19b48909a2a4405c64394f42d35e9796` |
| `589b7fcaaaefdae225caf5592033dfcca2c06497` | `87fa9479cfc8e1f5c1ae9dcdcd3cb2a4d3a336d8` |
| `caf94da1088a4adb902f6cc0123cc0eccacfc0d1` | `635a57aaea14690d857da97e312aa98b5bbb21c4`; `589b7fcaaaefdae225caf5592033dfcca2c06497` |
| `ec9e57136d66738e6fb9ceb7501483be4e3c119d` | `48c24f8d19b48909a2a4405c64394f42d35e9796` |
| `4fad1e3d1efa8cc5309826515c1025e27bd9cce4` | `caf94da1088a4adb902f6cc0123cc0eccacfc0d1`; `ec9e57136d66738e6fb9ceb7501483be4e3c119d` |
| `5c77f4b0907906751626bb28e831edfeb22851b6` | `4fad1e3d1efa8cc5309826515c1025e27bd9cce4` |
| `2806c7a507a09a34f0da88a9e58b6f07e6b7e8fb` | `5c77f4b0907906751626bb28e831edfeb22851b6` |
