# WoLong 启动崩溃：设备创建入口与交换链对象边界

## 证据与结论

检查对象：`D:\MMTCacheFolder\3Dmigoto\ZZMIDX12` 的 2026-09-19 13:46 日志、`d3d12_crash_20260919-134615.dmp`，当前源代码，以及 WoLong.exe 的 PE 导入表。没有启动游戏或向游戏再次注入。

- 日志记录 `0xc0000005`，`nvwgf2umx.dll+0x14bcce`，读取地址 `0xa0`，RCX 为零。这里是 NVIDIA 用户态驱动，不是游戏自带 DLL。
- 栈经过 `D3D12Core.dll+0x47078`、`WoLong.exe+0x161279f`。仅凭栈不能把根因归咎于驱动。
- 直接读取 dump 内存：调用上下文中的资源地址 `0x24551a0cbe0`，首指针为 `0x7ff95afec238`，落在我们的 `d3d12.dll+0x28c238`；对象 `+0x48` 为另一原生资源地址 `0x245590c0730`。结合既有栈内存分析里的 `ResourceWrapper::GetDesc` 和当前包装器布局，支持“代理资源进入原生调用链”的判断。
- 旧代码的 DXGI 工厂四个 CreateSwapChain 路径，无论调用方传入原生队列还是代理队列，都包装交换链；随后 GetBuffer 无条件返回 ResourceWrapper。这是可独立复现的对象边界错误。
- WoLong.exe 静态导入 `d3d12.dll` 的序号 **101、102**，分别对应 D3D12CreateDevice、D3D12GetDebugInterface；另有具名 D3D12SerializeRootSignature 导入。
- 当前 Run.exe 的常规启动路径为 ShellExecute + 全局 CBT hook。它保证 DLL 加载，不会自动把主程序已绑定到系统 D3D12.dll 的 IAT 导入项转到代理。原代码只有 DXGI 的导出 hook，没有对应主程序 D3D12 创建入口的补接管。

结论：不是靠增加延时解决的问题。这里同时存在**设备创建入口没有接管完整**、**原生/代理对象混用**和**启动日志未及时发布**。上述缺陷已修复；WoLong 整个启动流程及 Mod 效果仍需用户实机复测，不能由独立测试替代。

## 本次实现

1. 在目标进程 attach 时，仅重定向主 EXE 普通导入表中 D3D12CreateDevice / D3D12GetDebugInterface / D3D12GetInterface 的入口，兼容名称及对应序号。
   - 只替换仍指向系统导出的 IAT 项；既有其他 hook 不覆盖。
   - 使用原子比较交换；不全局 detour 系统 D3D12、驱动或 Streamline 私有模块。
   - detach 时仅恢复仍属于我们的项，不覆盖后装的 hook。
   - 不处理其他 DLL 的导入、延迟导入、动态 GetProcAddress，不能追溯替换注入前已经交付的设备。
2. 四个交换链创建路径统一检查调用方队列。代理队列正常包装；原生队列返回原生交换链，使 GetBuffer / GetDevice / QI 保持同一对象边界。
   - 原生回退会明确报告 Mod 未接管，不能把这种“不崩”计作完整功能成功。
3. 补充 DLL attach、IAT 转发、创建设备入口/返回值、交换链对象边界日志。
4. 首次 Present 之前把启动日志及时发布给异步写线程；之后保留原有批处理。修复日志路径反斜杠被吞，以及 wsprintfW 不支持 `%llu` 导致历史日志名变成 `d3d12_log_lu.txt` 的问题。

没有修改游戏文件、运行目录 d3dx.ini、注入延时或 Mod 配置。

## 验证

使用生产 TIMING DLL 和本机真实 D3D12/DXGI，而非模拟 COM 对象：

```powershell
.\dev_tests\swapchain_boundary_test.ps1 -Dll .\build\cmake-x64-release-timing\src\DirectX12\d3d12.dll -ImportMode ordinal
.\dev_tests\swapchain_boundary_test.ps1 -Dll .\build\cmake-x64-release-timing\src\DirectX12\d3d12.dll -ImportMode name
```

- 旧部署 DLL：在原生队列得到代理交换链的边界断言处失败；测试在向驱动传入错误对象之前停止。
- 修复后的对象边界测试：原生路径、代理路径、已有代理时仍使用原生设备别名，均验证 CreateSwapChain / CreateSwapChainForHwnd / CreateSwapChainForComposition。
- 检查后备缓冲身份、创建 RTV、ResizeBuffers 后再次 GetBuffer、GetDevice、无输出设备探测、无效交换链参数。
- IAT 测试通过真实 EXE 导入调用创建设备，不是手动调用代理导出来伪装接管成功；名称与序号 fixture 分开构建，两种均通过。名称 fixture 初次仍生成了序号导入，日志断言正确报错；检查 import library 确认后去掉名称 fixture 的序号声明，再运行通过，没有降低断言。
- 启动日志验证不调用 Present，确认少量日志仍落盘，且路径反斜杠保留。
- CoreWindow 路径经过编译和调用点检查，没有在独立测试里创建 UWP CoreWindow；游戏、Streamline/DLSS 和 Mod 绘制效果没有实机验证。
- 测试有意在释放 COM 对象后结束自身进程，不覆盖现有 runtime 的工作线程卸载流程。

构建使用现有 `build\cmake-x64-release-timing` 的同一 TIMING 配置增量构建。没有运行会删除整个 build 目录的清理步骤，以保留原有 dump 分析证据。

## 用户复测

保持原启动方式（Run.exe），完全退出游戏后重新启动一次，不需要先改 delay。

查看新日志：

1. 首行版本标记 `swapchain-boundary-v1`。
2. `DX12ImportRedirect,event=installed`，WoLong 对应序号入口应为 `byOrdinal=true`。
3. `D3D12CreateDevice,event=leave,...wrapped=true`。
4. 主游戏交换链 `DX12SwapChainBoundary,...mode=wrapped`，并进一步验证菜单、进场与 Mod 实际效果。

如果出现 `native_passthrough` / `unavailable_on_this_swapchain`，说明该调用方仍未被接管，不能按“Mod 正常”验收。如果出现 `foreign_hook_preserved`，说明已有其他导入 hook 被保留，需要结合新日志确认链路。若仍崩溃，保留新的 d3d12_log.txt 与 d3d12_crash_*.dmp；现在入口和包装关系会有直接证据。

`[Loader] delay=5` 在当前 Run.exe 中是成功加载后的退出等待，不是注入前等待；`dll_initialization_delay` 在当前 DX12 实现中没有对应读取，不能依赖它改变 DX12 初始化时机。

## 部署

已更新运行目录 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12` 的 d3d12.dll 和 d3d12.pdb，逐一核对与测试所用构建目录产物 SHA-256 相同：

- DLL：`A951DD8069D2E150E9DA17B79A9F2A5165950EADF15B46289838705AC1467CC4`
- PDB：`84CB88966963355693C5507BFDCD102F45EE3D2F3F66D99296D2576C8A264128`

未覆盖旧运行日志；下一次启动会由修复后的日志代码自动保留历史副本。

## 备份与回退

原 DLL、PDB、配置及最新崩溃日志/dump 已保存到：

`D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\backups\before_swapchain_boundary_20260919-142517`

回退时关闭游戏与 Run.exe，将该目录的 d3d12.dll、d3d12.pdb 复制回运行目录。配置本次未改，无需恢复。
