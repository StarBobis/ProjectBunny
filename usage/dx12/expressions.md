# 变量与表达式

```ini
[Constants]
global $enabled = 1
global persist $variant = 0

[CommandListNext]
local $next = ($variant + 1) % 3
$variant = $next
```

`global` 为共享数值，`local` 属于调用作用域，`persist` 记录到用户设置文件。DX12 的数值求值路径不同于 DX11，不套用其 float32 位操作或资源 getter。

## 运算

从高到低：

| 优先级 | 支持语法 |
| --- | --- |
| 1 | `()`、单目 `! + -` |
| 2 | `* / // %` |
| 3 | `+ -` |
| 4 | `< <= > >=` |
| 5 | `== !=` |
| 6 | `&&` |
| 7 | `\|\|` |

`//` 为向下取整的除法。相等使用约 `1e-6` 容差；逻辑表达式会求值两边，不依赖短路保护危险表达式。接近零的除数会得到零回退，应在脚本中主动检查。

当前不支持 DX11 的 `**`、位运算、位移、`===`、数学函数、`random`、二进制扩展字面量、`@/#/->` 或 Pool 访问。

## 分支

```ini
[CommandListSelect]
if $variant == 0
    x0 = 0
else if $variant == 1
    x0 = 1
else
    x0 = 2
endif
```

`elif` 与 `else if` 都可用。每个分支必须闭合；没有通用循环语法。

## IniParams 与延后恢复

`x0/y0/z0/w0`、`x1/y1/...` 为着色器常量分量。

```ini
[Constants]
global $saved_x = 0

[CommandListTemporary]
$saved_x = x87
x87 = 1
post x87 = $saved_x
```

`post` 延后恢复到该调用的 post 阶段，不表示普通语句执行完就立刻恢复。只有配套着色器实际读取该分量时，设值才影响画面。

`locked` 等 DX11 声明扩展不在 DX12 支持范围内。持久值恢复默认的方法见[管理 Mod](./mods)。
