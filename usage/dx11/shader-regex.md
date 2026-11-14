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
