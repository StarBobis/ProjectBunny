# DX11 初始化同步适配

对应上游 `f8bcae61b9b9a0c49f6bfdae4a3bff76b9d914e7`。该提交把 gInitialized 从配置加载移至 DLL 初始化，但在加载配置之前解锁并发布 true；InitD311 的原始 hD3D11 快速返回也早于全部导出地址设置。仅搬移赋值不能证明完整发布。

本地将整个配置/目标检查/链式库加载/导出地址解析放在真实生产 InitializationGate 下。另一个线程等待；同线程递归返回失败，不能死锁或将半初始化状态当成成功。回调 false 或 C++ 异常会复位 gate 并通知等待者；完成后才置 gInitialized 并发布 READY。配置阶段另有checkpoint：完整配置只加载一次，导出解析失败的后续尝试只重试库解析；不会重复注册action或留下指向已销毁global的旧绑定。配置解析若抛出异常，阶段标FAILED并继续拒绝使用，不能安全回滚的部分配置不再自动重放。三处 Windows critical section 通过 InitOnceExecuteOnce 初始化，不在每次导出调用时重复初始化。

InitD311 的内部接口改为 bool，全部 D3D11/DXGI 调用方检查结果；HRESULT 返回 E_UNEXPECTED、内核 thunk 返回 STATUS_UNSUCCESSFUL、SIZE_T 查询返回0。目标进程不匹配时仍加载原始库以转发，维持旧行为。TLS hooking-quirk 快路保留；已核对正常设置它的生产路径在初始化成功之后。

生产 ResolveD3D11CreationExports 先解析必要的两个创建导出，保留已安装 hook 的原函数指针。缺导出时释放本次库引用、返回失败，不发布任何新转发指针。成功路径保留库引用，供进程生命周期内的转发使用。可选旧 DDI/11On12 导出维持原兼容行为；未将它们全部变为新初始化前提。

新增 initialization_gate_regression 已接入同一完整生产回归目标，实际执行：

- 系统 d3d11.dll 的创建导出解析、kernel32.dll 缺导出拒绝、现存函数指针保留。
- 生产 gate 的 false/异常重试、同线程递归拒绝、完成后不重复执行。
- 八线程受控初始化，回调未释放前不能返回，完成后读取同一非原子 payload，验证 gate 提供的发布可见性。

此测试执行生产 helper，没有调用整个 InitializeD3D11Proxy，没有进入代理 DLL 的真实加载 hook 或 InitOnce callback；不证明目标游戏首次多线程入口或部分 LoadConfigFile 异常后的全部状态回滚（当前明确拒绝重放）。LoadLibrary 硬失败仍沿用 DoubleBeepExit，不能据 gate 单元测试称该终止路径支持重试。实际整库加载、递归链配置、目标游戏兼容仍待相应环境验证。完整构建/整套回归的最终结果以 DX11-Upstream-Sync.md 对应批次为准。
