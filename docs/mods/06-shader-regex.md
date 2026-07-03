# [ShaderRegex*] — Shader 正则匹配

按 Shader Model 目标批量匹配 shader，支持分组和子 pattern。

**Section 命名**:
- `[ShaderRegex<组名>]` — 主配置节
- `[ShaderRegex<组名>.<Pattern名>]` — 子 pattern 节（设置 `hasPattern = true`）

## Keys

### 主 Section

| Key | 类型 | 说明 |
|---|---|---|
| `shader_model` | 字符串（空格分隔） | Shader Model 目标列表。如 `vs_4_0 ps_5_0 cs_5_0` |
| `handling` | 字符串 | `"skip"` — 跳过使用匹配 shader 的 Draw/Dispatch |

### 子 Section

子 section（如 `[ShaderRegexMyGroup.Pattern1]`）表示该组有一个 pattern（设置 `hasPattern = true`）。子 section 中的具体 pattern 内容由正则引擎处理。

`temps` 和 `filter_index` 被识别但不处理。

## 案例

### 跳过所有 VS 4.0 Shader

```ini
[ShaderRegexSkipVS40]
shader_model = vs_4_0
handling = skip
```

### 多 model 匹配

```ini
[ShaderRegexOldShaders]
shader_model = vs_4_0 ps_4_0 cs_4_0
handling = skip

[ShaderRegexOldShaders.PatternA]
; 子 pattern 配置
```

### 按 model 分类替换（不跳过）

```ini
[ShaderRegexTargetVS50]
shader_model = vs_5_0
; handling 不设置，仅标记匹配用于其他处理

[ShaderRegexTargetPS50]
shader_model = ps_5_0
```
