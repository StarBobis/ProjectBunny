# 编写替换

先用[定位工具](./hunting)复制目标 Hash。DX11 的 Hash 与绑定假设不能直接套用。

## 跳过着色器绘制

```ini
[ShaderOverrideHide]
hash = 0123456789abcdef
handling = skip
```

将示例保存为 `Mods/Example/mod.ini`，把 Hash 换成实际值，按 F10。

同一着色器可能用于多个物体；跳过它可能同时隐藏其他对象。

## 按资源隐藏

```ini
[Constants]
global persist $hide = 0

[KeyHide]
key = NO_MODIFIERS F6
type = cycle
$hide = 0, 1

[TextureOverrideHide]
hash = 12345678
if $hide == 1
    handling = skip
endif
```

将 Hash 换成目标缓冲区 Hash。F6 切换显示；若同 Hash 对应多次绘制，还需要增加匹配条件。

## 自定义缓冲区

```ini
[ResourceMesh]
type = buffer
filename = mesh.buf
stride = 16
byte_width = 4096
```

`mesh.buf` 相对该 INI 寻址。这里仅声明资源；`stride` 和 `byte_width` 必须按实际数据填写，随后由替换命令绑定。

模型替换需要同时核对顶点布局、资源槽位、索引格式与绘制参数。声明成功不代表模型已经接入绘制。
