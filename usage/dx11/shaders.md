# ShaderOverride 与文件补丁

## 按 Hash 执行命令

```ini
[ShaderOverrideExample]
hash = 0123456789abcdef
CheckTextureOverride = ps-t0
x = 1
```

Hash 为导出的 16 位十六进制着色器标识。命中时运行命令；它与资源 Hash 不是同一类值。

| 选项 | 用途 |
| --- | --- |
| `hash` | 目标着色器 |
| `allow_duplicate_hash` | 允许多个同 Hash 规则的显式配置 |
| `depth_filter` | `depth_active` 或 `depth_inactive` |
| `model` | 指定重编译目标模型，如 `vs_5_0` |
| `disable_scissor` | 关闭该路径裁剪 |
| `filter_index` | 设置着色器筛选值 |

其余行可以是命令，例如 `handling = skip`、资源绑定、变量和 `run`。

## 编辑着色器

1. 开启 Hunting，定位目标并标记。
2. 在 `ShaderFixes` 找到导出的文件，备份。
3. 编辑后按 F10，检查编译结果和画面。

常见名称：

```text
0123456789abcdef-ps_replace.txt    HLSL 替换
0123456789abcdef-ps.txt            汇编补丁
```

保留工具生成的阶段、Hash 和后缀。HLSL 反编译失败时使用汇编，不要把反编译输出当作必然等价的原始源码。

## 输出与缓存

`[Rendering]` 中：

| 选项 | 作用 |
| --- | --- |
| `override_directory` | 补丁目录，模板为 ShaderFixes |
| `cache_directory` | 缓存目录，模板为 ShaderCache |
| `storage_directory` | 原始着色器输出目录，模板为 ShaderFromGame |
| `cache_shaders` | 缓存着色器 |
| `export_shaders/export_hlsl/export_fixed` | 导出原始、HLSL 或修复结果 |
| `dump_usage` | 导出使用关系 |

排错时先确认实际命中和编译错误，再处理缓存。更改反编译规则等缓存相关选项后，按模板说明退出游戏并清理对应缓存。
