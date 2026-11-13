# PNG、时间与 callback 集成只读复核

本轮只读检查真实生产和对应回归源码，并重读上游 `be6bd4eae63273e49bbff62a5e90e70e73597382`、`311d285339309d1366a833c8aea40b3ee658679c`、`4cd308a59810d983c3a1298464d49c3e96cbd8bf` 的 diff，结合先前逐提交审查。未改生产或测试，未启动构建；本文件不代表测试执行结果。

## 结论

未发现本轮 PNG/时间/callback 接线中的确定生产回退。发现一处小的回归覆盖缺口：`src/DirectX11/tests/png_color_space_regression.cpp` 的 partial-chunk 循环使用 `length < 12`，而生成的 sRGB 块共 13 字节，遗漏长度 12（仅最后一个 CRC 字节缺失）。建议覆盖到 `length < complete.size()`；当前生产对该输入会返回 false，无需因此改生产。

## PNG 边界与颜色字段

- `PngColorSpace.cpp` 使用真实文件大小和 64 位绝对定位。初始 position=8 不大于 file_size；循环要求剩余至少 12 字节；length 在加法前受 `file_size-position-12` 和 PNG 31 位限制。由此下一 position 不大于 file_size，且不超过 signed `_ftelli64` 的结果，绝对 seek 转换不会溢出。签名、块头和 intent 都要求完整读取，FILE 由 RAII 关闭。
- sRGB 正匹配要求长度 1、intent 0..3，且整个数据与四字节 CRC 空间存在。这里**不计算 CRC checksum，也不验证完整 PNG 结构**，头文件已明确其为有界元数据扫描器；上游 scanner 同样不验证 CRC。这不能记录成完整 PNG 解码/CRC 验证通过。
- 扫描在 IDAT 或 IEND 停止；不从 iCCP 推断 sRGB。显式 `srgb`/`linear` 提前决定 WIC flags，自动模式仅在扫描匹配时 FORCE_SRGB，其余保留本地 WIC_DEFAULT。DDS 仅 `srgb` 设置 forceSRGB，`linear` 不承诺去除 DDS 自带 sRGB 格式，符合上游当前行为。
- `IniHandler.cpp` 真实读取 `color_space`、转小写、接受空/srgb/linear，非法值清空并警告；日志的 `%ls` 实参使用 `.c_str()`。`CopyMetadataFrom` 复制 `override_color_space`，池元素初始化和 copy_desc 均走该 metadata 链，因此未丢失 d1deb9d2 的合并解决。
- LoadFromFile 保留本地 ComScope；本次颜色接线未替换共享 COM/WIC factory 生命周期或已有池实现。

## 时间与空间重用

- `FrameTiming.cpp` 使用 DX11 自有 C++17 steady_clock 微秒时钟；Globals 的 previous tick 从 launch tick 初始化，首帧 FRAME_TIME 不包含系统 uptime。UpdateFrameTiming 在 pre-present 命令执行前统一更新 TIME、FRAME_TIME 与 FPS。
- FPS 初次调用仅建基线，后续忽略零 delta 与长暂停，并在忽略前更新 last_tick；有效 delta 使用上游 EMA 公式。倒退时钟按零 delta 处理。FPS 保持 runtime 参数，FRAME_TIME 的显式静态快照逻辑保留。
- DX11 真实 tick 读写已为 uint64_t；random seed 只显式提取低 32 位，独立随机状态未回退为无同步的 G 状态读取。
- GetElementIndex、UsesMissingTemplate、GetLastUpdateFrame 均使用 GetSpatialReuseRadius，保持一致的 120FPS 缩放、非法时间返回零与 12/8/12 环形网格最大距离 2048 封顶。先 double 验证，再对有界 float 运算 ceil，避免无界 float 到 unsigned 转换。
- 空间查询有重绑定语义：60FPS 的邻居命中可能把原 key100 改为 query103。因此不同 FPS 阈值比较必须独立初始化池；主代理正在使用每轮 ResetPool/写回原 key 的修正，不应把已重绑定后的 exact hit 当成阈值失效。

## callback namespace fixture

- 真实 ParseCopyCommandListCommand 在命令入表和 runtime 标记前检查目标原始 pre/post body；外国 namespace 对任一非空阶段都拒绝。nullptr namespace 安全按全局处理；null reset 保持允许。全局 section 无拥有 namespace 时仍允许跨 namespace，符合上游该检查的 found 条件。
- `callback_namespace_regression.cpp` 通过真实 Include/LoadConfigFile 建立 namespace 元数据，覆盖 pre-only/post-only、拒绝后不变、相同 namespace 接受、空 callback、null reset 和全局 section。接受分支执行真实 CopyCommandListCommand 来验证双阶段代理更新。
- fixture 的局部命令对象在配置 owner 析构清理 registry 前销毁，避免旧目标裸指针跨清理继续使用。这组测试验证 CPU callback 接线，不把它当作 shader/GPU 命令执行验证。

主代理后续已将PNG短chunk循环扩至完整13-byte sRGB块的全部截断长度（含12），并运行真实decoder/COM回归通过；此补充不改变生产扫描器。
