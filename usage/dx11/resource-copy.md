# 槽位、复制与引用

## 目标写法

| 目标 | 含义 |
| --- | --- |
| `Resource名称`、`Pool名称[索引]` | 自定义资源、池元素 |
| `vb0`–`vb31`、`ib` | 顶点、索引缓冲 |
| `vs/hs/ds/gs/ps/cs-tN` | 对应阶段的 SRV |
| `vs/hs/ds/gs/ps/cs-cbN` | 常量缓冲，N 为 0–13 |
| `ps-uN`、`cs-uN` | UAV，范围取决于设备支持 |
| `o0`–`o7`、`od` | 渲染目标、深度模板 |
| `so0`–`so3` | 流输出目标 |
| `this` | 当前被 TextureOverride 检查的资源 |
| `bb` | 后备缓冲，作为支持操作的来源 |
| `r_bb/f_bb` | 上采样场景的真实/虚拟后备缓冲来源 |
| `iniparams` | 着色器可读 INI 参数资源 |
| `cursor_mask/cursor_color` | 光标遮罩/颜色资源 |

`vs/hs/...` 表示选择一个阶段，不是可直接粘贴的连写前缀。

## copy 与 ref

```ini
[ResourceSnapshot]
[ResourceAlias]

[CommandListCapture]
ResourceSnapshot = copy ps-t0
ResourceAlias = ref ps-t0
ps-t1 = ref ResourceSnapshot
```

- `copy`：独立存储，复制执行时的内容；后续原资源写入不改变快照。
- `ref`：共享底层资源；游戏后续更新仍可见，不是快照。
- `copy_desc`：复制描述并创建所需目标，不作为内容备份。
- `null`：清除引用或解绑。

建议明确写出 `copy/ref`，避免把默认行为当作所有目标都一致。

## 修饰选项

| 选项 | 意义 |
| --- | --- |
| `unless_null` | 源为空时保留目标 |
| `no_view_cache` | 不复用该操作的视图缓存 |
| `raw` | 需要原始缓冲视图的操作 |
| `set_viewport` | 绑定目标时同步视口 |
| `mono` | 以单目资源方式创建复制结果 |

```ini
ResourceAlias = ref unless_null ps-t0
ps-t0 = null
```

引用要求资源原本具备目标用途的 BindFlags；已经创建的原生资源不能凭一次引用获得新用途。格式、维度、采样数不兼容时应检查日志，不要靠强制转换掩盖错误。

区域、动态槽与批量操作见[区域与范围](./regions)。
