# 第二轮：全部336项对抗审查

基于本地 `38282b7` 和上游 `eae473b3`，按原同步账本的336个唯一SHA逐项覆盖，包括被跳过、合并及撤销的历史。目标游戏测试仍按用户决定暂缓。

机器记录见 [逐项覆盖清单](dx11-adversarial-round2.json)。分工为1–84、85–168、169–252、253–336；互斥写入独立证据，主代理汇总并复核发现。未读完整补丁或未追到当前实现的项保持 pending，不以提取文件、旧测试通过或抽查代替审查。

`reviewed-static-no-confirmed-gap` 仅表示本轮主代理静态审查暂未确认问题；`agent-reviewed-static` 表示子代理已留下逐项证据，`parentVerification` 单独记录主代理复核状态。两者均不表示新运行过测试，也不等于无差异证明。疑点必须保留触发条件及两侧路径，复现、修复与运行证据随后追加。

审查区分：遗漏/错误适配、必要编译适配、明确的上游缺陷修复、尚待判断的行为取舍。不能只用“更安全”否认有效输入上的差异，也不能为字面一致盲目恢复上游缺陷。所有确认的修复均需真实生产路径回归并通过适用构建后再推送。

## 本轮结果

336/336 个提交均有完整补丁阅读、当前生产路径及反例挑战记录，静态待审为 0。子代理互斥覆盖 1–84、85–168、169–252、264–287、288–311、312–336，主代理覆盖 253–263，并独立复核发现、复现与集成结果。`parentVerification` 区分接受委派证据和独立复核发现；主代理没有重新逐行阅读全部子代理补丁。静态覆盖不构成任意输入下的等价证明。

本轮确认并修复以下五项适配偏差；均使用真实生产逻辑回归，先观察失败再验证修复。

| 问题 | 复现及修复 | 回归文件 |
| --- | --- | --- |
| SpatialHash 落格精度 | `.5f/.1f` 上游 float 除法落格 5，本地提前提升 double 落格 4。恢复 float 除法，保留极大有限值的已定义回退。 | `round2_spatial_boundary_regression.cpp` |
| SpatialHash 公开参数单位 | 上游参数是 float32 元素索引；本地误当字节。恢复一次乘 4，再加字节单位的 CB/VB/IB 基址；内部缓存 API 仍用字节。真实非零 CB 窗口验证 `(0,1,2,.1)`。 | 同上 |
| SetHandleInfo 异常锁泄漏 | 对真实分配分别注入 ResourceHandleInfo、嵌套 RegionHashesCache 的 bad_alloc，另一线程观察锁。手工 Enter/Leave 改为 RAII，覆盖正常、重复及失败重试。不是全项目 OOM 路径证明。 | `round2_handle_info_exception_regression.cpp` |
| 重载保存锁范围缺失 | 真实 MoveFileExW 提交边界允许持 G 的真实 PERSIST 命令插入，随后被重载覆盖。将 save/wipe 与注册表替换恢复到同一 RAII 锁范围，保留保存失败不清旧状态。 | `round2_reload_snapshot_regression.cpp` |
| Region 跨物理末尾错误拒绝 | 2048B、stride4 VB 的 `Region(1792,512)` 应分配 512B 并复制可用 256B 前缀。恢复有效起点的前缀复制，目标尺寸限制到请求/物理大小，再应用 CB 对齐和 64KiB 限制。尾部未定义，不作零值断言。起点在末尾或之外仍拒绝。 | `round2_region_clamp_regression.cpp` |

所有回归文件位于 `src/DirectX11/tests/`，接入 CMake 与现有完整生产回归入口。旧的公开 SpatialHash 参数示例、Region 跨尾拒绝断言及对应语言文档已同步修正；内部字节偏移测试保留。

## 明确保留的兼容差异

以下差异会影响有效配置的输出，不能说当前 DLL 与上游完全相同：

- **Custom Stride**：上游无 override 的 custom 分支落入 CB 分支，返回 16；本地返回实际捕获/原生 stride。真实 VB stride4/8 回归固定本地行为。
- **捕获时 CPU 快照**：上游 partial write 改写共享捕获缓存，却不一致地失效各自 L2，结果可依赖查询先后。本地 COW 保留捕获时数据。真实 tracked partial copy 回归验证源 CPU/GPU 新值与 warm/cold 捕获旧值；这不表示 ref 的 GPU 资源也成为独立副本。
- **Mods 输入作用域**：上游提交219删除递归根集合唯一插入点，tip 下 MODS 分类失效；本地保留真实递归来源。普通 `include_recursive=Mods` 与 mods-only 输入禁用即可产生行为差异。依据完整源链判断，未运行上游 DLL 作输入对照。

前两项的真实生产 WARP 回归为 `round2_snapshot_stride_regression.cpp`。保留这些已有本地功能，而非为逐字一致恢复上游缺陷。另保留 COM apartment 平衡、浮点下溢、完整缓存键/失效及非法参数检查等已有适配；本轮没有撤销它们。

Standalone pool range 虽被上游解析器接受，但实际忽略 bounds 并操作整个 pool；上游文档只允许 pool range 与 slot range 配对。本地继续拒绝这种未支持的独立范围写法，正常双向 slot/pool range 保留。

## 验证和证据边界

- 项目脚本环境中的 CMake Ninja Release：DirectX11 和 `dx11_upstream_regression` 构建通过。
- 完整 `ctest --test-dir build/dx11-upstream-en --output-on-failure --timeout 30`：3/3 通过（16.06 秒），包括版本兼容、版本生成及真实生产/WARP 回归。
- `build/upstream/test-dx11.cmd`：pool flags 与 WARP resource properties 独立回归通过。
- 隔离目录 DirectX12 兼容构建通过；没有部署 DLL。
- 文档链接/锚点/代码围栏检查：38 页、87 个本地链接通过。

提交前最终复核：DirectX11 与回归目标再次构建通过，完整 CTest 再次 3/3 通过（15.66 秒）；`git diff --check` 和 336 项账本唯一性、顺序、完整阅读标记检查通过。

本轮 red/green 构建、CTest 日志和子代理源链证据保存在 ignored `build/upstream/adversarial-round2/`。持久记录为本文及 JSON 账本；临时日志未作为永久证据提交。逐项 `parentDisposition` 说明跨提交发现及最终处理，原同步账本不因静态审查而升级目标运行状态。

持久化锁修复恢复上游 reload 事务边界；上游和本地既有部分命令写入口并不统一持 G，不能据此声称所有命令并发均已线程安全。现有 WARP 的受限非零 array-slice GenerateMips 验证限制也仍保留，未通过关闭断言掩盖。

按用户决定，未启动目标游戏、未部署 DLL，也未对上游预编译 DLL 执行全功能差分。交付结论是：全部历史提交完成本轮静态对抗覆盖，确认的五项偏差已修复并通过适用回归；已知有意差异明确保留，目标环境运行验证待进行。
