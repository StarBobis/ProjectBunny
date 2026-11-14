# 命令列表

```ini
[Constants]
global $enabled = 1

[CommandListApply]
if $enabled
    x0 = 1
else
    x0 = 0
endif

[Present]
run = CommandListApply
```

`run` 调用已声明的列表；`[Present]` 每帧运行。嵌套深度受限，不能照搬 DX11 的 256 层递归设计。

## 命令范围

| 语法 | 用途 |
| --- | --- |
| `$变量 = 表达式` | 更新数值 |
| `local $变量 = 表达式` | 局部值 |
| `if/else if/elif/else/endif` | 分支 |
| `run = CommandList名称` | 调用 |
| `run = CustomShader名称` | 调用自定义着色器元素 |
| `CheckTextureOverride = ps-t0` | 检查固定槽的覆盖规则 |
| `handling = skip` | 跳过原始 draw/dispatch |
| `ib/vbN = Resource名称` | 模型缓冲绑定 |
| `ps-tN/vs-tN/cs-tN` | SRV 绑定 |
| `阶段-cbN`、`ps-uN/cs-uN` | 受相应执行路径支持的 CB/UAV 绑定 |
| `ResourceA = ref ResourceB` | 资源别名 |
| `xN/yN/zN/wN = 表达式` | IniParams 分量 |

命令能被解析与能在所有钩子执行是两回事。绘制、纹理捕获、计算和自定义元素各自需要相应资源上下文。

## pre / post

支持命令列表链接的 `run`、`pre/pre run/prerun`、`post/post run/postrun` 写法；数值与 IniParams 还支持 `post` 赋值。

不要将 DX11 任意 `post <资源命令>` 或自定义资源复制语义整体迁移。需要恢复时先确认该类型命令的执行路径。

## 不可套用的命令

当前没有 DX11 通用的 `store`、`dump`、`clear`、`analyse_options`、`preset/exclude_preset`、命令列表 `ref` 回调、池重置等功能。导出使用内置 F8/F9。
