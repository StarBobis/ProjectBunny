# 帧分析与性能诊断

## 捕获流程

1. 进入目标可见且动作稳定的场景。
2. 按 F8 一次，等待完成提示。
3. 打开 DLL 旁的 `FrameAnalysis-日期-时间`。
4. 按 draw/dispatch 编号对照资源、绑定和着色器信息。

F8 捕获一帧，不使用 DX11 的 `analyse_options`。资源复制可能停顿，预留磁盘空间。

## 读数据

| 数据 | 用途 |
| --- | --- |
| Draw/Dispatch 顺序 | 找到部件实际绘制与计算位置 |
| IB/VB 绑定 | 确认 Hash、步长与索引范围 |
| 描述符/资源绑定 | 对照阶段槽与底层资源 |
| 计算生产者信息 | 判断模型是否先经过 GPU 蒙皮 |
| 实际资源文件 | 检查导出数据，不只依赖屏幕 Hash |

同一资源可有多个视图，Hash 相同不等于视图、偏移和用途相同。缺少某个资源文件时检查跳过/失败记录，不当作该资源内容为空。

## 着色器

F9 抓取帧内着色器，输出到 `ShaderFixes`。等待完成后结合汇编、使用关系与 PSO 摘要分析；它不代替 F8 的完整资源抓取。

## 性能面板

F11 循环切换 Off、Compact、Summary、Top、Sections、Counters；F12 冻结/恢复显示。TIMING 版提供详细计时，数据可用性取决于构建。

定位工具与抓帧会引入额外开销。比较 Mod 性能时保持同场景、同 DLL 构建与相同面板/工具状态，不把抓取期间的停顿当作正常帧率。

## 日志

```powershell
Get-Content .\d3d12_log.txt -Tail 200
Select-String -Path .\d3d12_log.txt -Pattern 'failed|unsupported|warning|error' | Select-Object -Last 50
```

在实际部署目录执行。复现后另存日志；TIMING 构建部署会轮转旧日志。反馈异常时附触发步骤、配置和对应抓取目录中的相关文件。
