# DX12 Mod INI 配置系统 — 总览

## 架构

```
d3dx.ini
  └─ MigotoIniLoader (递归 include 解析)
       └─ IniDocument (扁平键值存储)
            ├─ MigotoConfig.Load → LoaderConfig, RuntimeConfig
            ├─ MigotoTextureOverrideParser → TextureOverrideMap
            ├─ MigotoShaderOverrideParser → ShaderOverrideMap
            ├─ MigotoShaderRegexParser → ShaderRegexMap
            ├─ MigotoResourceParser → ResourceMap
            └─ MigotoCommandListParser → CommandListMap
```

## 加载顺序

1. `MigotoIniLoader::LoadWithIncludes()` — 递归加载 `d3dx.ini` 及所有 `[Include*]` 引用的文件
2. `MigotoConfig::Load()` — 读取 `[Loader]` 和 `[DX12]`/`[System]`
3. `MigotoShaderOverrideParser::ParseSections()` — 解析 `[ShaderOverride*]`
4. `MigotoShaderRegexParser::ParseSections()` — 解析 `[ShaderRegex*]`
5. `MigotoTextureOverrideParser::ParseSections()` — 解析 `[TextureOverride*]`
6. `MigotoResourceParser::ParseSections()` — 解析 `[Resource*]`
7. `MigotoCommandListParser::ParseSections()` — 解析 `[CommandList*]` / `[Constants]` / `[Present]`

## 支持的 Section 类型

### 有解析器的 Section

| Section 前缀 | 解析器 | 用途 |
|---|---|---|
| `[Loader]` | MigotoConfig | 注入加载配置 |
| `[DX12]` / `[System]` | MigotoConfig | 运行时开关 |
| `[Include*]` | MigotoIniLoader | 递归包含其他 INI |
| `[TextureOverride*]` | MigotoTextureOverrideParser | 纹理/IA Buffer 替换 |
| `[ShaderOverride*]` | MigotoShaderOverrideParser | Shader 替换 |
| `[ShaderRegex*]` | MigotoShaderRegexParser | Shader 正则匹配 |
| `[Resource*]` | MigotoResourceParser | 资源数据定义 |
| `[CommandList*]` / `[BuiltInCommandList*]` | MigotoCommandListParser | 命令列表 |
| `[Constants]` | MigotoCommandListParser | 常量命令列表（精确匹配） |
| `[Present]` | MigotoCommandListParser | Present 命令列表（精确匹配） |

### 识别但无解析器的 Section（保留给 3DMigoto 兼容）

`[Key*]` `[CustomShader*]` `[BuiltInCustomShader*]` `[Pool*]` `[Preset*]` `[Hunting]` `[Logging]` `[Device]` `[Rendering]` `[Profile]` `[Stereo]` `[ConvergenceMap]` `[ClearRenderTargetView]` `[ClearDepthStencilView]` `[ClearUnorderedAccessViewUint]` `[ClearUnorderedAccessViewFloat]`

## 命名空间系统

当 INI 通过 `[Include]` 递归加载时，每个文件获得基于相对路径的命名空间。Section 引用自动解析：

| 引用写法 | 解析结果 |
|---|---|
| `ResourceFoo` | `Resource\<namespace>\Foo` (自动加前缀) |
| `Resource\OtherNS\Foo` | `Resource\OtherNS\Foo` (已是全限定名) |
| `\Foo` | `Resource\<namespace>\Foo` (以 `\` 开头) |
| `CommandListBar` | `CommandList\<namespace>\Bar` |

## Hash 格式

| 用途 | 位宽 | 示例 |
|---|---|---|
| TextureOverride hash | 32-bit hex | `a1b2c3d4` 或 `0xa1b2c3d4` |
| ShaderOverride hash | 64-bit hex | `deadbeefcafebabe` |
| Shader 字节码 hash | 64-bit FNV-1a | 算法：`hash=14695981039346656037; for each byte: hash^=byte; hash*=1099511628211` |

## NumericMatch 语法

用于 `match_vertex_count`、`match_index_count` 等比较键：

| 写法 | 运算符 |
|---|---|
| `123` | 等于 (=) |
| `=123` | 等于 |
| `!=123` | 不等于 |
| `<123` | 小于 |
| `<=123` | 小于等于 |
| `>123` | 大于 |
| `>=123` | 大于等于 |

## 执行模型

1. **Shader 替换** — PSO 创建时检查 shader hash，匹配则从 `ShaderFixes\<hash>-<stage>.bin` 加载替换字节码并创建新 PSO
2. **Texture/IA 替换** — 每个 DrawCall 前检查 IA 状态（IB hash + VB hash），执行 buffer 替换或 pre-skinning
3. **Vertex Limit Raise (VLR)** — 资源创建时自动放大缓冲区；UAV 视图创建时放大元素计数
