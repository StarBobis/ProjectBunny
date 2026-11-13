# Input toggle / HOLD 只读复核

本轮完整阅读上游 `bb2cc9e6526e2243a2b8e32cbf8ddf3a4f1718a7`、`ddd144d7bad35ae6a252adf564ff495a92c55eaa`、`f59f8e5d6a6f66884fbd82131ab971edd9028d65` diff，并检查真实 input.cpp/.h、Override.cpp、IniHandler.cpp 与 input_toggle_hold_regression.cpp。仅新增本文档，未改生产/测试、未构建。

## 结论

未发现这组适配中的确定生产错误。分类 gate 在 dispatch 前执行，避免照搬上游“第一项必定是 toggle”的漏洞；HOLD 保留 f59 最终的重复 Down 行为，没有停留在 ddd 的全局 active 早退中间态。

## 生产语义核对

- RegisterIniKeyBinding 对缺失配置返回 false，非法键解析失败不插入 action。只有显式 enabled_when_disabled 标记的 action 可在禁用时执行；toggle 注册传 true，而普通现有调用使用默认 false。因此 toggle 缺失、非法或排序不在第一项都不会放行普通首 action。
- DispatchInputEvents 仍首先 CheckForegroundWindow，随后按原策略轮询 controller，再进入 DispatchRegisteredInputActions。后者在 button CheckState / action Dispatch 前检查 gate。没有改变游戏原生键盘输入，仅控制本组件绑定。
- disable_input_initialized 初始 false；只第一次 LoadConfigFile 读取 disable_input。重载重新注册 toggle，但不覆盖已经切换的运行时状态。该标记独立于原 gConfigInitialized 的早期 include 加载流程。
- 禁用期间普通 InputAction、RepeatingInputAction、DelayedInputAction 都不采样、不推进其状态。已经 active 的 HOLD 不会在禁用瞬间自动恢复；其物理 Up 在重新启用后的采样时被观察并恢复。这是跳过 dispatch 的既定行为，不能声称禁用期间已释放所有 hold。
- KeyOverride::DownEvent 对 TOGGLE 保留 Toggle；HOLD 且 !active 才 Activate(true)，保存原值并设置 active；重复 HOLD Down 走 Activate(false)，仍运行 activation pre 并排入 post，但不覆盖原 save。Deactivate 的 !active guard 使任一 Up 恢复一次，后续 Up 不再执行 post/restore。
- Override::Activate 没有 ddd 的全局 active 早退，Preset/Cycle 原有调用不受该中间态限制。变量解析、transition schedule 和 transition update 仍拒绝 LOCKED；没有因 HOLD 改动开放写入旁路。
- InputAction / InputButton 的 virtual 析构保证 registry 删除实际 derived action/button；InputAction 负责拥有并删除 button，InputButtonList 释放其子按钮。构造到注册之间使用 unique_ptr，成功后才交给 registry，非法解析不会遗留已构造 backend。

## 测试源码及明确边界

测试将硬件采样替换为 MemoryInputButton，其余使用真实 registry、action 状态机、ToggleInput、LoadConfigFile/ReloadConfig、WARP wrapper、OverrideSave 和 transition 执行。覆盖普通首 action 的禁用 gate、非首 toggle、缺失/非法配置、重载保持状态、恢复后 deferred Up、polymorphic 析构、HOLD Down/Down/Up/Up 的 save/pre/post，以及 TOGGLE/Preset/Cycle/LOCKED。

当前有两处可明确补强的覆盖边界，均不是已证明的生产错误：

- Repeat 测试用 repeat=1001 得到 0ms 间隔，Delayed 测试用 delay=0/0；它们证明 gate 先于状态机执行及启用后恢复，不能证明正延迟、真实 repeat 时间间隔或跨禁用时间的 timeout 行为。
- 禁用时 deferred release 使用 InputCallbacks 计数；真实 HOLD 的重复 Down/Up 另行测试。目前没有把“真实 HOLD active → disable → 物理释放 → re-enable → 恢复一次”串成单个集成 case，不能将两组测试直接表述为已执行该组合。

没有执行 GetAsyncKeyState / XInput 的实际按键采样、前台窗口切换或游戏交互测试。本轮只读审查不增加这些运行证据；共享构建与测试结果由主代理记录。

集成后已补真实HOLD active→disable→backend释放→不poll/不恢复→reenable→Up只恢复一次的组合断言，最终完整CTest1/1通过1.31秒。正delay/repeat时序及真实OS前台/键盘边界仍按原审查未测，不将0-delay gate测试扩大为时间算法证明。
