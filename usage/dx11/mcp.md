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

### 回溯蒙皮缓冲区的 CPU 上传

支持的 DLL 在 `dx11_status.features` 中公布 `resource_provenance`。用 `dx11_resource_trace` 为当前设备开启短时间观测，再用同一目标和 `trace_id` 读取结果。选择原始 IB 资源哈希时，可附带 `first_index` 和 `index_count` 区分共用 IB 的部件。也可以使用 `uav_hash`、`cs_hash` 或同一进程的 `resource_id`。后面三项同时提供时需要同时命中。角色、场景和部件均由实际绑定决定。

~~~json
{"ib_hash":"4e993b0e","first_index":0,"index_count":37203,"frames":2}
~~~

1. 用 `dx11_resource_trace_status` 等待 `ready`。遇到 `no_match` 时查看 `miss_reason`、`indexed_draws`、`dispatches`、`dropped` 和 `command_list_gaps`。也可用 `dx11_resource_trace_cancel` 提前结束并读取已经收集的证据。
2. 用 `dx11_resource_trace_list` 列出命中项，用 `next_offset` 分页。同哈希资源通过底层 `resource_id` 区分。
3. 将命中项的 `submission` 传给 `dx11_resource_trace_chain`。IB 锚点从 DrawIndexed 的 VB0 往前追经过的 Copy、CS 输出 UAV 和 CS-t0/t1/t2。CS 或 UAV 锚点列出输入来源和后续能关联的 VB0 消费者。
4. 用 `dx11_resource_trace_event` 读取链中某个 `submission` 的完整字段。每个事件包含资源实例、描述符、区间、录制和提交的帧与 context，以及 CPU 源字节的 CRC32C 和长度。调用栈能解析时，还会给出首个 DLL 外的调用方模块和偏移。

`record` 与 `record_qpc` 指向录制位置，`submission` 按所观察的即时 context API 提交排列。延迟列表的 `command_list_id` 在重复执行时相同，`execute_id` 随每次执行变化，`list_order` 保留列表内相对次序。`submit_qpc` 标记提交时间。延迟列表在执行前仅保留参数和取证数据。延迟绑定随 setter 记录，由 `binding_source: "deferred_setters"` 标识。列表在采集开始前已有命令、绕开包装层，或追踪丢失时会留下缺口。序号用于比较记录到的 CPU 提交，GPU 完成时间需另行测量。

CreateBuffer 初始数据的哈希在启用 MCP 后随资源保存，后续绑定可以读到它。追到窗口以前的初始数据时，状态为 `initial_data_before_observation_window`。UpdateSubresource/1 和区域 Copy 返回观察到的区间。Unmap 的 `cpu_source_bytes` 是结束映射前的快照长度，`map_record`、`map_frame`、`map_thread` 和 `map_qpc` 指向对应 Map。D3D11 不提供 Map 的实际脏字节范围，`destination_range` 此时为空。CS UAV 和 VB0 分别给出 view 窗口和可用缓冲区窗口，具体读写字节由 shader 和索引决定。直接索引绘制的 IB 区间按绑定偏移和索引格式计算。

默认仅保存元数据和哈希。需要原始字节时，在首次查询得到目标输入的 `resource_id` 后重新布置采集，同时传入 `sample_resource_id` 和 `sample_bytes`。例如下面的请求保存实例 42 每次更新的前 12 KiB。

~~~json
{"resource_id":42,"sample_resource_id":42,"sample_bytes":12288,"frames":2}
~~~

用 `dx11_resource_trace_bytes` 读取某条更新的 Base64 数据。匹配实例才保存字节。每次观测最多保留 8192 条提交和 4 MiB 样本。CPU 哈希预算为每个事件 16 MiB、每次观测 128 MiB，超限原因分别在 `cpu_source_state` 和 `sample_state` 中报告。`dx11_resource_trace_chain` 的 `truncated` 标记受事件或链路上限影响的结果。新观测会替换已结束的结果。

APMI 可对角色详情、大世界、编组逐一复用这一路径。上述选择器取自编组页的 F8 样例。该样例的第 57 行上传到随后在第 72 行绑定为 CS-t2 的实例。第 74 行的 UAV 实例在第 2863 行用作 VB0，第 2868 行绑定 IB `4e993b0e`，第 2891 行绘制 37203 个索引。选择器使用实际 `IASetIndexBuffer` 资源哈希。

已有 F8 样例中，三个场景各有 6 条 CS-t2 绑定可按同一资源地址定位到更早的 CPU 上传。对同帧唯一且非零的矩阵比较得到的 177 槽对应和头发槽锚点适合做验收。需要补抓时可结合 Frame Analysis 的 `dump_on_update`，用已知 IB 范围、CS、槽位和底层实例对齐两份证据。家园或新角色首次观测时可先用 CS 或输出 UAV 发现各自的 IB 和输入实例。

缓冲区视图被清除或丢弃后，追踪链会按视图及矩形覆盖的字节范围更新来源。可以用 `dx11_resource_trace_chain` 查看同一缓冲区中哪些区间保留了先前写入，哪些区间已标记为 `discarded`。

#### 深挖选定上传的调用栈

`dx11_status.features` 包含 `resource_caller_frames` 时，可在 `dx11_resource_trace` 传入 `caller_frames`。先从目标链的 CPU 上传事件取得目的 `resource_id`，再将它作为 `caller_resource_id` 重新观测。以资源 ID 为主选择器时，`caller_resource_id` 默认等于 `resource_id`。资源实例 ID 只在同一目标进程内有效。下面的例子仅适用于先前已识别实例 67 的目标。

~~~json
{"ib_hash":"41e0edb1","first_index":0,"index_count":30666,"frames":2,"caller_resource_id":67,"caller_frames":24}
~~~

`dx11_resource_trace_event` 返回按录制线程调用顺序排列的 `caller_frames`。每帧包含 `stack_index`、模块和返回地址 RVA，可能包括 `UnityPlayer.dll` 和 `GameAssembly.dll`。原有的 `caller_module`、`caller_offset` 继续提供首个可解析的外部帧。

默认 `caller_frames=0`。开启后每条匹配上传最多保留 32 帧，原生展开最多尝试 48 帧，每次观测最多尝试 128 条匹配上传。延迟列表重放复用录制时的栈。根据 `caller_frames_state` 和 `caller_unwind` 检查结果。模块无法解析的帧保留顺序，模块和 RVA 为空。`partial` 标记解析缺口或达到帧上限，`event_limit` 表示超出上传数上限，`unwind_empty` 和 `no_external_frames` 表示当前事件缺栈。`captured` 表示已保存展开返回的外部帧。原生栈展开、内联优化和跨线程提交仍可能让上层装配调用缺席。调用栈和 CPU 字节采样可以独立启用。

在 APMI 六部件和编组实机样本中，脸眼 CS `3b4f1b1749cd3030` 与身体头发 CS `09d91b2eee7e232f` 的相关上传都返回 `UnityPlayer.dll+0x93DD2A`。本机 UnityPlayer 2022.3.62.9394352 的 `0x93DD24` 是 `call qword ptr [rax + 0x180]`，对应 `ID3D11DeviceContext::UpdateSubresource`。x64 unwind 关联的函数入口为 `0x93DB50`。这一共同帧能定位上传包装层。角色详情、大世界和编组是否经过同一个外观装配方法，需要比较这层以上的证据。

### 获取 Unity 侧骨骼身份

D3D11 的 shader、view、缓冲字节和调用方模块只覆盖提交边界。统一骨骼 ID、Transform 父子关系、bind pose 以及矩阵压缩前的槽映射需要从 Unity 项目或引擎构建取证。`dx11_resource_trace_status.unity_bone_identity` 会标记这个缺口。

有 Unity 2021.3 或更新版本的项目时，将仓库中的 `scripts/unity/ProjectBunnySkinningEvidence.cs` 加入其脚本目录。在编辑器用 **Tools → ProjectBunny → Export loaded skinning renderers**，或在开发构建的主线程调用 `ExportLoadedRenderers(path)`。它自动枚举所有已加载场景中的 `SkinnedMeshRenderer`，包括 inactive 渲染器和各级 LOD。导出的 `meshLocalSlots` 提供 `bones[i]` 指向的 Transform 和 `bindposes[i]`。同一快照的 `hierarchy` 可按 Transform instance ID 追父节点，`rigRootInstanceId` 标记该渲染器的骨架锚点。编辑器中有来源时，Prefab 对象和 Mesh 会记录资产 GUID 与本地 file ID。运行时实例 ID 仅用于同进程对应，层级路径和来源资产可用于跨快照提出匹配候选。

若项目拥有 palette 打包逻辑，在重排或压缩完成且上传之前保留最终槽位对应的 Transform 与原始上传字节。在调用上传的前后各取一次 `Stopwatch.GetTimestamp()`，之后将这两个时间和原数据传给 `AppendPaletteUpload`。JSONL 每条记录包含渲染器、LOD、最终槽表、字节长度和 CRC32C。要直接用 Windows DX11 的 `record_qpc` 对齐异步提交，请使用下文直接读取 QPC 的入队观测。使用 Unity 内建打包且只能访问公开场景 API 时，先导出 mesh-local 槽表。最终 GPU palette 的槽表仍需可插桩的打包入口。

可以在角色切换、LOD 资源实例化完毕或新场景加载后调用导出。用一个最小 Unity 场景验收时，让两个渲染器共用一个 rig，并使用顺序不同的 `bones` 数组和一组独立低模。确认它们可按相同 Transform 身份对齐，槽号和 Mesh 身份仍各自对应。若只能读取 GPU 缓冲，可用同帧唯一矩阵和 blend index 分布提出局部对应。重复矩阵、姿态变化、裁掉的骨骼、不同 bind pose 和独立 rig 资源变体会留下歧义。

#### 关联生产者入队与渲染线程上传

APMI 的大世界与编组样本各包含六个部件。12 条上传都从 `UnityGfxDeviceWorker` 线程 70724 录制并提交。每条原生展开 12 帧，保留的 8 个外部帧逐项一致，末尾进入 Kernel32 和 ntdll，样本均未触及展开或帧数上限。这些栈覆盖了图形命令的消费线程。要确认是谁创建或入队这条命令，需在生产者边界另行记录。

在拥有的 Unity 项目或开发构建中，对选中的 palette 调用 `CapturePaletteEnqueue`。它生成 `enqueueId`，记录外观观测 ID、Renderer、生产者 Windows 线程 ID、托管线程 ID、限量托管栈，以及最终上传区间和 CRC32C。在现有入队调用的前后各调用一次 `PaletteEnqueueTimestamp()`，随后用 `AppendPaletteEnqueue` 写入 JSONL。Windows 下使用与 DX11 一致的原生 QPC，并记录与 MCP 一致的进程实例 ID。托管栈会受 IL2CPP 构建设置影响。记录的是调用该采集入口的线程，需把入口放到实际调用排队 API 的位置。[Unity IL2CPP 托管栈说明](https://docs.unity3d.com/2022.3/Documentation/Manual/IL2CPP-managed-stack-traces.html)

- 应用拥有生产和消费两端时，可把 `enqueueId` 留在命令的诊断元数据中。消费端在实际图形 API 上传处记录相同 ID、线程和 QPC，可据此进行一对一确认。上传内容保持原样。
- 对应用自己录制的 Unity `CommandBuffer`，可用 `IssuePluginEventAndData` 将带 ID 的渲染回调与 `SetBufferData` 放在同一命令缓冲，实验测量消费端的标记区间。回调数据须存活到执行完成。只有实测确认 DX11 上传处于同一消费线程和标记区间内，才能用该区间缩小匹配。回调携带的是自身标记，Unity 公开 API 没有提供把它自动附到内建蒙皮命令的入口。[IssuePluginEventAndData](https://docs.unity3d.com/2022.3/Documentation/ScriptReference/Rendering.CommandBuffer.IssuePluginEventAndData.html) · [SetBufferData](https://docs.unity3d.com/2022.3/Documentation/ScriptReference/Rendering.CommandBuffer.SetBufferData.html)
- APMI 的内建蒙皮路径要取得真实的队列关联 ID，需有可插桩的 Unity 图形命令生产和消费边界，或由拥有者提供同构建的引擎诊断。当前实机材料提供了消费端上传的准确时间、区间和哈希，可在取得生产者记录后用于配对。

可用 `scripts/unity/match_palette_uploads.py` 对照生产者 JSONL 与导出的 DX11 JSON。后者应包含 `target_id`、`qpc_frequency` 和由 `dx11_resource_trace_event` 返回的 `events` 数组。传入开发构建实测的队列延迟上界，脚本用进程、原生 QPC、区间、字节数和 CRC32C 找候选。一个候选仍保留 `candidate` 状态。重复指纹或同一上传可归给多个生产者时报告 `ambiguous`。若拥有两端的项目在实际上传处记录了 ID，可把它作为同一消费者事件的 `producer_enqueue_id`，再核对 `matched_by_id`。重新打包、拆分或没有可读区间的写入需要对应的分段记录。

~~~powershell
python scripts/unity/match_palette_uploads.py producer.jsonl dx11-uploads.json --window-ms 30
~~~

同一条延迟上传被多次执行时，匹配工具按 `record` 核对一次 CPU 调用，并在 `uploads` 中保留各次提交的 `submission` 和 `execute_id`。

这里的 30 毫秒是命令用法示例。窗口之外没有命中时，检查队列延迟、采集范围和数据重排，再解释结果。

在拥有消费端的构建中，也可以通过 `--upload-windows uploads.jsonl` 自动关联 ID，无需改写 MCP 导出事件。每条 JSONL 记录的 `kind` 为 `dx11_upload_call`，字段为 `enqueueId`、`targetId`、`timestampClock: "windows_qpc"`、`timestampFrequency`、`nativeThreadId`、`beforeUpload` 和 `afterUpload`。时间应紧贴同一线程上的一次实际 DX11 调用。脚本拒绝覆盖多个调用或给同一次提交分配不同 ID 的窗口，再交叉核对上传区间和 CRC32C。通用的渲染回调区间继续用作候选线索。

#### APMI 的 native palette 入队点

本机 UnityPlayer 2022.3.62.9394352 已从 12 条实机消费栈反查出统一的队列命令 `0x274B`。以下为同一二进制内核对的 RVA，可用 `scripts/unity/verify_apmi_palette_queue.py` 在当前 DLL 和深栈对照 JSON 上复核。验证器先核对 DLL 的 SHA256，再检查指令、分发表、虚表和已有实机栈。

| 边界 | 已核对的证据 |
| --- | --- |
| 上游准备 | `0x54E66D` 和 `0x54F047` 调用 `0x54D230`，后者在 `0x54D2C0` 调用 `0x54D040`。该函数按每个 64 字节的 pose 准备缓冲，并引用 `Failed to create compute skin pose buffer!` |
| 生产者 | `0x54D208` 调用图形设备虚表槽 `+0x3A8`，入队实现为 `0xA129F0`。`0xA12A6A` 写命令值，`0xA12AC7`、`0xA12B26` 写两个对齐的引用，`0xA12B40` 发布写进度 |
| 消费者 | worker 在 `0x1112744` 读命令，分发表将 `0x274B` 送到 `0x111E08F`，由 `0x111E163` 调用后端 `0x740A80` |
| DX11 上传 | `0x740A80` 将 pose 数乘以 64，转入 `0x93DB50`，在 `0x93DD24` 调用 `UpdateSubresource`。已有深栈的返回地址依次覆盖两层后端调用和 worker 回路 |

下一次生产者采集可直接以上表的写命令、发布进度和读命令为观测边界。建议在同一运行实例内记录队列标识、逻辑发布序号、命令、两个引用、线程、QPC 和调用栈，消费时沿同一个队列、序号继续关联到 DX11 `record_qpc`。若只能观察循环缓冲的游标，先同时核对队列身份和绕回次数，再尝试对齐；单独的游标会复用。两个引用和 CRC32C 可用来交叉验证。生产者线程与队列实例是否在实际运行中按这一关系配对，仍需两端记录共同验证。

#### 一次采集一个 APMI 场景的生产者证据

新版 DLL 的 `dx11_status.features` 包含 `apmi_palette_producer` 和 `resource_trace_ib_groups`。将 APMI 停在要核对的场景，再运行下面对应的一行。将 `12345` 替换为 `dx11_discover` 返回的新 PID。每条命令在同一窗口内查询该场景的六个已知背靠 IB，并输出各自的 CS-t2、CPU 上传及生产者栈证据。

~~~powershell
python scripts/unity/capture_apmi_palette_producer.py --pid 12345 --scene details --output details-producer.json
python scripts/unity/capture_apmi_palette_producer.py --pid 12345 --scene world --output world-producer.json
python scripts/unity/capture_apmi_palette_producer.py --pid 12345 --scene team --output team-producer.json
~~~

详情和大世界预设来自六部件样本，编组使用实测的低模 IB。若换装或版本变化导致 IB 不同，先从当前场景确认实际 IB，再重复传入 `--ib <hash>` 覆盖该场景的列表。`missing_ib_hashes` 表示当前窗口缺少该绘制。`unconfirmed_producer_ib_hashes` 表示已找到绘制但生产者证据尚未确认。每个场景可先用默认两帧，若 `dropped` 或 `unity_producer_counts.omitted` 非零，可用 `--frames 1` 缩短窗口。

原生采集默认关闭。手动使用 `dx11_resource_trace` 时用 `ib_hashes` 选择一组 IB，传 `producer_frames: 24` 启用生产者栈。DLL 只在 x64 APMI 进程内、UnityPlayer 文件 SHA256 和加载后指令都匹配时装入观测 detour。原函数照常执行，采集仅读取队列元数据与调用栈。一次最多保留 2048 条生产者记录。采集结束后进入透传。同版本进程中后续采集复用已经安装的 detour。

用上传事件的 `unity_producer` 判读结果。`queue_position_and_references` 表示同一队列逻辑位置、两个引用和最终 D3D11 源地址及字节数一致。`synchronous_call` 表示同线程直接调用链也通过上传参数核对。`references_candidate` 用于缺少队列位置而只命中引用的候选。`ambiguous_queue_evidence`、`producer_not_observed` 和 `backend_upload_arguments_differ` 分别标出歧义、窗口缺失和后端上传参数差异。比较三个场景时重点看已确认事件的 `producer_thread`、`producer_qpc` 与 `frames`，再用 Unity 侧外观根和装配记录核对这些路径是否汇合。

这条路径定位到 compute skin pose buffer 的准备和提交。它可用于追问每个渲染器的 palette 来自哪里。整套外观的入口仍用下文的外观根与装配完成事件核对。与当前 DLL 的 CodeView 标识对应的公开 Unity PDB 查询返回 404，当前结果按已核对的 RVA 表示。磁盘 IL2CPP 元数据使用自定义文件头，托管候选方法仍以拥有者的运行时枚举或匹配构建符号为准。

#### 核对角色外观装配入口

1. 在可插桩的 Unity 项目或开发构建中，调用 `ExportCandidateMethods(path, assemblyName, terms)`。指定已加载的项目程序集和角色、外观、渲染器等名称片段。它只读枚举保留的类型和方法签名，最多输出 256 个候选，并标出类型缺失或截断。名称匹配只作为候选，场景资源或程序集被裁剪时需要补足构建侧的元数据。
2. 在一个真正观察到的装配完成位置调用 `AppendAppearanceObservation`。传入程序集、类型和完整方法签名、唯一观测 ID、场景标签、外观根对象及装配前后时间。输出包含根对象下的蒙皮和静态 Mesh 渲染器、共享材料、LOD、Animator、Avatar、根骨与层级。已有的 `AppendPaletteUpload` 可以附上同一观测 ID，给最终槽表和上传内容建立关联。
3. 每个场景从外观实例创建前开启 DX11 观测。外观装配记录通过 `assemblyObservationId` 关联生产者提交，生产者记录再按上文与消费者核对。用最早确认匹配的 `submit_qpc` 标记这次窗口中观察到的首次 D3D11 提交。组装完成与实际消费使用各自的观测事件，可以区分场景包装器和后续渲染任务。
4. 用角色详情、大世界和编组各自的装配记录确认候选是否覆盖脸眼、身体和头发的渲染器集合。三种场景都命中同一程序集、类型和方法签名，且它确实产出这些部件所共用的外观根，才足以确认统一入口。额外的场景包装器、不同 LOD 或资源变体可与共同装配方法一起记录。

已在本机 APMI `GameAssembly.dll` 的 PE 导出表核对域、程序集、image/class 和方法枚举入口，包括 `il2cpp_domain_get_assemblies`、`il2cpp_image_get_class`、`il2cpp_class_get_methods`、`il2cpp_method_get_name`。这些入口可以在已初始化的 IL2CPP 环境中提供保留的元数据。当前 DLL 的导出表未发现 `il2cpp_method_get_pointer`，方法列表本身无法把任意 GameAssembly RVA 可靠地还原为托管方法。需要同构建生成的符号或地址映射，或在拥有的 Unity 项目中记录实际装配调用。现有 APMI 运行证据可以验证 DX11 上传链，Unity 项目内的候选枚举和三场景装配关联仍待实测。

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

支持的 DLL 会在 `dx11_status.features` 中公布 `texture_dhash`。实时查询在绑定视图可见的 mip 范围内，根据已跟踪的上传选择最新的可用层级并计算 dHash。模组匹配继续由 `dhash_allow_rehash` 决定是否更新自己的缓存。缺少当前版本的缓存时，计算可能等待 GPU，建议只传需要核对的槽位。

## 捕获时点和 F8 回退

实时回读使用 staging 复制，在匹配的调用处同步取回字节。默认在调用前读取输入缓冲，在调用后读取 UAV 或 stream output。可用 `phase` 显式选择时点。大缓冲区可能使该帧等待 GPU，优先传 `offset` 和 `length`。延迟上下文录制与重放、其他 swap chain/context，以及完整的 API 调用串使用原生 F8。通过 MCP 触发 F8 后，用 `dx11_frame_analysis_files` 列出产物，再用 `dx11_frame_analysis_read` 读取所需文件。纹理字节保留在原生 F8 路径中。
