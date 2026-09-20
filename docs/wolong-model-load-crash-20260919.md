# WoLong：14:43 模型加载阶段崩溃调查

## 当前结论

**用户用诊断版再次复现后，15:13 的新 dump 已确认一个实际根因：PSO 缓存按值复制后，shader 描述符指针仍指向已销毁的临时字节码。该悬空指针问题及同一描述符的相关存储寿命已修复，并通过回归验证。游戏实机仍需复测，不能断言此前 14:43 的空锁异常一定与它同根。**

下文先保留 14:43 首次调查及诊断增强的过程，末尾追加 15:13 复现后的确定结论和修复结果。

前一轮启动修复已在游戏实机日志里得到验证：主程序的两个序号导入完成重定向，D3D12CreateDevice 返回 `wrapped=true`，交换链为 `mode=wrapped`。游戏运行约 222 秒、Present 计数达到 11288，此后发生另一处 CPU 访问异常。

## 已检查的证据

- 运行日志：`D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt`，约 32 MB，只采样开头、崩溃前尾部和相关事件。
- 本次小 dump：`d3d12_crash_20260919-144346.dmp`。
- 同时刻 Windows dump：`C:\Users\Administrator\AppData\Local\CrashDumps\WoLong.exe.2592.dmp`，211086494 字节。两份 dump 的异常线程及 RIP 相同。
- 主程序 PE / RTTI，以及包装器生命周期和当前崩溃报告实现。

关键事实：

1. 异常线程 34132，`0xc0000005`，写地址 `0x18`。
2. RIP：`ntdll.dll+0x1283d`，即 `RtlEnterCriticalSection+0x4d`。
3. 直接读取 dump 指令：`mov rdi, rcx` 保存传入锁地址，故障指令为 `lock btr dword ptr [rdi+8],0`，RDI=`0x10`。这说明锁参数接近空指针，不是锁竞争超时；寄存器 RCX 在函数内部已改变，不能把异常时的 RCX=1 当作入口参数。
4. 调用者：`WoLong.exe+0x162bb29`、`+0x162bad1`、`+0x13f8a51` 等。故障栈没有正在执行的代理函数，但不能据此排除先前生命周期或状态修改的影响。
5. 崩溃前约 78–125 ms 有集中释放记录，涉及命令列表、描述符堆和资源。此前最后一个 Present 的 HRESULT 为 0。没有找到已记录的设备移除或失败 HRESULT，不能据此推导所有历史 API 都成功。
6. 堆中邻近游戏对象的 vtable / 磁盘 RTTI 名称包括 `ktgl::CDX12GraphicsDevice`、`ktgl::CDX12ImmediateGraphicsContext`、`ktgl::CShaderFile`、`ktgl::CDX12HLPixelShader`。这些只是附近对象的类型，不足以确认空对象归属或释放者。

## 为什么现有 dump 还不足以定根因

两份 dump 都未包含 `WoLong.exe+0x162bb29` 和 `+0x162bad1` 的运行时代码。磁盘 EXE 对应代码字节不能可靠代表运行时执行内容，不能用它反汇编出的无意义指令作因果依据。下一步必须知道调用者从哪个成员/全局变量取出了空对象，以及相关检查分支；更改延时、引用计数或吞掉异常都没有足够证据。

## 本次诊断修改

- 在故障线程栈回溯时，选择每个 PC 所在页及前后相邻页。
- 仅选择同一内存分配中的、已提交且可读取的可执行页；跳过 NOACCESS / GUARD 页，去重，最多 144 个 4 KiB 页，即额外代码数据不超过 576 KiB（不含 dump 自身元数据）。
- 通过 MiniDumpWriteDump MemoryCallback 显式包含这些页，不依赖调试器之后从 EXE 文件恢复代码。
- 日志新增 `DX12CrashCode,event=requested,address=...,bytes=4096,fnv1a=...`。校验值可检查 dump 字节与采样时是否一致，`requested` 不表示已经写盘成功；仍须检查 dump 成功记录与实际内存流。
- 正常渲染路径没有新增采样开销；没有修改游戏对象生命周期、资源内容、Mod 配置或注入时机。
- 新构建标记：`swapchain-boundary-v1+crash-code-v2`。

## 实际验证

`dev_tests/crash_report_test.ps1` 使用生产崩溃报告源码，执行三个独立子进程崩溃场景：无日志写线程、写线程可停、写线程无法停。

测试的故障代码动态生成在内存里，磁盘不存在对应机器码，旁边设置 NOACCESS 页。三个场景均通过：

- 异常记录、栈、面包屑、原异常处理器链和日志交接保持有效。
- 逐页读取生成 dump 的 MemoryListStream，核对**所有**日志声明的代码页与 FNV-1a 校验值。
- 动态代码页确实可读回，不可访问页未被加入，页数上限符合约束。

此测试证明新增取证能力，不证明 WoLong 加载模型不再崩溃。

## 部署与回退

已完成同一 TIMING 配置的增量编译，以及生产 DLL 的序号导入/交换链烟雾回归测试。已更新运行目录的 d3d12.dll、d3d12.pdb，并核对部署前后 SHA-256 一致：

- DLL：`56DDE16BA34C7C4EB7E96A17A59EE3591AF7AA02645565DA490401BF9E1746B2`
- PDB：`DBBCE4CC6C228FA2226E605C0767DBC98FC92C3F3C7EDD0A77FB82ABDE979386`

旧 DLL/PDB、配置、本次日志和小 dump 已备份到：

`D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\backups\before_crash_code_v2_20260919-145715`

没有改写当前运行日志，下一次启动会保留它的历史副本。回退时关闭游戏与 Run.exe，把备份中的 d3d12.dll、d3d12.pdb 复制回运行目录即可，配置无需恢复。

## 用户复现后的新证据（15:13）

用户已用诊断版再次复现，生成 `d3d12_crash_20260919-151341.dmp`（1885156 字节）。已逐页读取实际 dump，25 个请求代码页全部通过日志 FNV-1a 校验。

本次不再仅有游戏内部空锁栈，而是直接落在：

- `DX12WrapperDetail::GraphicsCommandListWrapper::SetPipelineState+0x4d0`，`d3d12.dll+0x70320`。
- 故障指令 `movzx eax,byte ptr [rdx+r9]`，RDX=0、R9=`0x233238e8040`（无效读地址）。
- 上下文中的哈希初值 `0xcbf29ce484222325`、乘数 `0x100000001b3`，长度 `0x48e8`（18664 字节）。
- 捕获代码显示它从 PSO 缓存节点的 VS 描述符读取地址/长度，进入 `GraphicsPsoNeedsReplacementLocked` 内联的 shader FNV-1a 哈希循环。

对应源码缺陷：

1. DeepCopyGraphicsDesc / DeepCopyComputeDesc 将 shader 字节码放进局部 `DX12StoredPso record` 的 vector，并把描述符指针指向该 vector。
2. `gPsoRecords[pipelineState] = record` 默认按值复制 vector，但指针字段仍指向局部 record 的存储。
3. 局部 record 析构释放存储；稍后的 SetPipelineState / replacement / reload 读取缓存描述符，形成 use-after-free。

这与新 dump 的无效读取位置吻合。原实现的独立负例也在 Graphics / Compute 两条发布路径均稳定失败，无需依赖“被释放内存恰好不可读”才复现。

## 确定问题的修复

- 缓存发布改为移动，转移实际持有的 vector 存储，不再复制出错误的内部指针。
- DX12StoredPso 禁止隐式复制，防止同类错误重新引入；移动构造/赋值保持可用。
- 同一持久化描述符中的输入布局数组、语义字符串（含短字符串）、StreamOutput 声明/字符串/stride，以及 CachedPSO 字节，也改为拥有自己的存储，避免稍后替换 PSO 时读取调用方临时数据。
- 替换 PSO 的创建快照显式复制数据并统一重新绑定内部指针，再沿现有锁内 AddRef / 锁外创建 / Release 链路使用。
- 没有通过关闭 Mod、禁用 shader 检查或吞掉访问异常来避免崩溃。

## 新增回归验证

```powershell
# 原版本：预期在 Graphics / Compute 的缓存所有权断言处失败
.\dev_tests\stored_pso_lifetime_test.ps1 -Baseline
# 修复版本：全部通过
.\dev_tests\stored_pso_lifetime_test.ps1
```

测试提取真实生产记录定义、复制 helper、两处发布表达式及 replacement hash 谓词；仅用简单 hash-membership 表替代无关的 Mod 配置，未模拟 shader 存储操作。验证：

- Graphics 五阶段与 Compute 字节码描述符均指向缓存自身拥有的数据。
- 调用方和临时记录销毁后仍保留原始 payload。
- 输入布局、短/长语义字符串、SO 空语义 gap、stride、cache blob 的完整所有权。
- 创建快照不依赖原缓存，缓存清除后仍可使用。
- 实际 replacement 哈希谓词的命中与不命中路径。
- 移动构造/赋值、已有记录替换、map rehash、空可选字段。

该测试验证 CPU 所有权及读取逻辑，不替代游戏或驱动的完整 PSO 替换运行验证。

## 最终修复版部署

TIMING 增量构建通过；生产 DLL 的序号导入、名称导入及 native/wrapped 交换链边界回归均通过。修复版已部署到 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12`，复制后逐文件核对 SHA-256：

- d3d12.dll：`B4F8E0AAA05C3486BE11BB6E151B4731FC0B506BE27EE9C36C855DF1E41C8507`
- d3d12.pdb：`D8DEE60B940AD535C8899678A81C9AC47B163C5AC5ABBBF91356FE2194DF6563`

部署前检查游戏和 Run.exe 均未运行，并确认备份与部署前 DLL/PDB、配置、日志、新 dump 的哈希一致。第二份备份目录：

`D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\backups\before_pso_owned_v3_20260919-152502`

回退时关闭游戏与加载器，把该目录的 DLL/PDB 成对复制回上级运行目录，即恢复到诊断版。没有改动游戏 EXE、d3dx.ini 或 Mod 文件，也没有自动启动游戏。

## 最终复测要求

使用新修复版按原方式启动游戏，日志标记应含 `pso-owned-storage-v3`。重走同一模型加载场景，并确认 Mod 与渲染效果。如果仍崩溃，继续保留新日志和 dump；现有代码页采集会保留，不需要再部署一次纯诊断版本。

此前 14:43 的空锁调用点，借本次捕获代码已进一步定位为游戏从即时上下文成员 `+0x750` 取出空值，再把 `+0x10` 当作锁传入。尚无证据证明它与此次 shader 字节码悬空一定同根，不能提前宣称也已消除。
