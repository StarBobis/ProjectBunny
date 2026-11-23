# MCP 自动化与定点抓取

DX11 DLL 可以通过本机 MCP 触发现有按键动作，并按狩猎 hash、绑定位置或 draw 序号捕获指定 buffer。接口默认关闭。

## 连接

在游戏现有的 `d3dx.ini` 中添加 `[System]` 下的 `mcp_enabled = 1`。需要 Hunting 和原生 F8 时，将 `[Hunting]` 下的 `hunting` 设置为 `1`，然后启动游戏。更新 DLL 后需要重启游戏。

解压仓库根目录的 `projectbunny-dx11-mcp.zip` 后，可用技能附带的桥连接游戏。需要 Windows 和 64 位 Python 3.9 或更新版本。客户端配置使用实际解压路径。

~~~json
{
  "mcpServers": {
    "projectbunny-dx11": {
      "command": "python",
      "args": ["C:/path/to/projectbunny-dx11-mcp/scripts/dx11.py", "stdio"]
    }
  }
}
~~~

`dx11_discover` 返回 `pids` 和 `targets`。每个目标列出 PID、可执行文件名和可用的 `target_id`。在 `stdio` 前加 `--pid` 可设置默认实例，加 `--output-dir` 可指定抓取结果目录。技能入口默认保存到 `%LOCALAPPDATA%/ProjectBunny/mcp-captures`。同一 Windows 账户可以使用 MCP 接口。开发时也可直接运行仓库里的 `scripts/dx11-mcp.py`，将仓库作为桥来源。

### 多进程控制

只有一个 Live 端点时可以自动选择。同时运行多个进程时，用 `dx11_discover` 确认目标，然后在每次 Live 调用传 `pid` 或发现结果中的完整 `target_id`。

~~~json
{"target_id":"14320:01dc123456789abc","action":"Hunting.toggle_hunting"}
~~~

上例的 ID 是占位示例，实际调用请复制发现结果。即使三个进程的 exe 名相同，`target_id` 也会按 PID 和进程创建时间区分。游戏重启后重新发现目标。PID 和 `target_id` 同时传入时应当互相匹配。

同一桥为每个进程保留独立管道。同一个目标的请求按顺序执行，多个目标可并行处理。Live 结果回传 `pid` 和 `target_id`。抓取 ID、性能快照 ID 及后续状态、读取、保存请求使用最初的目标。启动监控仍可用 `dx11_monitor_poll` 的 `pid` 按进程筛选。

## 调用快捷键

用 `dx11_hotkeys` 查询 action ID。Hunting 的前进、后退、标记、切换模式和 VB slot 等功能，以及模组 `[Key...]` 和 `back` 都走原处理函数。动作遵守输入禁用范围。MCP 的 `press` 和 `release` 直接调用处理函数，按住类动作分两次调用，重复触发则调用多次。

| 操作 | `dx11_hotkey` 的 `action` |
| --- | --- |
| F10，重载配置和 ShaderFixes | `f10` |
| 只重载配置 | `Hunting.reload_config` |
| Ctrl+Alt+F10，清除持久设置后重载 | `ctrl+alt+f10`，同时传 `confirm_wipe: true` |
| F8 | `f8`，开启 hold 模式时分别传 `phase: "press"` 与 `phase: "release"` |
| Ctrl+F9 | `ctrl+f9`，循环到需要的性能显示模式 |
| 下一个 IB | `Hunting.next_indexbuffer` |
| 触发 `[KeyExample]` | `KeyExample.key` 或 `KeyExample.back` |

`dx11_profiling` 返回 Ctrl+F9 面板里的文本，包括 Top Command Lists 和 Top Commands。用 `dx11_log_tail` 读取主日志或当前 F8 的 `log.txt`，后者包含原生抓帧的命令执行记录。

直接调用 `dx11_profiling {}` 会读完整面板。需要分块时，先调用 `{"length":8192}` 建立快照。后续传同一个 `snapshot_id`、上次的 `next_offset` 和 `length`，直到 `next_offset == total_bytes`。偏移和长度按 UTF-8 字节计算，块在字符边界结束。发起新快照会替换旧快照。

## 启动前监控和闪退记录

使用支持监控的新 DLL，在启动游戏前调用 `dx11_monitor_start`，例如 `{"target_exe":"ZenlessZoneZero.exe"}`。也可以给桥传入 `--monitor-exe ZenlessZoneZero.exe` 自动提前开始。监控用 64 位 Python，通过 Windows 本机会话的共享内存从 DLL 加载入口接收记录。磁盘日志关闭时也能收到。渲染控制仍由 `mcp_enabled` 管理，预先开启监控只接收诊断数据。

循环调用 `dx11_monitor_poll`，把上次的 `next_seq` 传给 `after_seq`，并可指定 `wait_ms` 等待新事件。启动时会收到 DLL 加载、配置解析和 D3D11 初始化的阶段及日志，进入渲染后会收到 Present 心跳。结果中的 `no_progress` 表示已超过设定时间没有阶段或帧进展，`phase` 会保留最后一个检查点。进程退出后会返回退出状态和可用的退出码，之前接收的记录仍可读取。

`stall_ms` 用于启动阶段。进入 Present 后，桥按一秒心跳计算渲染阈值，最低为 2000 ms。启动返回 `present_stall_ms`，会话状态返回当前适用的 `stall_threshold_ms`。

默认采集启动 INFO 和运行时 WARNING，可以按需设 `level: "info"` 或 `"debug"`。高频日志超过缓冲上限时，结果会报告丢失数。结束时调用 `dx11_monitor_stop`，同一个桥进程仍可查询最近记录。换桥后可以从仍存活的进程身份或 DLL 定期发布的身份记录重新关联会话。若 Windows 在执行 DLL 加载入口前就终止进程，此时没有 DLL 内部记录可采集。

## 定点读取

1. 从 `dx11_hunting` 读取当前选中的 IB/VB hash，或使用 overlay 给出的 hash。
2. 调用 `dx11_capture`，例如 `{"kind":"ib","hash":"1234abcd","frames":30}`，等待指定 hash 在接下来的帧中出现。已知 shader stage、slot、draw 序号或 `resource_id` 时可以继续缩小范围。
3. 用 `dx11_capture_status` 查询捕获 ID。状态为 `ready` 后，用 `dx11_capture_bytes` 分块读取，或用 `dx11_capture_save` 保存整份数据。新任务会替换上一个已结束的结果，需先保存想保留的内容。

Live 控制可由不同交换链的 Present 响应。待处理抓取绑定于接收请求的 context，`frames` 只统计该 context 的后续 Present。其他设备交替呈现时，原抓取继续等待原 context 的绘制。原 context 销毁时，抓取结束为 `device_changed`，可在新 context 重新定位后再抓取。缓冲区在有效 Map 期间返回 `mapped`，即使期间热重载切换了 MCP 开关。Unmap 完成后可重新读取。

`hash_mode` 可指定 `resource` 或 `region`。资源 hash 沿用狩猎系统的资源标识，动态缓冲区每帧的字节仍会变化。区域 hash 沿用 Hunting 的范围算法，捕获结果记录命中时的区域和格式。相同 hash 的实例可用 draw、slot 或 `resource_id` 区分。未登记在 Hunting 的资源使用绑定位置或资源 ID 定位。

IB 抓取传 `{"kind":"ib","indexed_range":true}` 时，只等直接 `DrawIndexed` 或 `DrawIndexedInstanced`，并根据绑定偏移、起始索引和索引数读取实际使用的字节。此模式省略 `offset` 和 `length`。与 F8 的完整 IB 对比时，用结果的 `index_range.offset` 和 `index_range.length` 在 F8 的 `.buf` 里截取同一段。

实时选取支持 IB、VB、CB、buffer SRV/UAV、stream output 和 indirect 参数，也能取得 draw/dispatch 参数及绑定的 shader ID。`source: "mod"` 可选取模组命令列表或 CustomShader 发起的 draw/dispatch。用 `dx11_custom_buffers` 找到命名空间下的声明，再用 `dx11_custom_buffer` 读取已初始化的 mod buffer。`dx11_shaders` 可列出启用 MCP 后保留的 DXBC，包括已实例化的 CustomShader。用 `dx11_shader` 将指定 shader 的字节放入捕获结果。

按名读取自定义 buffer 的结构化错误分别报告 `CUSTOM_RESOURCE_NOT_FOUND`、`CUSTOM_RESOURCE_UNINITIALIZED`、`CUSTOM_RESOURCE_NULL`、`CUSTOM_RESOURCE_NO_INSTANCE`、`CUSTOM_RESOURCE_NOT_BUFFER` 和 `CUSTOM_RESOURCE_WRONG_DEVICE`，分别对应名称不存在、尚未初始化、当前为 null、缺少底层实例、对象类型不符和设备不符。

### 查询原始 PS 贴图的内容哈希

`dx11_texture_dhash` 按 IB 哈希、实际索引数和起始索引锁定一次原始绘制，在覆盖规则执行前读取指定 PS 槽位。原始绘制随后被模组跳过的情况也能匹配。相同 IB 和索引范围用于多个 pass 时，可传 `ps_hash` 匹配该次绘制实际绑定的像素着色器。

~~~json
{
  "ib_hash": "1234abcd",
  "hash_mode": "region",
  "resource_hash": "abcd1234",
  "index_count": 600,
  "first_index": 0,
  "ps_hash": "0123456789abcdef",
  "slots": [16, 17, 18]
}
~~~

普通 IB 哈希使用 `hash_mode: "resource"`。区域哈希还要传入底层 IB 的 `resource_hash`，匹配时按实际索引范围核对区域字节。每次可指定 1 至 8 个互异的 PS 槽位。调用后用 `dx11_capture_status` 轮询同一个 ID。结果中的 `capture.slots` 列出槽号、状态、格式、纹理大小及可用的 16 位十六进制 `dhash`。空槽报告 `unbound`，无法计算的纹理报告 `unavailable`。

支持的 DLL 会在 `dx11_status.features` 中公布 `texture_dhash`。实时查询根据最新已跟踪的上传选择 mip 并计算 dHash。模组匹配继续由 `dhash_allow_rehash` 决定是否更新自己的缓存。缺少当前版本的缓存时，计算可能等待 GPU，建议只传需要核对的槽位。

## 捕获时点和 F8 回退

实时回读使用 staging 复制，在匹配的调用处同步取回字节。默认在调用前读取输入缓冲，在调用后读取 UAV 或 stream output。可用 `phase` 显式选择时点。大缓冲区可能使该帧等待 GPU，优先传 `offset` 和 `length`。延迟上下文录制与重放、其他 swap chain/context，以及完整的 API 调用串使用原生 F8。通过 MCP 触发 F8 后，用 `dx11_frame_analysis_files` 列出产物，再用 `dx11_frame_analysis_read` 读取所需文件。纹理字节保留在原生 F8 路径中。
