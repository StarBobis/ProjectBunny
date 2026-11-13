# DX11 下一批逐提交审查

本文件是只读审查结果，不代表实现或验证通过。审查范围是上游 `e4ae63da..37635f6e` 的 18 个 first-parent 提交。逐项阅读 `git show <SHA> --`，对照本地 `src/DirectX11/CommandList.cpp/.h`、`IniHandler.cpp`、`src/common/util.cpp` 和 DX11/DX12 CMake。没有运行构建、部署或修改同步账本。工作区没有 `.codegraph`。

## 优先处理的依赖与缺陷

1. **必须合并编译的两提交**：`51a23856` 的 `ParseFloatToken` 已含二进制分支，但使用未声明的 `value`；`da86b1b9` 才增加声明及 `ParseBinaryLiterals`。分别记账，作为一个构建验证组。前者也改变分词行为，不是纯格式。
2. **布局覆盖依赖**：`cde6bab4` 要先有 `271e97df/e4ae63da` 的布局元数据及 deferred override 接口。它的布局偏移修正边界错用 `original_format + format_size`，应使用 `original_offset + format_size`，同时检查增减后的 UINT 范围。`IAGetInputLayout` 的结果不能强转包装类，应沿用本地 UUID QI。`RunCommandListComplete` 在 `_RunCommandList` 前检查新建状态的空列表，无法刷新随后收集的普通 draw 覆盖；需核实实际 draw 完成路径。
3. **INI 数值转换**：`de03a3c9` 和 `51a23856` 都将所有 `ERANGE` 当溢出，无意把下溢零变成无穷。保留 `wcstof` 的实际下溢结果；明确检查 `end != start`，避免直接调用解析器时空串被当零。int 饱和转换应保留，不能换成直接 float→int。
4. **保留本地安全修复**：`d89560b7` 的 `extract_arguments` 仍用 `wcsrchr('(')`，会重新破坏嵌套 `HashRegion(($offset+4)*2,(8+8))`。保留本地外层平衡括号提取、参数数目界限、可复制的 shared_ptr 表达式及空输入状态重置。
5. **选项解析并非简单重构**：`37635f6e` 增加尾部选项支持、撤销 `296e988a` 整个临时 helper。建议以最终 reader 实现承接 `296e988a/26312d6c/4041d689/d89560b7/37635f6e`，逐项记录对应行为和测试，勿先引入又删除中间 helper。是否记录 superseded 需由主代理结合账本规则决定，不能仅凭最终源码相同标 skipped。

## 逐项证据与适配

| 完整 SHA | 文件和实际变化 | 依赖、适配与关键回归 |
| --- | --- | --- |
| `cde6bab4a5b730e1a50552a66c62d291f9133735` | CommandList.cpp/h：ElementFormat、ElementOffset、布局 match/replace、draw 前应用 | 依赖包装布局/deferred 接口；修正上述边界、原生布局 QI、刷新时机。offset 使用 wcstof 后直接转 UINT，须拒绝空值、NaN/inf/负数/越界及尾部垃圾；使用 RAII 释放布局，确认 Defer 的引用转移规则。回归多个 slot、同语义不同 index、格式变大/变小、APPEND、创建失败维持原绑定、嵌套命令与恢复。 |
| `76e6edad55fa319601e3628b84ad126a77c26ac5` | CommandList.cpp/h：全部 static_evaluate 增加默认 false 的 evaluate_variables 参数，逐层传播 | 可独立，不依赖布局语义；默认 false 必须保持优化器不冻结运行变量；支持 ret=nullptr 的仅探测调用。验证变量更新后 runtime 变化、显式 snapshot、二元/一元递归。 |
| `de03a3c938eb6140730e44239917ddf0ef9b4bba` | IniHandler.cpp/h、globals.h：GetIniFloat/Int/Bool 与 Constants 支持表达式；命名空间、模板类型名称、默认 namespace | 仅依赖76e静态求值，C++17足够。回归 >32 字符表达式、先定义常量、命名空间覆盖、失败默认值与 found、IEEE inf/nan、int饱和、下溢、bool各别名。无新增文件。 |
| `51a23856e0ea657c854ad455bf47064d959aafb7` | CommandList.cpp/h：分拆 operand parse，增加 NamespaceScanner、变量/目标 scanner、ParseFloatToken，重写 tokenise | 与da86共同编译；不能跳过。保留本地括号平衡与状态重置。isdigit用于wchar_t应改ASCII范围判断；回归 `$a-1`、资源 slot 连字符、UTF/路径命名空间、池索引、成员嵌套、`.5`、inf/nan、空/未知token。 |
| `da86b1b9e4ec3e6f6d450730802035674f442675` | CommandList.cpp、IniHandler.cpp/h：二进制字面量 uint64 parser 和 prefix 长度 | 补51a编译缺口。回归0b0、0b1、64位、65位失败、无digit失败、非法digit/尾随文本、表达式与配置 literal，数值仍服从float精度。 |
| `d654cf76db8d2857ae6b994296f9386ac45bcf6d` | CommandList.cpp/h：FRAME_NUMBER枚举、关键字、evaluate、允许上下文 | 独立小提交；frame_no转float精度与现有表达式一致。验证帧更新、允许上下文、不支持上下文。 |
| `6a77c5acc70853eaaf8b358e09da6bae96c7d676` | CommandList.cpp：TIME和FRAME_NUMBER只在evaluate_variables=true时可静态快照 | 依赖76e+d654。保持默认优化不折叠time/frame；ret为空时不能解引用。验证显式/默认静态求值与随帧更新。 |
| `db64f5b188c59be830a2414c07f897fdc296ce18` | CommandList.cpp/h：LOCKED变量flag、关键字、普通赋值拒绝 | 不是C++const；上游只拒绝ParseCommandListVariableAssignment。另有staging回读、池赋值/重置等fval写入路径，应审查其目标是否能指向locked变量，避免仅按提交描述报告所有写入都不可变。回归初始化读取、普通赋值拒绝、不锁变量继续赋值、persist组合/重载。 |
| `057b4bce3fe813ffdb1414f4896a5a5b6d4f781a` | CommandList.cpp、HackerDXGI.cpp、globals.h、util.cpp/h：HLSL式一元函数，计数/随机 helper，系统tick | 依赖新tokenizer。noise只列入function_tokens，没有operator，不能报告noise实现。C++17可用手写popcount；countbits float→uint32对NaN/越界须定义安全边界。random调用计数器是共享非原子状态，评估多线程命令调用；float舍入可能使巨大max返回max，需要测试严格上界。DX11 G依赖宜放DX11或条件guard；本地DX12当前未编译common/util.cpp，不能称当前必然破坏DX12。 |
| `4e624ddca22f672e76fbf71aa1e45deb35521db5` | CommandList.cpp：->Index成员；无索引Pool->Size重映射POOL_SIZE | 小而真实功能，不等价现有语法。回归Pool->Size/#Pool等价、Pool[id]->Index/#Pool[id]等价、空池/代理链、非池->Index拒绝或安全失败。 |
| `d76ef27cb4d90168e30493731a883ec946b86130` | CommandList.cpp：移除ParseTarget最后的contains_whitespace拒绝 | 依赖51a现在负责token边界。不能提前删检查而继续旧tokenizer。核对直接ParseTarget调用仍严格消费目标；回归成员/池括号内部空格允许，外部垃圾拒绝。 |
| `296e988a57989e73e057399fda5d9efc6bd72eb0` | util.h：非破坏性 terminated option helper | 由263使用、376整体删除；中间方案，仅识别空格不识别tab。可由最终reader承接，不能重复保留无调用helper。 |
| `26312d6c5204fd65480bec46bd1cf8f4c1cd06d2` | CommandList.cpp/h：用上述helper替换栈buffer options parser；UNKNOWN flag | 本地已用调用者wstring解决悬空ptr，但尚不等价解决带空格括号误报与UNKNOWN。不要回退到上游旧栈指针。回归 copy Pool[$a + 1]、嵌套member参数、未知前缀选项；最终376还要支持尾选项。 |
| `8c1d58b7d3365c8b4afd969a6b16ec7584eb8d47` | CommandList.cpp：parse_pool_copy_operation对源pool调用PropagateFlags(dst模板bind/misc) | 本地尚无这条传播调用，不能skip；已有PropagateFlags不能证明解析copy处已调用。注意本地proxy链/循环拒绝，不恢复上游无限递归。回归A=ref B之后通过A作为VB/SRV创建B资源、已有/惰性元素、链式proxy、misc flag。 |
| `8f423e077542b56a0ce94fcfe948d11deed51e05` | CommandList.cpp/h、IniHandler.cpp：移动token arrays/pragma；keep_alive_frames成员及两个INI选项改名 | 不是纯格式。新INI名pool_expiration_timeout_frames、pool_expiration_reset_elements；本地保留旧名可做兼容alias且新名优先，避免静默破坏用户配置。保留occupied/updated/expiration_checked，不能照搬哨兵状态。验证新旧配置与冲突优先、帧号UINT_MAX/回绕和过期重置。 |
| `4041d689404862330365838bad4fe9a288ef89c7` | CommandList.cpp/h：CommandArgumentReader及separator/peek模式 | 依赖51a scanners与ParseFloatToken。GetEnum模板定义放cpp，当前调用同TU可编译，若给其他TU公开需头中定义。GetTokenInternal仅计数不校验交叉及多余闭括号，需栈校验；模式切换失败要清peek有效性。Finished探测写入错误、试探GetEnum留下错误，影响376诊断。读取成功才消费，错误不推进。 |
| `d89560b707b00ff785138271e6d1b358e65ae4df` | CommandList.cpp/h：member参数用reader；提取MemberInfo；删除旧parser | 依赖404；共享表达式/文件级描述struct沿用本地设计。保留平衡外层提取，勿用wcsrchr倒退。回归缺参数、多参数、零参数()、嵌套函数逗号、空参数、坏括号、字符串语义名、重复解析清旧状态。 |
| `37635f6e3d7a299c285986328b082a3e3c765978` | CommandList.cpp/h、util.h：reader解析source及前后options，撤销terminated helper | 依赖404；不能因名refactor而skip。上游unknown_token_count>1且非finished才标UNKNOWN，单未知选项或尾未知被静默吞掉；修复诊断并核对是否应拒绝。试探解析不应永久记录可恢复错误；读到坏token后不能仅src_found=true报告成功。回归前置/尾置/混合选项、仅source、重复source、未知首/中/尾、缺source、tab、多余逗号、未闭合括号。 |

## 合理实施顺序

遵守账本拓扑逐项审查，代码集成可分组：布局覆盖；76e+de03的静态配置表达式；51a+da86的tokenizer/二进制；frame/time/locked；函数；pool getters/空格；选项reader最终链；pool flags及选项名。没有发现本范围内可直接按纯格式或合并提交跳过的完整提交。所有18项都是单亲提交；8f仅部分hunk为格式，但整体有公开配置变化。

所有生产变化应继续通过现有真实DX11回归target/WARP场景及DirectX11集成构建。这里只提出场景，不声称已执行；目标游戏验证仍需另行记录。这个范围没有新增源文件或要求升级C++标准/外部依赖，不能照搬上游vcxproj。
