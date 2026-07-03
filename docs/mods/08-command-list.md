# [CommandList*] / [Constants] / [Present] — 命令列表

定义命令序列，可设置 IB/VB、执行 Draw/Dispatch、触发 TextureOverride 检查。

**Section 命名**:
- `[CommandList<名称>]` — 标准命令列表
- `[BuiltInCommandList<名称>]` — 内置命令列表
- `[Constants]` — 精确匹配，常量命令列表
- `[Present]` — 精确匹配，Present 命令列表

## Action Keys

每个 action key 映射到一个命令：

| Key | Action | 值格式 | 说明 |
|---|---|---|---|
| `run` | Run | 命令列表引用 | 执行另一个命令列表 |
| `checktextureoverride` | CheckTextureOverride | Target 标识符 | 触发 TextureOverride 匹配（见下方 Target 语法） |
| `draw` | Draw | `count, start` 或 `from_caller` | 执行 Draw(VertexCount, StartVertexLocation)，`from_caller` 表示从调用方复制参数 |
| `drawindexed` | DrawIndexed | `count, start, baseVertex` 或 `from_caller` | 执行 DrawIndexed，`from_caller` 从调用方复制参数 |
| `dispatch` | Dispatch | `groupsX, groupsY, groupsZ` | 执行 Dispatch |
| `handling` | HandlingSkip | `skip` | 跳过此命令列表 |
| `ib` | SetIndexBuffer | 资源引用 | 设置索引缓冲区为指定资源 |
| `vb<N>` | SetVertexBuffer | 资源引用 | 设置顶点缓冲区 slot `<N>` 为指定资源 |

## Target 标识符语法 (checktextureoverride)

| 语法 | Target 类型 | Shader 阶段 |
|---|---|---|
| `ib` | IndexBuffer | — |
| `vb<N>` | VertexBuffer | — |
| `vs-cb<N>` | ConstantBuffer | Vertex |
| `ps-cb<N>` | ConstantBuffer | Pixel |
| `cs-cb<N>` | ConstantBuffer | Compute |
| `vs-t<N>` | ShaderResource | Vertex |
| `ps-t<N>` | ShaderResource | Pixel |
| `cs-t<N>` | ShaderResource | Compute |
| `ps-u<N>` | UnorderedAccessView | Pixel |
| `cs-u<N>` | UnorderedAccessView | Compute |

## Link Keys

| Key | 说明 |
|---|---|
| `run` | 添加到 main 链接列表 |
| `pre` / `pre run` / `prerun` | 添加到 pre 链接列表（在前执行） |
| `post` / `post run` / `postrun` | 添加到 post 链接列表（在后执行） |

## 案例

### 基础 Draw 命令列表

```ini
[CommandListMyDraw]
ib = ResourceMyIB
vb0 = ResourceMyVB0
drawindexed = 36, 0, 0
```

### 触发 TextureOverride 检查

```ini
[CommandListCheckAll]
checktextureoverride = ib
checktextureoverride = vb0
checktextureoverride = vs-t0
checktextureoverride = ps-t3
checktextureoverride = cs-u0
```

### 嵌套命令列表

```ini
[CommandListOuter]
pre = CommandListSetup
run = CommandListDraw
post = CommandListCleanup

[CommandListSetup]
ib = ResourceMainIB

[CommandListDraw]
drawindexed = from_caller

[CommandListCleanup]
dispatch = 1, 1, 1
```

### 从调用方复制 Draw 参数

```ini
[CommandListPassthrough]
draw = from_caller
drawindexed = from_caller
```

### Constants 命令列表

```ini
[Constants]
run = CommandListSetupConstants
checktextureoverride = vs-cb0
checktextureoverride = ps-cb2
```

### Present 命令列表

```ini
[Present]
run = CommandListOnPresent
dispatch = 1, 1, 1
```
