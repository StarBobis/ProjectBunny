# 交接文档：DX12 Mod 能力补齐（3DMigoto 兼容层）改动审核材料

> 面向对象：接手审核的工程师 / 模型。
> 仓库：`D:\Dev\ProjectBunny`（C++20，MSVC，CMake+Ninja，DX11/DX12 双管线）。
> 上下文：目标是把 GIMI(DX11) 风格 Mod（参考 Mod：桌面版
> `Remielle Blancheplume and Noireplume full ver 4.3`，2118 行 ini）搬到本项目 DX12 运行时可解析、可执行。
> 全程验证基线：`dev_tests\`（离线单测/探针）+ `build_release_timing_x64.ps1` 全量 clean 构建（38/38）。

## 0. 结论速览

- 解析层：对参考 Mod 全文件 **0 未识别行**（探针断言 18 项全 PASS）。
- 模型层：阶段1 条件绑定保真、阶段2 CustomShader 元素/编译/PSO/记录-重放、阶段3 ref 别名均已实现并编译通过。
- **未做**：游戏内验证（本机无可用游戏会话），阶段2 重放与阶段3 捕获模式的实机正确性未判定。

## 1. 改动文件清单

### 1.1 新增源文件（`src/DirectX12/ModRuntime/`）

| 文件 | 职责 | 关键设计点 |
|---|---|---|
| `DX12CustomShaderElement.h` | [CustomShaderX] 段装载期模型 | 纯解析层重建（无第二解析器）：stage 文件槽、blend/cull/front/topology/sample_mask、oN→bb 标志、stateLines 原文保留 |
| `DX12CustomShaderRegisters.h` | xNN 寄存器↔float4 打包 | t120 IniParams 契约；`x87/y87/z87/w87 → slot[87].xyzw`；纯函数可单测 |
| `DX12CustomShaderCompile.h` | SM6 编译单元 | DXC 动态装载优先（dxcompiler 与 dxil 同目录配对、进程内单次常驻），d3dcompiler+dxil 兜底；按 (路径, profile) + 修改时间缓存；`DX12CustomShaderBytecode` 统一持有 ID3DBlob/IDxcBlob |
| `DX12CustomShaderPipeline.h` | 根签名/PSO 装配（设备无关） | 共享根签名：1 个描述符表（t0..t127 SRV）+ s0 静态采样器；`D3D12SerializeRootSignature` 走动态解析（工程不链 d3d12.lib）；TYPELESS→typed RTV 格式推导 |
| `DX12CustomShaderDevice.h` | 设备侧对象 | `DX12IniParamsTexture`（1D 512×float4 + 4 槽上传环 + 跨帧 barrier）；`DX12CustomShaderTableHeap`（shader-visible 128 槽，默认填 1×1 黑色 2D 防类型不符）；`DX12CustomShaderSurfaceCache`（按 (swapchain, index) 缓存 bb RTV）；`DX12CustomShaderPipelineCache`（根签名/PSO 设备缓存） |
| `DX12ResourceAliasStore.h` | ref 别名仓库 | 名称级别名（键大小写归一），链解析 ≤16 跳、环回退、原子门控零开销路径、`BindingNameWithoutRefPrefix` |
| `DX12DrawTextureCaptureUtils.h` | 捕获模式 | `X = ref ps-tN` 绘制期活引用捕获（AddRef 游戏资源 + 原 SRV desc 建自有 SRV），注册进 gLoadedResources；占位资源捕获前 failed、捕获后替换；重捕获 Retire 延迟销毁 |

### 1.2 修改文件

| 文件 | 改动摘要 |
|---|---|
| `src/common/MigotoIniCommandListUtils.hpp` | `IsPostVariableAssignmentKey` 接受 `post $x` 与 `post xNN`（延迟常量赋值） |
| `src/common/MigotoCommandVmUtils.hpp` | post 队列支持常量目标（`PostAssignment{name, expression, constantTarget}`），冲刷走与即时 xNN 一致的写路径 |
| `src/DirectX12/Config/DX12Config.h` | 新增 `CommandListActionKind::SetThisResource` 与 `SetShader` 增加 hs/ds/gs；TextureOverride 解析分支深度感知（顶层静态 / 嵌套进 action 流），`this=` 嵌套→SetThisResource、顶层保持静态字段；`hasConditionalThisReplacement`、`explicitNullBindings`；`ib/vbN=null` 不再产生垃圾"Resourcenull" |
| `src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h` | 元素 registry 全局 + 代际不可变快照；`gResourceAliases`；别名 store 实例 |
| `src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp` | 装载 registry/别名清空/发布；元素注册 TIMING 诊断（`DX12CustomShaderDiag event=element_registered`） |
| `src/DirectX12/ModRuntime/DX12CommandListExecutor.h` | 执行器：`SetThisResource` 运行时（分支门控，经 DrawSrv 补丁替换 this 绑定）；元素拦截→**记录** `CustomElementRecord`（spec 按值、调用方像素槽绑定捕获、iniParams 快照）；`RegisterResourceAliasFromAction` |
| `src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp` | `DX12ModExecutePresentCustomElements` 重放主链（见 §2.4） |
| `src/DirectX12/ModRuntime/DX12DdsTextureLoadUtils.h` | `EnsureLoadedDrawTextureSrv`(+DeviceOnly) 入口 `ref ` 剥离 + 别名解析；占位空资源（无 filename）标记 failed 并降级（捕获落地后替换） |
| `src/DirectX12/Core/ModState/DX12ModIaReplacement.h` | `CustomElementRecord` 队列（含 spec 按值拷贝）；拷贝/移动/赋值同步（审核中发现四个特殊成员函数曾遗漏 `customElementRecords`，已修复） |
| `src/DirectX12/Core/DX12ModState.h` | 重放函数原型 |
| `src/DirectX12/Hooks/DX12IaReplacement.h` | `ExecutePresent` 末尾调用重放 |
| `src/DirectX12/Core/CommandState/DX12SimulateContext.h` | 新增 OM RTV/视口/剪刀影子字段（重放还原用） |
| `src/DirectX12/Presentation/DX12CommandListWrapper.h` | `OMSetRenderTargets`/`RSSetViewports`/`RSSetScissorRects`/`ClearState` 维护上述影子 |

### 1.3 非源码

- `docs/DX12_CustomShader_and_ConditionalBinding_Plan.md`：阶段计划/进度/游戏内日志验证指引表。
- `dev_tests/`：常驻离线验证（`ini_probe`、`alias_test`、`null_test`、`build_all.bat`、README）。

## 2. 设计语义要点（审核需重点核对）

1. **DX11 语义 → DX12 架构映射**
   - `xNN` 写读：VM 全局符号（`AssignSymbol`），重放前打包进 t120 1D float4 纹理（对应 draw_2d.hlsl 的 `IniParams[87].xy` 等）。
   - 自定义元素 = 记录-重放：executor 在元素 `run` 点记录（spec 拷贝 + 纹理槽 + iniParams 快照），`ExecutePresent` 以元素自有 PSO/根签名/OM(bb RTV)/全屏视口重放。**注意**：`ExecutePresent` 实际在 Present 后首个被拦截的 Draw 处触发（`ExecutePendingPresentBeforeFastReject`），同一列表上其后仍有游戏 Draw，并非"列表尾"；因此重放结束后会从影子状态还原游戏堆/根签名/PSO/拓扑/OM/视口/剪刀（早期版本"无需还原"的假设不成立，审核中已修复）。
   - `this=` 条件替换 = SetThisResource action（VM 分支门控）→ `AddThisRequestFromMatch` 生成 drawSrvPatch。
   - ref 别名 = 名称级 store（加载咽喉解析）；`X = ref ps-t0` 捕获模式已实现（`DX12DrawTextureCaptureUtils`：活引用 AddRef + 原 SRV desc 视图，无 GPU 拷贝；重捕获 Retire 延迟销毁）。
2. **关键风险点**（设计时已意识、仍未实机验证）：
   - bb 对象包装性：若交换链 GetBuffer 返回包装对象，RTV 创建可能失败（日志 `replay_skip reason=no_back_buffer` 可见）。
   - **帧时序语义**：Present 替换（含自定义元素）在下一帧首个被拦截 Draw 处重放；UI 覆盖层会画在
     下一帧场景渲染之前——若游戏随后全屏重绘场景，菜单会被盖住。这是该架构 [Present] 的既定时序，
     是否正确取决于游戏首 pass 形态，必须实机确认（`element_drawn` 日志 + 截图）。
   - 元素体多 Draw/条件 Draw：当前只记录"首个普通 Draw"参数，条件/多 Draw 退化为默认 4 顶点 strip。
   - 编译后端：本机 VulkanSDK 自包含 dxcompiler 同进程二次编译 fail-fast(0xC0000409)；官方 dxcompiler/dxil 需随 d3d12.dll 分发（未落盘）。
   - 增量构建依赖不可信（cmake msvc_deps_prefix 与本地化 cl 前缀不匹配）——**必须以 clean 全量构建验证**。
3. **审核修正史（本轮审核发现并立即修复）**：
   - DXC 编译路径 `DXC_OUT_OBJECT` blob 双重 Release（`DxcComPtr` 与 `FromDxc` 重复持有）→ 改为 detach 后转移所有权。
   - `DX12ModIaReplacement` 拷贝/移动四个特殊成员函数遗漏 `customElementRecords` → 已同步。
   - 重载不清自定义着色器缓存（`ClearAll`/`ClearCache` 无调用点，PSO/字节码跨代残留）→ 重放入口按重载代际惰性清理。
   - `_control_point_patch_list` 后缀长度 off-by-one（25≠26，patch 拓扑永不解析）→ 命名常量修复。
   - 表堆/ini 参数量化布局：**跨帧** 4 槽轮换 + **帧内** 每元素独立区域
     （kMaxCustomElementsPerFrame=64；超出记日志跳过而非损坏先绘元素）——描述符写与上传 memcpy
     都是 CPU 侧、先于 GPU 执行生效，同帧共享区域会让所有元素读到最末元素状态（复审发现并已修复）。
   - IniParams 上传同理：`UpdateAndUpload(commandList, present, elementIndex, data)` 按
     (present%4, elementIndex) 二维定位上传区域，CopyTextureRegion 列表内有序。
   - 重放泄漏 OM/视口/剪刀/堆/根签名/PSO/拓扑到后续游戏 Draw → 包装层影子跟踪 + 重放后还原。
   - `DX12IniParamsTexture` 部分初始化失败后永久楔死、`DX12CustomShaderSurfaceCache` 空 RTV 堆描述符写入、`ref ` 大小写敏感、patch 控制点未钳制、`EnsureLoadedDrawTextureSrvDeviceOnly` 漏别名解析、PackFloat4 每元素 2048 次全局锁 → 均已修复。
   - 执行器开关内曾出现误插导致的重复 case/游离代码与 DdsUtil 重定义（更早修复史），复查位置整洁。

## 3. 验证方法与复现

```bat
:: 1) 离线单测
dev_tests\build_all.bat
dev_tests\alias_test.exe      :: 期望 ALL PASS（12 项）
dev_tests\null_test.exe       :: 期望 ALL PASS（7 项）
dev_tests\ini_probe.exe "<Mod 根>\Remielle.ini"
                              :: 期望 total unrecognized: 0（19 项断言全 PASS）

:: 2) 全量构建（唯一可信基线；产物部署到 D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\）
powershell -File build_release_timing_x64.ps1   :: 期望 "All projects built successfully" 38/38
```

## 4. 待办（未完成，供审核确认不回归即可）

- 游戏内验证（需可运行会话 + `d3d12_log.txt`，标记见计划文档 §游戏内验证指引）。
- ~~捕获模式 `X = ref ps-tN`~~（第14轮已实现：活引用捕获，见 §2）。
- 官方 dxcompiler/dxil 部署（第14轮已加部署策略：仅 `third_party/dxc/` 官方对自动拷贝；
  VulkanSDK 自包含版二次编译 fail-fast 会崩游戏，永不自动部署；缺失时优雅降级）。
- 元素体条件/多 Draw 解释。
- 外部环境依赖：`CommandList\ZZMI\*`/`CommandList\RabbitFX\*`/`CommandListSkinTexture` 由
  ZZMI 基础 ini 与其他 Mod 提供；缺失时 `run` 优雅空转（本 Mod 中间 pass 纹理交换随之退化）。
  `BuiltInCommandListUnbindAllRenderTargets` 在本架构下语义等价于空操作（重放始终自建 bb RTV）。
