# Input scope 批次合并接线审查

本次只读审查，没有构建、运行测试或修改代码/账本。主代理正在执行的统一构建不计作本文的验证结果。审查命令为 `git -C build/upstream/XXMI-Libs-Package show -s --format=%H%n%P%n%s <sha>`、`git show --format= --remerge-diff <sha>`、`git show --format= --cc <sha>`；后两项分别检查重演冲突解决与 combined diff，不能只凭其中一项为空判定无独有改动。

## 本批七项 merge

下表父顺序与 git 输出一致。数字是本次读取的 UTF-8 diff 字节数，不含提交说明；0表示确实执行并得到空diff。

| Merge | 第一父；第二父 | remerge / combined 字节 | 适配记录 |
| --- | --- | --- | --- |
| `48c24f8d19b48909a2a4405c64394f42d35e9796` | `da6eed94e32e99bc333e2e35c4a67f583e974a54`；`79b4d29f8001e3293b2cfcf83bae02a6bb800018` | 0 / 0 | PR15输入开关父功能合并，无独有解决；不回搬旧的首action推断或覆盖后续scope。 |
| `975bba806d833dec80bda715ae26dadfcb04d947` | `c05b4cae770bcba965e5a1d8511ea8a1f7446080`；`3102185e7adaa2069397316f8d76df97dc173ff8` | 3531 / 3538 | 非空，三文件解决详见下一节。 |
| `c4009058aa9c07c569dd5b204dc7412ca289fa98` | `8c439438aab52f907d4a189594258965738a6a3e`；`7ea5e9306c51234301781d226766c01540270c32` | 0 / 0 | 只承接Constants稳定压缩；不恢复持久化父分支旧count清理或失败后破坏状态。 |
| `60947bec02c5c3327184f4d5631b3646fa4991cf` | `c4009058aa9c07c569dd5b204dc7412ca289fa98`；`0271e6f79b78c449d6b66d1782091d34e6b24415` | 0 / 0 | 只承接If静态求值缓存最终字段；不重复移植两次缓存。 |
| `635a57aaea14690d857da97e312aa98b5bbb21c4` | `60947bec02c5c3327184f4d5631b3646fa4991cf`；`1ff00d94096fffef76122bde8c0c88eb1dc1253b` | 0 / 0 | 只承接operator mask，保留本地空树、UTF、嵌套target解析安全边界。 |
| `caf94da1088a4adb902f6cc0123cc0eccacfc0d1` | `635a57aaea14690d857da97e312aa98b5bbb21c4`；`589b7fcaaaefdae225caf5592033dfcca2c06497` | 0 / 0 | 只承接直接递归遍历及格式调整；不覆盖mask或改变运算结合性。 |
| `4fad1e3d1efa8cc5309826515c1025e27bd9cce4` | `caf94da1088a4adb902f6cc0123cc0eccacfc0d1`；`ec9e57136d66738e6fb9ceb7501483be4e3c119d` | 0 / 0 | 只承接变量lookup优化；保留Unicode namespace大小写、namespace优先/global回退及跨TU可达定义。 |

### 975bba806 的唯一解决与当前接线

完整读取了三文件remerge与combined内容。解决的是功能并存及字段类型，不是新的清理算法。

- `Dependencies/d3dx.ini`：保留 `auto_clear_persist_vars=1`、`force_detect_color_space=0` 与独立 `[Input]` 全部选项。当前模板三组均在，Input仍有 `input`、`toggle_input`、`input_disable_mode`。无需用旧分支模板覆盖当前文档。
- `IniHandler.cpp`：冲突解决将auto_clear读取放在屏幕fallback读取前，同时保留force_detect和Input配置。当前LoadConfigFile也在该位置读取auto_clear，随后读取force_detect和Input。新的unknown count/精确集合与成功写后提交没有被merge回退。
- `globals.h`：同时保留持久化选项与64位 `ticks_at_launch/gSystemTickCount`，删除旧unsigned tick字段。当前这两个字段为uint64_t且各只有一份，FrameTime/FPSCounter仍在；持久化字段保留并增加实际unknown count。

当前Input接线另核对：`InputDisableScope` 有NONE/MODS/ALL，普通注册的INVALID退ALL，toggle显式NONE；LoadConfigFile每轮读取disable scope，而初始开关由独立 `disable_input_initialized` 保护；`DispatchRegisteredInputActions` 在调用前按scope gate。来源scope保存在实际解析的IniSection中，未拿namespace别名重新推断路径。此为已读取代码的整合事实，不代表本次已验证硬件输入/游戏行为。

结论：未发现975的独有解决遗漏；其余六项没有独有解决。应按各父功能实际集成结果登记，不能把merge空diff当成父功能测试通过。

## 下一连续25项的分组与适配风险

范围严格取账本 `2806c7a5` 后25项，末项 `8d4d2cb3`。本次保存普通提交patch及merge remerge至 `build/upstream/input-scope-integration-review/`，读取以下核心改动；这里只给下一批分工/验证建议，没有实施。批内仍逐SHA记录，不能以分组代替拓扑顺序。

| 分组与提交 | 具体风险与验证重点 |
| --- | --- |
| 输入默认：`45a8ff4e51c08b97fd2c4eaa3d29616e77ac762a`、`4e384d877be14bfd49f9723002417c131e55942e` | 输入缺省从false改true；模板on/off改1/0。它确实改变上一批明确采用的缺失off行为，应更新首次缺失配置回归，但reload仍不重读运行时开关；不要丢scope NONE与非法toggle保护。模板的注释仍误称input_disable_scope，应使用真实input_disable_mode键。 |
| 未知数量/dirty：`4bd226f7dcee8bc723b3014de748dd62cbd4e017`、`13f88a7ac6195d77d59d884558ac3231cc908a99`、`60f2cd44b547bac50972d8c9039017f3dbcf7d92` | 4bd当前真实count已提前覆盖。13f把dirty改bool并令每次赋值直接覆盖，60f随后修复无变化赋值清掉先前dirty；应以最终行为承接，不恢复中间丢更新。若保留本地bit2独立清理机制，需要明确适配而不是机械替换类型，覆盖changed→unchanged、bit2→bit3、LOCKED及NaN/signed-zero原约定。 |
| 非persist旧值与新版清理：`c0675d4beb7f737eac0347b6bd400d26f74c3dd9`、`fa3c42cd5ba5d9214798aff80aa7c089e0aa89a9`、`015d33945e12b2d355c2735129083cdcd5a5c1b9` | c067禁止user文件给已失去persist的普通/池变量赋旧值，不能只在保存时丢弃；其独立中间patch在IniHandler函数之间插入不完整声明，宜与015最终形态共同实现。fa3增加非并发append，核对seek/句柄关闭、CRT模式及共享DX12边界。015将键迁为clear_unknown_settings、拆已知重写/未知append、按名字和值hash判变，并将未知处理移至本轮Constants执行后；模板默认0但生产读取默认true，需明确兼容优先与缺失默认。避免两次写入后半失败丢未知、提前清dirty/unknown、hash碰撞及路径串值；应保留完整literal与LOCKED/失败重试。 |
| 资源属性最终组：`13cc0ae46e5b8f638d93993721827be26a3684bf`、`3f5538a5c7e930ee871ce2041ec6b7daeee2dad8`、merge `1dd8c5075d41d3320f2c3dbf895f8454e37ee5dc`、`e8e2d7305d6c5dff0140c063fd3d97b10d5ae22a`、`5510cce23d3bd2383d1e46f7c13c0b04763193f1`、`f3c997881a07a6f61f06aecce8e80f68ea5f68f2`、`849feef145337a84371a342dfb944cdbb10570c3`、merge `0f5275b3b858759c5ba091ca14f236d7663c7eab` | 新Array/Mips/BindFlags及D3D11_BIND literals。按最终表长度排序：mips6、array7、bindflags11；保留本地LastFrame正确11。新枚举位不能覆盖本地Region/POOL/VARIABLE/LAYOUT。BindFlags无D3D11_BIND_NONE，使用显式0；Array/Mips自定义默认-1需fallback，显式0有定义。整资源Array/Mips不是当前SRV窗口，3D无Array按上游NOT_A_TEXTURE；WARP维度/缺资源/override/引用平衡及旧Region回归。 |
| DXGI前缀与pool词法：`812a392a8b2c812e3081dc6dd280d4295ae0a6ec`、`f9d665e220d8551015b88b5bb22976929e509bdf` | 812本地前批已使用完整12字符前缀和全词匹配，核对即可。f9优先把$pool候选作为target解析，解决namespace空格/连字符/UTF和动态index；保持普通$pool前缀全局变量回退、嵌套括号边界、未知值失败及现有$a-1不对称兼容，不整段替换本地安全tokenizer。 |
| v1.1.5文档：`96aee72048d1ebe5a81aca51e2f5bbdf39c31fa4` | 新literal与属性页需随真实API更新；链接中md/#写法及枚举前缀/输出error值逐项核对，未执行的示例不能称验证。 |
| INI section直传：`06962f170a36eeaf9bfa5cd9d7ce4b748fbf56cd`、`8d156caad87180bf038f0892451a6a6a91fcf264`、merge `3b2907250ad89ce397ecc1f4f1aab01eee9791bf` | 持有map节点指针替代每行lookup；保留本地IniSection.input_disable_scope、recursive_root及显式namespace来源。重复section失败清指针，跨file/section切换不可沿用旧entry；覆盖重复key与user最后覆盖，不能凭重构标题宣称性能提升。 |
| 解析次序/include：`e624dcf1fa1b66a61618d7f6164303d604456372`、merge `f82a5aed52c3827aeb4396cd83ad9fafeedb351b`、`8d4d2cb3cab28fccc151b2e458c39f0a349b20ca` | 将variable/flow优先于general/IniParam/resource；核对失败parser是否有注册/局部scope副作用及合法回退、unknown pool hook仍达。Include按长度switch只是分派，必须保留当前实际recursive来源归属/路径大小写边界、owner路径捕获、user最后读取和防重复终止；不能重引原始前缀分类。 |

### 下一批015的锁与事务接线核对

015补丁写 `CriticalSectionGuard(&G->mCriticalSection);`；另读取该提交的 `DirectX11/lock.h` 后，确认它是展开为命名 `CriticalSectionLockGuard` 变量的宏，并非语句末立即析构的临时对象。其底层类构造enter、析构leave且禁止复制/移动，不能仅凭调用外观报告锁寿命缺陷。适配时要同时带上实际guard定义或使用本地明确命名的RAII等价物，不能补造测试独有锁实现。

015仍在ClearKeyBindings/OverrideSave.Reset后才尝试保存，直接照搬会重新破坏本地失败事务，应继续让必要保存成功后才释放live状态；此外它将Constants执行后未知处理置于第二段临界区，必须审查两段之间的live指针和配置访问归属。

这一后续分析没有修改本批实现，也没有将后续条目标记implemented/validated。
