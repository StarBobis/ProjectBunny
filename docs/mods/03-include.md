# [Include*] — INI 包含指令

递归加载外部 INI 文件，支持目录递归和排除规则。每个 `[Include*]` section 独立处理。

## Keys

| Key | 类型 | 说明 |
|---|---|---|
| `include` | 路径（相对） | 包含单个 INI 文件。路径相对于当前 INI 所在目录 |
| `include_recursive` | 目录（相对） | 递归包含目录下所有 `.ini` 文件。路径相对于当前 INI 所在目录 |
| `exclude_recursive` | 名称/模式 | 排除匹配的文件或目录。支持 `*` 后缀通配。`DISABLED`/`disabled` 前缀自动排除 |

## 命名空间规则

通过 `[Include]` 加载的文件自动获得基于相对路径的命名空间：
- `mods\characters\hero.ini` → namespace `mods\characters`
- 文件内的 `[ResourceFoo]` 自动解析为 `Resource\mods\characters\ResourceFoo`
- 跨文件引用时使用全限定名或相对引用

## 案例

### 基础包含

```ini
; d3dx.ini
[IncludeMain]
include = mods/main.ini

[IncludeCharacters]
include_recursive = mods/characters
exclude_recursive = DISABLED*
exclude_recursive = *_backup*
```

### 加载多个 mod 包

```ini
[IncludeMods]
include_recursive = Mods

[IncludeShaderFixes]
include = ShaderFixes/overrides.ini
```

### 目录结构示例

```
d3dx.ini
mods/
  characters/
    hero.ini          → namespace "mods\characters"
    enemy.ini         → namespace "mods\characters"
    DISABLED_boss.ini → 被 exclude 跳过
  effects/
    particles.ini     → namespace "mods\effects"
ShaderFixes/
  overrides.ini       → namespace "ShaderFixes"
```
