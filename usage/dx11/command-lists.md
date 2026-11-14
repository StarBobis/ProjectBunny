# 命令列表与执行时机

```ini
[Constants]
global $enabled = 1

[CommandListApply]
if $enabled
    x = 1
else
    x = 0
endif

[Present]
run = CommandListApply
```

`run` 在当前位置调用；命令按出现顺序执行。条件支持 `if`、`else if`/`elif`、`else`、`endif`。不要把任意循环语法写入 INI；需要遍历时使用池范围或有界命令调用。

## pre / post

绘制钩子中的普通命令通常在原始 draw 前执行；`post` 在原始操作之后执行：

```ini
[ResourceSaved]

[ShaderOverrideExample]
hash = 0123456789abcdef
ResourceSaved = ref ps-t0
post ps-t0 = ref ResourceSaved
```

这是保存/恢复结构，需在两者之间加入实际替换。`post` 是单独阶段，不是“下一行之后”。

`[Present]` 的普通命令位于一帧末尾，`post` 用于下一帧开始的状态准备。嵌套调用也有 pre/post 阶段。

## 控制命令

| 命令 | 作用 |
| --- | --- |
| `run = CommandList名称` | 调用命令列表 |
| `run = CustomShader名称` | 运行自定义着色器 |
| `handling = skip` | 跳过原始绘制，必须在前置阶段 |
| `handling = abort` | 结束当前命令执行路径 |
| `CheckTextureOverride = ps-t0` | 对该槽资源执行匹配的 TextureOverride |
| `preset = Preset名称` | 激活预设 |
| `exclude_preset = Preset名称` | 排除预设 |
| `reset_per_frame_limits = 资源或着色器` | 重置指定对象的每帧执行/复制限制 |
| `special = draw_3dmigoto_overlay` | 在需要的自定义绘制位置调用叠加层 |
| `special = upscaling_switch_bb` | 配合上采样切换后备缓冲 |

绘制、资源操作、读回和导出分别见对应参考页。嵌套调用上限为 256；不要依赖递归做无界遍历。

## 回调与代理

```ini
[CommandListCallback]

[CommandListImplementation]
x = 1

[CommandListRegister]
CommandListCallback = ref CommandListImplementation
run = CommandListCallback
CommandListCallback = null
```

`ref` 转发执行，不复制命令；`null` 恢复目标自身命令。跨命名空间只能填充空回调，不能覆盖另一空间已有命令的列表；清除代理仍允许。

代理赋值捕获当时解析到的根，改变中间别名不会自动重定向所有旧绑定。成环赋值被忽略；F10 后重新建立引用。
