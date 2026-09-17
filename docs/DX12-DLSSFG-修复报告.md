# DLSS 帧生成：交换链重建失败修复

## 结论与证据

此次已定位并修复一条与现场错误吻合、可独立复现的生命周期缺陷：绘制缓存持有旧原生交换链的 `IDXGISwapChain3` 引用，但交换链 wrapper 销毁时没有清理缓存。旧 flip-model 交换链继续占用 HWND，使帧生成创建替代交换链失败。

游戏内最终效果仍需用户复测；本报告不把编译或 WARP 测试等同于 DLSS 游戏实测。

### 游戏日志

最新 `C:\Users\Administrator\AppData\LocalLow\miHoYo\绝区零\Player.log`，205–212 行：

```text
[DLSS/FSR/XeSS] Recreating swapchain for Frame Generation change
d3d12: CreateSwapChainForHwnd failed (80070005); swapchain creation aborted.
d3d12: RecreateSwapchainGfxWindow: CreateHWND failed — device cannot create a swapchain.
Crash!!!
```

2026-09-17 的三个崩溃目录 `Crash_2026-09-17_101207679`、`Crash_2026-09-17_101936912`、`Crash_2026-09-17_104901608` 内 `Player.log` 均记录了相同的 `80070005`。

代理日志 `d3d12_log.before_20260917-185134.txt` 中，旧主交换链被 release，随后 FG 以 `flags=0x842` 请求重建。旧代码无论成功或失败都记录 `swapchain_create_return`，因此该标记不是成功证据。现已补充真正的 HRESULT/结果对象日志。

原交接文档从崩溃处理阶段的等待栈推断“等待句柄永远不触发”“交换链已创建成功”，证据不足，且后一个结论被游戏错误日志直接否定。`STATUS_BREAKPOINT` 也不能单独证明引擎触发了卡死检测。

## 修复内容

### `src/DirectX12/Presentation/DX12FactoryWrappers.h`

- `SwapChainWrapper` 析构时先调用 `DX12NativeOverlayInvalidate(mSwapChain)`，清掉外部绘制缓存持有的原生交换链和后台缓冲区引用，再释放 wrapper 自有引用。
- `CreateSwapChainForHwnd` 返回后记录 `event=result`、`hr`、`hwnd` 和成功时的原生交换链地址，不再只能依赖执行阶段标记判断。
- 保留 wrapper 合法且配平的 v1–v4 接口缓存及基类所有权，不以删除正常引用掩盖问题。

### `src/DirectX12/ModRuntime/DX12CustomShaderDevice.h`

统一 `ReleaseChainResources()` 用于缓存失效、切换交换链和销毁：

1. 清理后台缓冲区持有引用。
2. 释放缓存的 `IDXGISwapChain3` QI 引用。
3. 清空交换链标识、有效掩码、RTV、尺寸和格式。

另外修复同一缓存中的两个真实所有权错误：

- 切换到另一交换链时，旧后台缓冲区指针在释放前被覆盖，造成引用泄漏。
- 不传 `backBufferOut` 时，旧代码把 `GetBuffer` 结果存入缓存后又立即 `Release`，使缓存没有自己的持有引用。常规 `[Present]` overlay 路径确实使用这个分支。现在缓存保留该引用，只有调用者要求返回资源时才额外 AddRef。

超过固定 RTV 槽位数的后台缓冲区索引现在安全返回失败，不再取模后错误引用另一缓冲区。稳态仍使用缓存，不引入每帧 QI、GPU 等待或额外队列提交。

`DX12NativeOverlay.cpp` 的失效接口注释同步说明 resize 和 destruction 两个入口。

## 验证

### TIMING 构建与部署

```powershell
.\build_release_timing_x64.ps1
```

成功完成，退出码 0。构建脚本已部署到：

```text
D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12.dll
```

部署 DLL 与 `x64\Release\d3d12.dll` 的 SHA256 一致：

```text
D05808FCE994A24E26083D21C9FBB01D2A95599EAB05696AF382067D3CDC876D
```

默认配置的另一个复制目标 `D:\Dev\ssmt4\src-tauri\resources\DX12\` 不存在，脚本跳过该目标；本次用户指定的 ZZMIDX12 目标复制成功。未自动启动游戏。既有轮转日志保留。

### WARP / DXGI 回归测试

新增：

- `dev_tests/surface_cache_lifetime_test.cpp`
- `dev_tests/test_surface_cache_lifetime.ps1`

测试脚本每次从生产头文件抽取完整 `DX12CustomShaderSurfaceCache` 类原文编译，不维护另一个模拟实现。测试使用真实 WARP 设备、真实 DXGI 交换链和隐藏窗口。生成的头文件、可执行程序和对象文件全部在 `build/surface-cache-*` 下。

```powershell
# 修复后的生产类
.\dev_tests\test_surface_cache_lifetime.ps1
# HEAD 中修复前的生产类；四个用例均应返回失败，作为负向对照
.\dev_tests\test_surface_cache_lifetime.ps1 -Baseline
```

| 用例 | 修复前 | 修复后 |
|---|---|---|
| 缓存失效释放 QI 引用 / 重复失效 | 引用未释放 | PASS |
| 不返回资源指针时仍持有缓存资源 | 未持有应有引用 | PASS |
| 切换交换链释放旧后台缓冲区 | 旧引用泄漏 | PASS |
| 同一 HWND 释放后重建交换链 | **`CreateSwapChainForHwnd hr=80070005`** | **连续重建 16 次 PASS** |
| 不返回资源路径 → 失效 → ResizeBuffers → 再取视图 | 未运行 | PASS |

初次测试对 QI 前后交换链引用数假设过强；真实 DXGI 后台缓冲区本身也会持有父交换链。测试已调整为在相同外部资源持有状态下比较清理后的引用数，最终上表全部验证通过。COM 引用数断言仅针对本机 WARP 实现；同 HWND 真实重建成功是独立的语义验证。

这些测试验证缓存本体和底层重建机制，不替代 wrapper 析构的完整游戏调用路径或 NVIDIA DLSS FG 实测。

## 用户复测步骤

1. 完全退出旧游戏进程，重新启动以加载新 DLL。
2. 保持 DLSS 帧生成开启，进入之前会闪退的 3D 场景。
3. 若能进入，继续切换一次帧生成开/关、切场景和调整窗口大小，确认无回归。
4. 若仍失败，保留新的 `d3d12_log.txt` 和 `Player.log`；重点检查 `DX12SwapChainCreate,event=result` 的 HRESULT，以及游戏内是否仍出现 `80070005`。

本次没有修改游戏设置、Mods，也没有关闭帧生成绕开问题。既有 dbghelp/等待诊断改动保留，没有把缺乏本次运行证据的 private COM 互操作疑点扩展为无关的大范围修复。
