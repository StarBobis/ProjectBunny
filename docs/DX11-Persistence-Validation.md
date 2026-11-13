# DX11 持久化与配置重载验证

`src/DirectX11/tests/persistence_regression.cpp` 导出 `test_dx11_persistence_regression()`，由完整链接生产源码的 `dx11_upstream_regression` 最后调用。测试使用真实系统 D3D11 WARP、HackerDevice、HackerContext、INI 加载器、指令解析/执行器以及保存和重载入口，不替换生产实现。

测试 exe 必须位于 `build` 目录内。测试通过 CREATE_NEW 独占创建其旁边的 `d3dx.ini`；若已经存在则失败，避免覆盖配置。user_config 使用同目录 PID 唯一文件名。RAII 在成功或断言异常后删除自建配置并恢复原 user_config 路径。不会读取项目根配置或游戏配置，也不部署 DLL。全局变量清理前先销毁持有其裸指针的命令和资源池。

覆盖内容：

- 真实 INI pool 选项：数量、RING/FIFO、懒分配、默认值、持久化标志、混合类型、超时和过期重置。
- 普通全局变量和已分配 RING 槽登记、命令写入标脏、写盘内容、未分配槽不写盘、代理只保存最终所有者、混合资源访问保留变量、非 RING 禁止持久化。
- `LoadConfigFile` / `InitIniParams` / `SavePersistentSettings` / 连续两次 `ReloadConfig` 的实际读取重建路径；登记数量稳定、Constants 恢复保存值且不再次标脏。
- 缺失父目录导致保存打开失败，dirty 必须保留；恢复有效路径后无额外赋值即可重试成功。此测试发现并促成修复了保存前过早清 dirty 的缺陷。
- 整池清空后配置重载先保存默认值，再读回默认值。
- 同一 INI 的 pool_size 算术表达式和引用已声明全局变量的默认值；kv_map 数值读取的 found 与缺失 fallback。

最终集成记录：主代理在 x64 MSVC/CMake/C++17 环境完成正式 47/47 构建，CTest 1/1 通过（测试 0.29 秒，总计 0.46 秒）；包括本文件全部持久化、INI 表达式/found 断言及同轮布局回归。复现命令：`cmake --build build/dx11-upstream-en --target DirectX11 dx11_upstream_regression -j 6`，随后 `ctest --test-dir build/dx11-upstream-en --output-on-failure --timeout 30`。此前首次主体集成亦通过，最终结果以上述完整复跑为准。

尚未验证：目标游戏中热键触发、真实 GPU/注入生命周期；磁盘空间耗尽或 fclose 中途失败（现有失败注入覆盖 CreateFile 打开失败）。文件格式精度检查包含可精确表达的小数，尚未对全部 IEEE 浮点值作往返枚举。配置持久化为原有覆盖写入行为，没有新增崩溃原子性保证。

新增锁定变量集成：真实 Key/Preset/Cycle 排除 LOCKED 目标且未锁控制变量仍可更新。persist+locked 的声明值保持权威，保存器省略锁定赋值；手工加入遗留 `$persistence_locked = 99` 后，重载保持声明值7并请求一次清理，下一轮重载完成清理后保持 clean。原池值、registry数量和dirty断言保留。最新统一构建及CTest 1/1（0.30秒，总计0.44秒）通过，含分词/数学函数并行批的完整回归。

过期选项新批集成：真实配置覆盖旧名only、新名only、新旧冲突以新名优先、新名存在但无效使用其默认值。保留原持久池/锁定/重载/失败重试断言；最终完整CTest 1/1（0.34秒）通过。

Callback重载覆盖：fixture增加非persist结果变量与两个真实CommandList；初载解析并执行ref后结果22，清空局部赋值命令避免遗留裸指针；写回配置删除source section后真实ReloadConfig确认source不存在、destination不再proxy并执行自身结果11。原pool值、dirty0、registry4、locked清理和保存重试断言均保留。最新完整CTest 1/1（0.36秒）通过。

Reader/续期批集成：真实Preset/Key cycle配置中额外尾词目标被忽略，原合法未锁控制变量仍为6、locked仍为7、target数量保持1；pool_expiration_refresh_on_read默认false、显式true与重载重建策略均验证。保留callback重载删除、所有持久值/dirty/registry/失败重试断言。最新完整CTest 1/1（0.39秒）通过。

## 第十七批：未知普通变量保留

新增persist_preservation_regression.cpp接完整生产CTest：初载frame0/未初始化、未知合法literal/无效值拒绝、大小写及空格/UTF namespace、System.auto_clear_persist_vars切换、重复autosave不推进、两次explicit reload的after-write清理、mod停启、owner路径隔离与等价路径、打开失败locale/dirty、真实句柄导致的reload和wipe失败及重试。最终CTest1/1（0.56秒，总0.72秒）通过。fixture普通文本读取规范CRLF，未降低内容断言。未注入中途磁盘写或关闭失败；cold-start未知池槽沿用上游c05早return未捕获的边界，不能宣称此回归已覆盖。
