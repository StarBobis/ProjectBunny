# [ShaderOverride*] — Shader 替换

根据 Shader 字节码 hash 匹配并替换 PSO 中的 shader。

**Section 命名**: `[ShaderOverride<名称>]`

**索引键**: `hash` — 64-bit FNV-1a 字节码 hash

## Keys

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `hash` | uint64 (hex) | `0` | **必需** Shader 字节码 hash (64-bit FNV-1a)。可带 `0x` 前缀 |
| `handling` | 字符串 | *(空)* | `"skip"` — 完全跳过使用该 shader 的 Draw/Dispatch |

## 替换文件路径

替换 shader 从以下路径加载：
```
<baseDir>\ShaderFixes\<hash>-<stage>.bin
```

其中 `<stage>` 为两字母 shader 阶段：
- `vs` — Vertex Shader
- `ps` — Pixel Shader
- `ds` — Domain Shader
- `hs` — Hull Shader
- `gs` — Geometry Shader
- `cs` — Compute Shader

## Command List 链接

与 `[TextureOverride]` 相同，未识别的 key 作为 Command List 操作解析（`run`/`pre`/`post`/`draw`/`drawindexed`/`dispatch` 等）。

## 案例

### 基础 Shader 替换

```ini
[ShaderOverrideMyVS]
hash = deadbeefcafebabe
```

替换文件：`ShaderFixes\deadbeefcafebabe-vs.bin`

### 跳过特定 Shader

```ini
[ShaderOverrideSkipPS]
hash = 12345678abcdef00
handling = skip
```

### 带命令列表的 Shader 替换

```ini
[ShaderOverrideWithCommands]
hash = aabbccdd11223344
pre = MyPreCmdList
post = MyPostCmdList
```
