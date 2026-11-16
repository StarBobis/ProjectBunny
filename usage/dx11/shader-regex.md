# ShaderRegex

对着色器**汇编**应用模式匹配和替换。适合处理多个结构相近但 Hash 不同的着色器。

```ini
[ShaderRegexExample]
shader_model = ps_5_0

[ShaderRegexExample.Pattern]
^mov (?P<dst>r\d+\.xyzw), (?P<src>r\d+\.xyzw)\n

[ShaderRegexExample.Pattern.Replace]
mov ${dst}, ${src}\n
```

这是保持指令不变的语法示例；先用真实导出汇编验证匹配，再写修改逻辑。

## 组成

| 节/选项 | 含义 |
| --- | --- |
| 主节 `shader_model` | 必填，可列多个模型 |
| 主节 `temps` | 命名临时寄存器，自动分配并调整声明 |
| 主节命令 | 匹配后执行的 INI 命令 |
| `.Pattern` | PCRE2 模式 |
| `.Pattern.Replace` | 对应替换文本 |
| `.InsertDeclarations` | 插入尚不存在的声明 |

同一组可增加对应的命名 Pattern/Replace 配对。省略 Pattern 会匹配指定模型的所有候选，应严格限制用途。

INI 会移除行首尾空白、空行和注释；需要缩进就匹配 `\s*`，需要换行就写 `\n`。

默认启用多行、忽略大小写。`${0}` 表示完整匹配，`${name}` 引用捕获或命名临时寄存器。替换必须保持合法声明、寄存器与控制流。

## 检查结果

标记匹配着色器，检查 ShaderFixes 导出的修补结果与日志。测试不匹配、部分匹配及游戏不同画质路径；过宽模式可能改到其他效果。

## 排除指定 Shader（DX11）

在主节重复填写 `exclude_hash`，每行一个 Shader Hash：

```ini
[ShaderRegexEnableTextureOverrides]
shader_model = vs_4_0 vs_4_1 vs_5_0 vs_5_1
exclude_hash = 5927c81f6515eb11
exclude_hash = 0123456789abcdef
run = CommandListSkin
```

Hash 使用 1～16 位十六进制，大小写均可，也支持 `0x` 前缀。重复值会合并；空值、非法字符或超长值会报警并禁用该组。

排除只作用于所在 ShaderRegex 组：该组的模式匹配、汇编修改、命令列表和 `filter_index` 均不应用于这个 Shader，其他组及显式 ShaderOverride 不受影响。配置重载会更新排除名单并使旧 Regex 缓存失效；排除检查在 Shader 处理阶段完成，不增加逐 Draw 的条件判断。
