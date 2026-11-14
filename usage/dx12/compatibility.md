# DX11 配置迁移范围

按当前仓库运行时整理。节名被识别或某个字段被存入内部结构，不等于已经接入执行。

| 功能 | DX12 |
| --- | --- |
| Include、递归加载、排除规则、命名空间 | 可用 |
| global/local/persist、分支、基本四则与逻辑表达式 | 可用 |
| Key cycle/hold/toggle、condition、反向 cycle | 可用；不含 DX11 全部输入后端与过渡选项 |
| TextureOverride、ShaderOverride | 可用，支持字段以对应参考页为准 |
| Resource 文件、资源别名、图形纹理 SRV 捕获 | 可用，依赖执行上下文 |
| CustomShader | 有独立执行路径，状态与资源支持不等同 DX11 |
| Present、CommandList | 可用；post 和嵌套边界不同 |
| ShaderRegex 无 Pattern 的模型筛选 | 可用 |
| ShaderRegex PCRE2 Pattern/Replace/InsertDeclarations | 不支持 |
| Pool、Region、动态槽与槽范围 | 不支持 DX11 扩展语法 |
| `@/#/->`、资源 getter、dHash | 不支持 DX11 表达式接口 |
| 位运算、位移、数学函数、random、locked | 不支持 DX11 扩展语法 |
| store、dump、clear、analyse_options | 无同等通用命令，抓取用 F8/F9 |
| Preset、输入暂停模式、DX11 全局 Device/Rendering/Logging | 不按 DX11 语义执行 |
| 完整显式 instanced/indirect 命令集合 | 未完整提供 |
| DX11 输入布局设置器、Stream Output | 不直接迁移 |
| DX11 立体驱动配置 | 不适用 |

## 迁移顺序

1. 先确认游戏的 DX12 加载方案。
2. 重新捕获 Hash、槽位、draw/dispatch 参数。
3. 从单个静态贴图或模型开始，验证资源格式与布局。
4. 加入按键、变量与条件逻辑。
5. 最后处理 GPU 蒙皮、自定义着色器和复杂状态。

游戏能启动、INI 没有报错都不足以证明替换路径已执行。每次只增加一类行为，对照原画面与禁用 Mod 的结果。
