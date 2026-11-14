# 常用写法

各例独立使用。Hash、资源槽和数据布局按游戏替换；不要把整页拼成一个 INI。

## 按部件切换模型

```ini
[Constants]
global persist $variant = 0

[KeyVariant]
key = NO_MODIFIERS F6
type = cycle
$variant = 0, 1

[ResourceIndices]
type = Buffer
format = R32_UINT
filename = body.ib

[ResourceVertices]
type = Buffer
stride = 32
filename = body.buf

[TextureOverrideBody]
hash = 12345678
match_first_index = 0
match_index_count = 300
if $variant == 1
    handling = skip
    ib = ResourceIndices
    vb0 = ResourceVertices
    drawindexed = 300, 0, 0
endif
```

需要 `body.ib/body.buf`，步长、格式与数量只是例值。变体 0 使用游戏绘制，1 使用自定义绘制。

## 保存并恢复一组贴图

```ini
[PoolSaved]
pool_size = 4

[ShaderOverrideMaterial]
hash = 0123456789abcdef
PoolSaved[0:3] = ref ps-t[0:3]
; 在这里添加实际替换
post ps-t[0:3] = ref PoolSaved[0:3]
```

用 `ref` 保留绑定，不复制像素。若需要内容快照，改用明确的 copy 并考虑 GPU 成本。

## 扫描未知槽位

```ini
[Constants]
global $slot = -1
global $i = 0

[ResourceFound]

[CommandListStart]
$slot = -1
$i = 0
ResourceFound = null
run = CommandListFind

[CommandListFind]
if $i < 16
    if ps-t[$i]->Format == DXGI_FORMAT_R8G8B8A8_UNORM
        $slot = $i
        ResourceFound = ref ps-t[$i]
    else
        $i = $i + 1
        run = CommandListFind
    endif
endif
```

从目标 draw 调用 `CommandListStart`。格式只作候选筛选；未找到时 slot 为 -1，不继续用它索引。扫描上限必须给嵌套调用预算留空间。

## 按对象保存状态

```ini
[Rendering]
cache_resource_data = constant_buffer

[Constants]
global $object = 0

[PoolState]
pool_size = 64
pool_index_type = fifo
pool_variable_default_value = 0
pool_expiration_timeout_frames = 120

[CommandListTrack]
$object = vs-cb0->HashRegion(0, 64)
if $object >= 0
    $PoolState[$object] = FRAME_NUMBER
endif
```

仅当选中数据足以区分对象且缓存有效时有意义。内容相同、哈希碰撞或数据更新都可能改变关联；需要精确对象身份时不能只依赖此例。

## 低频读回

```ini
[Constants]
global $last_sample = -1
global $value = 0

[CommandListSample]
if FRAME_NUMBER != $last_sample
    if FRAME_NUMBER % 30 == 0
        $last_sample = FRAME_NUMBER
        store = $value, vs-cb0, vs-cb0->Offset / 4
    endif
endif
```

从有效 CB 上下文调用；每 30 帧最多读一次。频率按需求选择，不把它当作零成本实时监控。
