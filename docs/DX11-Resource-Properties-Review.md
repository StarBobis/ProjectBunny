# Resource 属性组集成复核

只读检查真实 `src/DirectX11/CommandList.cpp/.h`、`tests/texture_properties_regression.cpp` 和移植前副本；重读上游 `862d1b6f1015c23d40fcaca4fd97620ce9085ce0`、`777141aaf378c2c371c611adc4821508899645fc` diff。未改生产或测试，未构建。本文件记录代码审查证据，不代替执行结果。

## 结论

未发现该组接线中的确定生产回退。普通 ref 的 `buf_size=0` 是原有未知长度发布约定，不能为满足测试改成物理长度；size getter 仍可从真实 buffer desc 返回 128。当前 fixture 已明确检查这两层语义，并增加真实 copy/readback 检查 oversized metadata 的物理边界。

## 已核对的语义

- ResourceCopyTargetEvaluationMode 使用 uint32_t；format/width/height 位为 0x20000/0x40000/0x80000，RESOURCE_MASK 为 0xe01ff，包含原 Region0x100。pool 至0x2000、variable0x4000、layout0x8000/0x10000 均不重叠。TIME/FRAME_TIME/FPS 属于独立 ParamOverrideType，不共享该位空间。
- ViewInfo 是文件作用域结构。SRV/RTV/UAV 仅在 ViewDimension 真为 BUFFER/BUFFEREX 时读取对应 union 的 FirstElement/NumElements；texture MipSlice、MostDetailedMip、array slice 不再误作 buffer offset。custom generic view 通过 QI 确定真实接口，ComPtr 释放 QI 引用。
- FillInMissingInfo 保留显式 stride/offset/format 优先，buffer size 先按物理 ByteWidth 限制，结构 stride/native format fallback 保留。buffer view offset 用 uint64_t 乘法再物理 clamp。原有先填物理 size、普通 ref 发布未知 size 的行为未被改成 view 长度语义。
- format getter 按 custom override、cached metadata、GetResource 已知格式、typed view、native texture 依次取值；这是 override/cached 优先规则，不应因为 view 更具体而覆盖显式 metadata。unknown view 时仍回退 native texture，符合 862d1b6f 最终语义。
- width/height 查询完整 native texture desc，不按 view mip 或 slice 缩小；1D height=1，buffer=-4，缺资源=-2，未知格式=-1；显式 custom width/height（包括零）先返回。新 getter 持有资源/view 的 ComPtr，early return 同样释放引用。
- 最小 GetResource 输出参数调用始终保留有效 view 地址；CB 转 byte 的 offset/size dereference 均先判 nullptr；IB stride 查询仍保留 format 指针用于计算元素大小。GetResource 内部并非普遍允许 view=nullptr，本组没有传入该非法组合。
- Region copy 保存 selected_offset/selected_size，FillInMissingInfo 后恢复二者；ref 时单独发布 selected_size，普通 ref 则继续 buf_dst_size=0。CB FirstConstant/NumConstants 的16字节模型未被新属性 helper 改变。
- DXGI literal 检查完整12字符前缀并由 ParseFormatString 验证完整名称，修正上游仅比较4字符的问题；VALUE 可静态求值，非法 enum 不补成零。INI 大写拼写通过既有规范化路径处理。

## 测试源码证据与边界

texture_properties_regression 通过真实 WARP 和生产 expression/copy parser 覆盖 typed SRV/RTV/UAV/DSV、无 view native fallback、custom cached/override 优先、1D/2D/3D 整 extent、重复调用 COM 引用平衡、typed buffer offset、IB stride、CB region window、missing/error值和 DXGI literal 正反例。新增 oversized metadata copy 分支实际读取返回 buffer 内容，而非仅检查源码或发布字段。

本轮没有执行上述测试，也没有修改正在运行的共享 build；最终通过状态由主代理的集成运行给出。
