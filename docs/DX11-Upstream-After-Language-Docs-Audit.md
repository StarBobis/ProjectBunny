# DX11 语言文档之后 25 项提交审查

只新增本审查文档及 build/upstream/after-language-audit 临时材料，未修改生产、测试、CMake 或同步账本，未启动构建。范围从账本 `9e75c140` 之后按拓扑选取接下来25个 pending 条目，包含PNG并行分支和8个merge；不是first-parent直线序列。每项读取完整diff，merge另检查父提交、`--cc`及`--remerge-diff`。文件级patch材料在build目录，不作为测试通过证据。

## 应先处理的依赖与风险

- 时间组：e850→474→be6→ca4。首帧旧tick为0会产生uptime长度frame_time；半径float→uint32存在非有限/越界风险。重新读取当前 `ResourceHash.cpp::UnpackCellCoords/SpatialDistanceChebyshev`，本地为12/8/12，X/Z环距最大2048、Y最大128，Chebyshev总最大2048；不能再引用旧10bit的512界限。保留read miss模板/不分配、32位NaN key、proxy和过期修复。
- resource属性组：9a→b0（PR merge80）→862→158→777。FillInMissingInfo并非纯格式，view union读取限制到buffer；格式/宽高是新增功能。宽高是完整资源尺寸，不能悄悄改成view mip大小。
- PNG/color组：bfbd→a2→863→d1→311→b7。旧分支涉及旧pool实现和FORCE_SRGB默认；只承接最终功能，保留本地WIC_DEFAULT、ComScope/owning factory、CMake/C++17、池metadata与清理。d1存在独有merge解决，其余7个merge重合并diff为空。
- 当前代码事实：Globals/HackerDXGI仍使用DWORD GetTickCount秒模型，无FRAME_TIME/FPS；CommandList仍旧FillInMissingInfo且暂无format/width/height或override_color_space/GetWICFlags。因此不能把本批功能误判已等价。既有InputLayout full-key缓存/游戏uncached政策、ShaderBindings原子输出与缓存同步、structured Store完整结构复制/readback桶共享，都只在merge父更新出现，不应回退。

## 逐提交结果

| 完整 SHA | 实际功能/内容 | 依赖、适配与验证要求 |
| --- | --- | --- |
| `e850bd7b8b6031fc325734664029678ec8302d97` | 微秒steady_clock TIME；64位ticks；util新GetSystemTicks。 | 与474/be6/ca4成组。本地仍DWORD GetTickCount和float秒，未等价；只需C++17 chrono，不升标准。保留本地ExpressionFunctions独立同步seed，勿回退共享非原子random。测试单调时间、长运行float精度边界；微秒采样分辨率不代表float TIME始终有微秒数值精度。 |
| `4742840607b17188953ad74419d4f826c4dfd36a` | FRAME_TIME运行参数和显式静态快照。 | 依赖e850。上游previous tick初始化0，首次Present会减出steady_clock uptime；初始化为launch tick或明确首帧策略。ret=nullptr保护沿用本地。测试首帧、正常delta、长暂停、默认优化不折叠/显式快照。 |
| `be6bd4eae63273e49bbff62a5e90e70e73597382` | FPSCounter指数滑动平均、FPS表达式和Present更新。 | 依赖e850/474。FPS不在upstream static_evaluate分支，保留runtime语义。本地未有FPSCounter。C++17 min/max防Windows宏。测试初始0、固定delta、EMA平滑、零delta、长暂停忽略、计时倒退/回绕策略；不把平滑FPS当瞬时计数。 |
| `ca4d732eaa99ccfea4461d7d7027291a7623ab98` | SPATIAL半径以120FPS参考和FRAME_TIME缩放。 | 依赖474，公开语义变化。直接ceil(float)→uint32会对NaN/inf/超界UB；先验证/在double运算，并按真实12/8/12 torus的最大Chebyshev距离2048封顶即可保持有限比较结果。上游首帧delta缺陷必须一起修。测试60/120/240FPS邻居阈值、零/长delta、read miss不分配和assignment重绑定不变。 |
| `4cd308a59810d983c3a1298464d49c3e96cbd8bf` | 禁止不同namespace覆盖非空CommandList（src非null时）。 | 独立行为门槛，非本地cycle缓存等价。精确比较目的section namespace，预检查后才设置runtime标记/入队；null清除仍允许，空callback跨namespace允许。测试同namespace非空、不同namespace空/非空、显式名字、pre/post任一非空、null清除和拒绝不改变旧引用。 |
| `6c9c2ac032a4002f78f3e39cf2893eb212fd177d` | v1.0.5 docs增加TIME/FRAME_TIME/FPS示例与跨namespacecallback限制。 | 仅两份Markdown，不改CMake。跟随上述功能复制/合并，不能直接报告微秒float精度；使用真实输出对应说明。检查相对链接与Linux大小写。 |
| `9a8d82dd895f2699c5063e869577261ceaf164d0` | FillInMissingInfo拆ViewInfo/GetViewInfo/资源format helper。 | 并非纯格式：view FirstElement/NumElements仅buffer view解释，避免texture view union误用。本地仍旧单体FillInMissingInfo，未等价。保留CB Region窗口、typed/raw/structured绝对字节模型；FirstElement*stride等用64位并夹实际ByteWidth。buf_size先赋完整buffer导致后if(!buf_size)通常不会取view范围，须按现有接口含义核对，不擅改成length。WARP SRV/RTV/UAV/DSV、texture mips、显式参数优先与COM释放。 |
| `b0c044e8b6bbde4408a0434b806ad97ef7edba6e` | 新增resource ->format/width/height、NOT_A_TEXTURE=-4。 | 可与9a/862/158组。格式优先custom override/cache再view/native；extent明确取整个resource而非SRV mip/view尺寸，1D height=1，buffer=-4，missing=-2。本地暂无这三getter，保留已有custom null guards和RAII。枚举mask增位不能覆盖本地Region/POOL/LAYOUT位。WARP texture1D/2D/3D/arrays/mips、typed/typeless view、buffer/absent/customoverride。 |
| `80edf0a3e006fdced665b2314ccc02fc8c385bb2` | Merge PR18（parents 9a8d82dd、b0c044e8）。 | --cc与--remerge-diff均无文件diff。父补丁分别适配后无独有解决可移植；不重复b0整个diff。不能因git show --stat很大就重复导入。 |
| `862d1b6f1015c23d40fcaca4fd97620ce9085ce0` | format helper改名GetTextureFormat；format getter独立查询view再native。 | 依赖9a/b0，实际fallback变化非纯rename：view UNKNOWN时回落texture native，不再依赖FillInMissingInfo是否有view。测试typeless native+typed view及未知viewformat回落，保留override优先。 |
| `158c8cdc790141b6124237ee8acec2745b600635` | 属性getter只向GetResource请求实际使用的输出参数。 | 依赖b0/862。逐hunk可按本地最终RAII最小输出承接，不能覆盖先前stride/size/null/Region/spatial bounds和Try零有效值。验证少传nullptr不会触发生产缺依赖、视图/引用平衡；不是新增属性。 |
| `777141aaf378c2c371c611adc4821508899645fc` | 表达式DXGI_FORMAT_*枚举字面量。 | 通过ParseFormatString转VALUE，无新增标准。上游前缀ncmp长度4与dxgi_format_实际12不符，适配精确前缀+完整ParseFormatString验证。测试大小写规范化、常见format数值、UNKNOWN/非法拼写、表达式静态求值与资源format比较。 |
| `bfbd33a3579369a17ca49ca9599c759f70f9753c` | 资源color_space=srgb/linear；WIC flags、DDS forceSRGB；池实例继承。 | PNG分支来自较旧主线，不能套旧FORCE_SRGB默认回退。两处日志把wstring直接传%ls需.c_str。本地当前仅WIC_DEFAULT且无override字段，未等价。必须metadata copy、pool模板/懒实例/副本/重载共同继承。DDS linear并无upstream强制去sRGB实现，不能照WIC承诺。真实PNG/DDS格式+像素回读、无tag默认、srgb/linear显式覆盖、未知值警告。 |
| `a2c0e172e0482664b979cff0bc3fbd06675461b5` | 自动扫描PNG头中IDAT前sRGB chunk并forceSRGB。 | 初实现FILE空指针、signature部分读未检查、FILE泄漏；chunk读取把1..3字节也视成功，未初始化数据、size+4溢出/fseek失败。必须使用最终311存在性/关闭语义并补完整长度/文件边界与RAII，不能复制这个危险中间体。沿用当前COM作用域和owning WIC factory。测试不存在/短签名/短chunk/大chunk/IDAT前后sRGB、普通非PNG。 |
| `86357a26f47defb21b4e1f3ccf7478144edc2f47` | GetWICFlags拆到CustomResource私有helper，新增WIC头可见性并fclose。 | 继承a2所有剩余边界风险，helper最终使用const wstring&避免不必要文件名复制即可C++17；不需要依赖升级或改DirectXTK COM。新增header若在公开.h引入需CMake包含路径现已具备。 |
| `d1deb9d25d8e81589c66c40331c2d09347ba1950` | PNG与8c1d58b7主线merge，存在真实解决。 | --remerge-diff确认：WIC调用选择GetWICFlags而非主线DEFAULT；CopyMetadataFrom补override_color_space；删除旧resources/ExpireResources池实现以承接主线新pool；另注释编码变化。必须保留新color字段通过当前CopyMetadataFrom，不能盲skip或覆盖新pool/ResetRuntimeState。combined只看++也能看到color字段，但remerge更完整。 |
| `311d285339309d1366a833c8aea40b3ee658679c` | PNG scanner先检查f != NULL，成功文件分支内fclose。 | 功能修复，不能只当格式；本地未来安全helper提前覆盖可记录。最终仍没修exact fread、chunk边界/seek错误。不存在文件应正常走loader失败而不崩；重复调用资源数/handle数不增长。 |
| `b7c2e77b9656a7a42351ae9d5f03bb92b8bc6969` | PNG file判断NULL改nullptr。 | 完整diff只这一比较，语义无新增；安全helper以nullptr/RAII实现可提前等价。无必要单独测试，沿文件失败回归。 |
| `7576bdabd0f9f3f5a101a45215d9fa869bf673db` | PNG分支merge主线f8a50f93。 | --cc/--remerge-diff均无文件diff，无独有解决；父主线更新已由此前逐提交工作承接，勿重新复制Shader/Context/ResourceHash旧文件。 |
| `26ec4b1a68cd9530855369d6c5caca0d0d7ccea3` | PNG分支merge主线609244f4。 | --cc/--remerge-diff均空；parent小变更并非新merge功能，按父证据处理，无独有解决。 |
| `393b396f37327e22404b511376cdff578c648a40` | FindTextureOverrides注释恢复en dash。 | 完整diff仅注释单行，功能无变化；本地该处已ASCII修正避免旧编码警告，保留ASCII即可记录无功能差异，不为这条引入UTF编码回退。 |
| `3c8d8f18449ba59ca3373e49b45795aae4edfd3d` | PNG分支merge84a7d071。 | --cc/--remerge-diff均空，无独有解决。父包含Reader/LOCKED/ResourceHash安全行为，保留当前实现，不重复覆盖。 |
| `19664553839e0ae2b66f19b4822c146b4b8df6bd` | PNG分支merge24e2cff7。 | --cc/--remerge-diff均空，无独有解决。父含shader declaration/parser及structured Store等已同步组，必须保留本地atomic输出、cache锁、合法structured复制和debug回归。 |
| `a3f014e5b79211cefdbb45ca24aa0d265b92aa05` | PNG分支mergea9d7c89b。 | --cc/--remerge-diff均空，无独有解决。父含ByteCodeReader搬移/cache并发/InputLayout政策/lockguard等；不能把merge stat当新的缓存实现重新移植而丢full-key/weak lifecycle/作用域COM修复。 |
| `b3a309b7c702bec944b8456f235ca4f84b9b9505` | PNG分支merge6c9c2ac0。 | --cc/--remerge-diff均空，无独有解决。父为time/FPS/跨namespace/docs组，按本表前六项处理；不可重复登记功能或整目录覆盖。 |

## 验证与可跳过边界

上述测试均是后续建议，未在本审查执行。新实现应使用真实生产target/WARP/配置加载验证，不用字符串断言代替功能回归。PNG malformed可用独占build临时文件；不存在文件、重复加载及COM apartment生命周期不能省略。

明确可作为无新增功能登记的是b7/nullptr与393/注释；7个空remerge的merge在父补丁已逐项承接后无需重复移植独有代码。d1必须单独记录其metadata传播和WIC选项选择。没有理由跳过时间/FPS、半径语义、跨namespace规则、新resource属性或color_space/PNG检测功能。所有本批源改动可保持C++17，无需复制vcxproj、升级标准、引入新图像库或恢复旧pool、哈希缓存和COM实现。
