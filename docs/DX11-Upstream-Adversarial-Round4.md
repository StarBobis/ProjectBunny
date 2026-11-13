# 第四轮及后续修复—复审循环

基于本地 `fd3a984`、上游 `eae473b3c3b833380e70ba9dc61a45c9937ef924`。开始时上游检查仍为 336 项，无新增。本轮改变切面，覆盖配置到资源使用、别名状态传播、GPU 可见性及实际 DLL 接线；同时执行完整既有回归，不将场景审查表述为又逐行重读全部 336 个补丁。

用户随后要求：每批修复并通过回归后，再做独立对抗审查；确认新问题则继续循环，只有完整一轮未发现新确认问题且验证通过后才能收尾。以下保留每次复审的发现和验证边界。

## 第四轮：确认并修复三类问题

| 场景 | 修复前真实观察 | 处理 |
| --- | --- | --- |
| 动态 pool 赋值目标 | ref 采样 2 次；copy/copy_desc 采样 3 次；动态源和目标共 3 次。缓存归属、描述符和发布可能指向不同元素。 | THIS 解引用及 source preflight 后，本次调用冻结 CUSTOM destination；缓存、重建、发布共用同一目标。无效动态目标安全返回，空源 unless_null 不选择目标。 |
| buffer RTV 绘制后 hash | 双 VB/RTV buffer 预热 CRC `03c8eb67`，真实 raster Draw 后原生 staging CRC `9c53a0fa`，缓存却仍返回旧值。 | 图形写集增加 RTV 枚举，通过现有仅 buffer 的失效 helper 更新；返回的 COM 引用逐一释放，保留 UAV/SO 和 deferred 提交失效路径。缓存开启时增加一次 OM 查询，未测性能成本。 |
| 旧配置声明污染早期选项 | 完全相同配置连续 Load，allow_buffer_resize 首次 true、第二次 false，原因是读到上代 globals。 | 早期选项隐藏旧 globals、pool、resource 注册表，原对象保持存活；按声明/资源解析阶段恢复并沿用既有清理节点，防止旧代解析副作用。当前 Constants 尚未声明时不提供前向变量支持。 |

回归分别为 `round4_alias_regression.cpp`、`round4_gpu_regression.cpp`、`round4_config_regression.cpp`。赋值回归覆盖两元素实际资源及 original metadata、ref/copy/copy_desc/null、无效索引、unless_null 和重复调用。配置正例覆盖 Constants 后 Pool size/default 与 Resource width，并验证重载新值生效。

赋值测试曾错误地假定 SeedExpressionRandom 会清零 sample counter；已依据其公开的单调计数语义补偿 seed 后重放真实随机输入。此前 null aligned0 单独不算生产缺陷；2/3 次采样的 red 观察不依赖该 oracle，仍有效。扩展测试另实际触发并修复 SetResource 无效 CUSTOM 目标的空指针解引用。

第四轮构建与全量 CTest 4/4 通过（20.48 秒），独立 pool/property 回归、DirectX12 兼容构建通过，随后启动第五轮，未据此直接收尾。

## 实际 DLL 冒烟测试

新增独立 `dx11_proxy_smoke` CTest，使用刚生成的 DLL，在 build 下创建独占配置和副本，在隐藏子进程中实际 LoadLibrary 并执行 DllMain、解析创建导出与 ordinal、读取三份嵌入字体，创建 WARP device/context，检查返回 vtable 属于该代理模块，完成真实 CreateBuffer/CopyResource/Map 全字节比对。子进程有超时，退出后只清理已核实的自有目录；不部署到游戏目录。

初始 11.1 请求被默认 ForceDX11 拒绝，属于已有接口策略，测试改用明确 11.0；未为了测试修改产品默认。后续补测原生默认 feature list、只 device、只 context 及无输出验证调用。

构建接线另核对了上游 24 个 DX11 本目录编译单元：ByteCodeReader 对应本地 ShaderBindings，Input 对应 input，其余均接入 CMake；导出 def 与上游字节相同。此项是接线核查，不是二进制等价证明。

## 第五轮：再次发现问题并继续修复

- **context-only 创建缺失 owning device**：实际 DLL 创建成功但 context.GetDevice 返回 null。包装前从原生 context 恢复其 device，以临时输出引用走已有包装和相互关联流程，随后平衡该引用；不关闭该路径的代理功能。实际 DLL 扩展冒烟测试验证可选输出组合。
- **创建失败误发布旧缓存**：原 null 目标在 copy_desc 重建失败后变为非 null，stride 从 2 变 4、original_dhash 从 67890 变 12345，resource 仍为旧 64B。修复“缓存命中当前资源”和“创建失败”共用 null 返回的歧义，显式返回成功状态；成功后再修改 source_stride/view 等状态。失败不插入永久负缓存，允许同描述重试。
- **Constants 期间旧 STATIC 池污染新代**：真实二次 Load 后旧 lazy slot 在新 globals 留下 ghost=1，新池首次同名注册返回 null。恢复节点进一步拆分：Constants 前只恢复 globals；ResourceSections 前才恢复旧 pool/resources，防止 Constants 初始化表达式实例化旧池。修复后 ghost=0，新 slot 非空。

创建失败新增 `round5_assignment_regression.cpp`：对合法 128B 描述的真实 native CreateBuffer 注入一次 E_OUTOFMEMORY，分别覆盖 copy/copy_desc，验证旧 null/resource/view/数值和 original metadata/CPU snapshot/缓存条目数保持，卸载注入后同描述重试成功。快照通过实际 GetRegionHash GPU readback 预热；没有假造生产依赖。

STATIC 池回归为 `round5_config_regression.cpp`。第五轮 GPU 源链审查未确认新问题；“native copy 写回 tracked allocation”候选已因资源池实际切换至原生私有 allocation 而排除，没有把不成立路径包装为发现。

## 证据边界

子代理独占源链材料及 red/green 日志位于 ignored `build/upstream/adversarial-round4/` 和 `adversarial-round5/`。持久结论保存在本文与同步账本。

配置隔离只对相应注册表提供作用域恢复，不意味着整个 LoadConfig 的所有设置和枚举 map 都能事务回滚；早期 C++ 异常恢复目前为源码审查，未做完整 OOM/线程交错穷举。未运行目标游戏、未向目标目录部署 DLL、未运行上游预编译 DLL 全功能差分。原有兼容差异及 WARP GenerateMips 非零 first-array-slice 限制继续保留。

第五轮最终检查点：DirectX11/完整生产回归/实际 DLL 冒烟目标构建通过，完整 CTest 4/4 通过（13.77 秒），38 页及 87 个本地链接的文档检查、diff whitespace 检查通过。第六轮独立复审已启动，完成前保留循环待办。

## 第六轮：COM 及 extended interface 复审

配置和跨 pipeline 两组复审未确认新问题。创建/所有权组通过实际 DLL 再次复现两项遗漏，因此继续修复而未收尾：

- `GetImmediateContext` 第一次延迟创建时，native GetImmediateContext 和成功 QueryInterface 分别增加引用，但未释放旧接口。加载代理前完成的独立原生对照 normal/late AddRef 计数为 2/2，代理为 2/3。成功 QI 后补 Release，保持失败 QI 的原引用转移路径。
- `GetImmediateContext1` 延迟创建缺少 Bind/Init/Hook 处理。配置 allow_platform_update=1、Constants x=1 后，PS t120 正常创建/普通延迟/Context1 延迟绑定实测为 1/1/0。补齐与普通路径相同的初始化，并按 hook 模式选择返回原生或包装 context。

上述断言已合并到原 `proxy_smoke.cpp`，没有再维护第二份重复宿主。测试分别运行普通旁加载、先加载原生运行库再加载代理两种隔离子进程。原生 refcount control 在加载代理前完成，避免 LoadLibrary 重定向使“原生对照”也调用代理。fixture 明确分配 IniParams，避免 0/0/0 空资源比较掩盖绑定遗漏。

修复后完整 CTest 4/4 通过（15.85 秒），两种实际 DLL 加载模式均通过。第七轮独立复审已启动，尚未依据本轮通过结果收尾。
