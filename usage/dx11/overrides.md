# 编写替换

先通过[定位](./hunting)取得 Hash。下面的 Hash 是占位值，必须换成当前游戏中的目标。

## 隐藏绘制

```ini
[TextureOverrideHide]
hash = 12345678
handling = skip
```

放入 `Mods/Example/mod.ini`，按 F10。若隐藏范围过大，需要进一步限定绘制，而不是把多个无关对象一起跳过。

## 条件隐藏

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

F6 切换显示。若没有变化，先确认 Hash、匹配对象和按键是否正确。

## 贴图资源

```ini
[ResourceDiffuse]
filename = diffuse.dds

[TextureOverrideDiffuse]
hash = 12345678
this = ResourceDiffuse
```

`diffuse.dds` 放在该 INI 旁。目标 Hash 应来自原始贴图；替换图需适配目标用途、格式与尺寸要求。

模型替换还需要匹配顶点布局、索引格式和绘制范围，不能只替换 Hash 或资源文件名。
