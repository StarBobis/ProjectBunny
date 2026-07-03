# [DX12] / [System] — 运行时配置

控制 DX12 代理模式和覆盖层开关。`[DX12]` 和 `[System]` 均可使用以下 key，`[DX12]` 优先级更高（后读取）。

## Keys

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `proxy_mode` | bool | `true` | 启用 DX12 代理/劫持模式（别名: `dx12_proxy_mode`） |
| `safe_mode` | bool | `false` | 安全模式 — 禁用所有 mod 覆盖（别名: `dx12_safe_mode`） |
| `overlay` | bool | `false` | 启用游戏内覆盖层（别名: `dx12_overlay`） |

## 案例

### 完整 DX12 代理 + 覆盖层

```ini
[DX12]
proxy_mode = true
safe_mode = false
overlay = true
```

### 兼容写法（使用 [System]）

```ini
[System]
dx12_proxy_mode = true
dx12_safe_mode = false
dx12_overlay = false
```

### 仅劫持不覆盖（用于调试）

```ini
[DX12]
proxy_mode = true
safe_mode = true
overlay = false
```
