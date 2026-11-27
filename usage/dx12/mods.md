# 管理 Mod

将 DX12 Mod 解压到 `Mods`，保留 INI 与资源的相对位置：

```text
Mods/
└─ Example/
   ├─ mod.ini
   ├─ texture.dds
   └─ mesh.buf
```

`d3dx.ini` 需要包含：

```ini
[Include]
include_recursive = Mods
exclude_recursive = DISABLED*
exclude_recursive = desktop.ini
```

## 重载

修改 INI 或替换资源后按 F10，等待重载完成。存在错误或警告时先检查提示与日志，不要连续按键。

F10 重载 Mod 配置，不替换正在运行的 DLL。

## 禁用与冲突

把 Mod 目录改名为 `DISABLED Example`，按 F10。恢复原名即可启用。

模型重叠、贴图错误或闪烁时，只保留一个替换该目标的 Mod，再逐个启用。DX11 Mod 需要适配，复制目录不能完成转换。

## 键盘和手柄共用一个选项

在同一个 `[Key*]` 段中重复写 `key`，键盘和 XInput 手柄就能操作同一个开关或循环。下面的例子用 Z 或 2 号手柄的 A 键向前切换，用 B 键回退。

```ini
[Constants]
global persist $variant = 0

[KeyVariant]
type = cycle
key = Z
key = XB2_A
back = XB2_B
$variant = 0, 1, 2
```

`XB_A` 匹配任意已连接手柄，`XB1_A` 至 `XB4_A` 指定端口。方向键使用 `XB_DPAD_UP` 等名称，摇杆按下使用 `XB_LEFT_THUMB` 或 `XB_RIGHT_THUMB`。肩键、Start、Back、Guide 和左右扳机也可绑定。Guide 需要支持它的 XInput 1.3 或 1.4。

`key = ctrl XB2_LEFT_TRIGGER > 128` 要求同时按住 Ctrl 并让左扳机值超过 128。扳机值范围为 0–255，省略阈值时超过 30 触发。`no_XB2_B` 要求 2 号手柄已连接且 B 松开。使用 `type = hold` 时，拔掉正在触发该组合键的手柄会释放它并恢复保存值。

### 控制循环的起点和边界

上例默认启用 `smart` 和 `wrap`。如果持久变量恢复为 1，按 Z 会切到 2。再按一次会回到 0。添加 `wrap = false` 可让循环停在两端，到达端点后继续朝同一方向按键，端点的命令也只执行一次。

如果需要按固定顺序依次执行命令，设置 `smart = false`。这时首次前进从第一档开始，首次后退从最后一档开始，其他命令对变量的修改也不会改变循环位置。

变量、`condition` 和 `run` 的逗号列表按档位配对。例如 `run = CommandListA,,CommandListB` 表示第一档执行 A，第二档留空，第三档执行 B。某列较短时继续使用它的最后一项。某档的条件为假时，位置仍会移动，该档的赋值和命令会跳过。

### 过滤短按和短暂松手

`delay` 指定按键需要连续按住多少毫秒才会触发，`release_delay` 指定连续松开多少毫秒才会确认释放。两者默认都是 0。下面的配置适合需要长按才开启的缩放。

```ini
[KeyHoldZoom]
key = RBUTTON
type = hold
delay = 200
release_delay = 80
$zoom = 2
```

在 `[Constants]` 中声明 `global $zoom = 1` 后，按住右键满 200 毫秒的第一帧会将它设为 2，提前松开会取消这次按下。松开后 80 毫秒内重新按住，缩放会保持。若同时配置了 `condition`，运行时在按下延时到期时检查条件。一个循环配置中的多个 `key` 和 `back` 各自计时。

### 让变量平滑变化

下面的长按键会在 200 毫秒内将 `$zoom` 从当前值过渡到 2。松开时，用 300 毫秒回到按下前的目标值。时间以毫秒计，曲线可选 `linear`（默认）或 `cosine`。

```ini
[Constants]
global $zoom = 1

[KeyZoom]
key = RBUTTON
type = hold
$zoom = 2
transition = 200
transition_type = cosine
release_transition = 300
release_transition_type = cosine
```

`run` 的普通命令在按下时执行，可以读取过渡开始前的值。对于 `activate` 和 `cycle`，`post` 命令在本帧变量更新后执行一次。过渡时间为零时，它能读到目标值。过渡仍在进行时，它读到第一帧的值。对于 `hold` 和 `toggle`，`post` 在松开或关闭时执行，读取恢复前的值。

## 保存的选项

`global persist` 变量默认保存在 `d3dx_user.ini`。可在主配置指定路径：

```ini
[Include]
user_config = d3dx_user.ini
```

暂时禁用 Mod 后，它的设置会成为未知条目。随包配置将 `clear_unknown_settings` 设为 0，会长期保留这些设置。

如果旧配置把这个选项写成了 `$变量`，请改成明确的 `0` 或 `1`。读取清理开关时 Mod 变量还不可用，回退值为 1。需要长期保留未知条目时应填写 `0`。主配置使用默认命名空间时，重复填写该项以首个非空值为准。带命名空间的包含文件可以覆盖它。

如果把它设为 1，或使用省略该键的自定义配置，首次按 F10 会提示保留了多少项。对同一用户设置文件再次按 F10 时，如果未知名称和数值都没有变化，DX12 会清理这些条目。启动和自动保存均不计入次数。如果之前在设置为 0 时已经按过 F10，开启清理后的第一次 F10 就可能清理同一批未知项。清理前重新启用 Mod，可以恢复原值。

要在自定义配置中长期保留停用项，可以使用与随包配置相同的设置。

```ini
[System]
clear_unknown_settings = 0
```

若 F10 提示保存失败，当前选项仍在内存中。解除文件占用或修复写入权限后，再按 F10 即可重试。

随包配置中，**Ctrl + Alt + F10** 会删除当前用户设置文件，丢弃尚未保存的选项，并重新加载 Mod 默认值。要保留一份副本，请在按键前备份当前 `user_config`。如果刚把 `[Include] user_config` 改到另一个文件，重置会删除旧文件并读取新文件中的选项。

可以在主配置中改绑两个按键。它们在 Hunting 关闭时也有效。

```ini
[Hunting]
reload_config = no_modifiers VK_F10
wipe_user_config = ctrl alt no_shift VK_F10
```

旧 DX12 配置省略这两个键时，普通 F10 继续可用，清空设置需要显式绑定。将任一项留空可禁用它。清空快捷键至少要包含一个需要按下的键。若配置引用不完整或设置文件被占用，重置会中止。当前选项仍可使用，修复后可再次按键。
