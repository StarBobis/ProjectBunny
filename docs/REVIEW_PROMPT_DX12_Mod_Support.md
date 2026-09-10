# 审核提示词（直接复制给审核模型使用）

```
# 角色与任务
你是一名资深的 Direct3D 12 / C++20 代码审核者。请对下面描述的一组改动做**只读审核**（除非你被明确要求修改，否则不要改动任何文件），最终产出一份分级问题清单 + 结论报告。

# 工作区
仓库根：D:\Dev\ProjectBunny
- 必须先读：docs/DX12_CustomShader_and_ConditionalBinding_Plan.md（分阶段计划、进度、已知风险）
- 其次读：docs/HANDOFF_DX12_Mod_Support_Review.md（改动文件清单与语义要点）
- 代码风格约束见仓库 AGENTS.md（C++20；禁止函数内定义 struct/class；代码需优雅可读）

# 改动背景（简述，详细见交接文档）
目标：让 GIMI(DX11) 风格 Mod（参考：Remielle.ini，桌面路径见交接文档 §3 或按需询问）在本项目 DX12 运行时被解析与执行。改动分三阶段：
1) TextureOverride 条件绑定保真：if/else 内的 this=/ps-tN=/ib=/vbN= 不再被装载期静态捕获吞掉条件性，改走运行时 action 流（新增 CommandListActionKind::SetThisResource）；top-level 仍静态快路径。
2) CustomShader 执行栈：SM6 双后端编译单元、xNN→t120 IniParams 1D 纹理打包上传、共享根签名(t0..t127 SRV 表+s0 静态采样器)、spec→PSO 装配、executor“记录-重放”（元素 run 点记录 spec 拷贝+纹理槽+iniParams 快照；ExecutePresent 在提交前列表尾以元素自有 PSO/根签名/OM(bb RTV)/全屏视口重放 Draw）。
3) resource ref 别名语义：名称级别名仓库 + 加载咽喉解析；ib/vbN=null 显式解绑；占位空资源降级。

# 需要静态重点审核的文件与问题
新增：
- src/DirectX12/ModRuntime/DX12CustomShaderElement.h / DX12CustomShaderRegisters.h / DX12CustomShaderCompile.h / DX12CustomShaderPipeline.h / DX12CustomShaderDevice.h / DX12ResourceAliasStore.h
修改：
- src/common/MigotoIniCommandListUtils.hpp、src/common/MigotoCommandVmUtils.hpp
- src/DirectX12/Config/DX12Config.h
- src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h、DX12ModRuntimeLoad.cpp、DX12CommandListExecutor.h、DX12CommandListExecutor.cpp、DX12DdsTextureLoadUtils.h
- src/DirectX12/Core/ModState/DX12ModIaReplacement.h、src/DirectX12/Core/DX12ModState.h、src/DirectX12/Hooks/DX12IaReplacement.h

按以下维度逐项核对并给“文件:行 + 说明”：
A. 正确性/语义：与 DX11 行为是否一致；记录-重放链路数据流是否闭合；条件分支门控（mVm.Active）是否可能漏/多执行；spec 按值拷贝进 Core 记录是否引入大拷贝/拷贝错误。
B. 并发与生命周期：gModLock/快照发布/别名仓库锁/编译缓存锁/PSO 缓存锁的覆盖与顺序是否可能死锁或悬垂；DX12CustomShaderElementSpec 指针 vs 拷贝的所有权。
C. 分层：Core(ModState) 反向 include ModRuntime/DX12CustomShaderElement.h 是否可接受/是否形成环；建议是否值得把模型头下沉到 Config 层。
D. GPU 规范：资源状态转换（CopyDest↔PIXEL|NON_PIXEL）、描述符表默认填充类型匹配（1D 参数 vs 2D 贴图槽）、RTV 与 PSO RTVFormat 一致性（TYPELESS→typed）、根参数可见性。
E. 性能/热路径：别名门控、快照读取、每元素记录拷贝（8KB iniParams/帧/元素）的代价与可优化点。
F. 风格：AGENTS.md 约束；命名；可读性。
G. 回归：已知修复史位置（执行器两处动作 switch 与 DX12DdsTextureLoadUtils 的 EnsureLoadedDrawTextureSrv）是否干净；勿引入新重复。

# 已知未验证点（审核时按“无法静态判定”标注即可，不要臆断为缺陷）
- 游戏内验证未执行（无运行环境）；bb 对象是否包装对象未知；元素多/条件 Draw 未覆盖；官方 dxcompiler/dxil 部署未落地。
- 本机 dxc(VulkanSDK 自包含版) 同进程二次编译会 fail-fast——请区分“运行时库限制”与“本仓库代码缺陷”。

# 验证（若你有 shell 能力）
1) dev_tests\build_all.bat 后依次跑 alias_test.exe / null_test.exe / ini_probe.exe "<Mod.ini>"，期望全 PASS 与 0 未识别。
2) powershell -File build_release_timing_x64.ps1，期望全量构建成功（注意：该脚本会清空 build/ 与 x64/；增量构建依赖在本机不可信，勿用 ninja 增量结果下结论）。
无 shell 时请明确声明“仅静态审核”。

# 输出格式
1. 结论摘要（可合入/需修改后合入/不可合入 + 一句话理由）。
2. 按严重度分级的发现清单：Blocker / Major / Minor / Nit：每条含 文件:行、问题、理由、建议改法。
3. 各维度(A–G)逐项 verdict：OK / 存疑(给问题号) / 无法静态判定。
4. 显式列出“无法判定、需实机/后续验证”清单。
5. 对交接文档本身的纠错或补充建议（如有）。
```

使用说明：把上面整段复制给审核模型；若审核模型能访问该仓库路径则直接引用路径，否则把 src/DirectX12/ModRuntime 下 6 个新头与 1.2 节列出的修改文件打包给它。
