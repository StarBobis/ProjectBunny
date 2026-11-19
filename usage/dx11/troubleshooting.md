# 排错

## Mod 没有效果

1. 确认游戏使用 DX11，加载的是对应游戏适配包。
2. 检查 `Mods` 下确实有 INI，而非未解压的压缩包。
3. 检查 `[Include]` 路径，以及目录是否被 `DISABLED*` 排除。
4. 按 F10，检查 `d3d11_log.txt` 中的配置警告。
5. 只启用一个已知可用的 Mod。游戏更新后可能需要新的 Hash 或适配。

没有定位面板时，先检查 `hunting`，不要直接判断 DLL 未加载。

## 修改默认值无效

`persist` 的已保存值会覆盖声明默认值。需要重置时，备份后按 Ctrl + Alt + F10；该操作影响所有持久设置。

## 按键无效

让游戏处于前台，检查修饰键、小键盘、重复绑定及 `[Key] condition`。使用 `[Input]` 的配置还需确认 Mod 输入未被关闭。

## 画面异常或崩溃

退出游戏，将新增 Mod 移出 `Mods`，或按排除规则禁用，再启动。逐个恢复以定位触发项。仅重载配置无法撤销已经替换的 DLL。

## 更新后排查 DX11 运行期优化

先确认游戏已重启并加载新版 DLL。若异常可重复触发，在游戏的 `d3dx.ini` 下临时设置以下选项，再按 F10 对照同一场景。游戏无法进入时，退出后改配置再启动。

```ini
[Rendering]
mod_runtime_optimizations = 0
```

若异常消失，恢复总开关，按[运行期优化开关](./settings)分别对照相关路径。拍摄可比较的帧时间时关闭额外 profiling 计数和 F8 抓帧。反馈时附上触发异常的开关、配置与日志。

## 反馈问题

提供游戏与版本、DLL 版本、加载方式、复现步骤、相关 INI，以及 `d3d11_log.txt`。说明禁用全部 Mod 后是否仍发生。

日志较大时先提取尾部：

```powershell
Get-Content .\d3d11_log.txt -Tail 200
```

## 屏幕通知中的中文

DX11 的错误、警告、通知、Hunting 和性能统计文字使用 Windows GDI Unicode 字体绘制，不再依赖内嵌 SpriteFont 字库。中文通知使用 Microsoft YaHei UI，等宽统计使用 Consolas 和系统字体链接。英文错误提示不会自动翻译。

窄字符屏幕日志接口使用 UTF-8，宽字符接口使用 UTF-16，格式化不依赖系统当前区域设置。非法 UTF-8 会使用替代字符；上游已经变成 `?` 的文本无法通过字体恢复。系统须安装覆盖所需文字的字体。

文字遮罩在内容变化时生成并上传，重复消息复用缓存；窗口尺寸变化会清空对应缓存。每种字体缓存最多 64 条、16 MiB。通知按视口宽度自动换行，单块文字纹理最多 4096×1024 像素，超出部分裁切。保留 SpriteBatch 合成，不要求游戏以 BGRA 标志创建设备。中文渲染不使用 DirectWrite、Direct2D、WIC 或 COM；仅使用原有的 GDI32/USER32 和 D3D11。字体和位图在首次可见文字时创建，不在 DLL 加载时初始化。

开发回归运行 `ctest --test-dir build/dx11-dump-validation --output-on-failure`。Overlay 专项生成 `overlay-chinese.png`，覆盖 Unicode、并发缓存、GDI 句柄释放、COM 状态不变、实际 WARP 合成及交换链缩放；代理加载测试验证导出和 Hook 路由。这不替代目标游戏全屏、HDR、内核驱动和其他 Hook 的兼容性验收。
