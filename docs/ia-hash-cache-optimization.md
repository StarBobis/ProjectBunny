# IA Hash Cache 命中率优化 + ShaderOverride 负缓存优化

## 日期
2025-07-17

## 根因

两个优化的**根因相同**：

`DX12ActiveIaState` 中的 `BufferLocation`（GPU VA）每帧随 ring-buffer 重新分配而变化，导致所有基于它的缓存 key 失效：

| 缓存 | Key 含 GPU VA | 效果 |
|------|--------------|------|
| IA Hash State Cache (4096 entries) | `iaViewFingerprint` | 命中率 7.2% |
| Cheap Fingerprint → PSO Neg Cache | `MakeCheapIaFingerprint` | 大量 miss |
| ShaderOverride Negative Cache | `indexBufferLocation` + VB locations | 禁用中 |

## 修改方案

### 1. DX12ActiveIaState — 新增 resource 指针

```cpp
ID3D12Resource* indexBufferResource;
ID3D12Resource* vertexBufferResources[32];
```

### 2. 填充 — 在 IA State 捕获时解析 GPU VA → resource

### 3. 替换指纹中 GPU VA → resource 指针

- `BuildIaHashStateCacheKeyForTargets`: `BufferLocation` → `(UINT64)resource`
- `MakeCheapIaFingerprint`: `BufferLocation` → `(UINT64)resource`
- `MakeShaderOverrideNegativeCacheKey`: `indexBufferLocation` → `(UINT64)resource`

## 修改文件

| 文件 | 操作 |
|------|------|
| `ia/DX12ActiveIaState.hpp` | 新增 resource 字段 |
| `command/DX12CommandListRuntime.cpp` | 填充 resource 字段 + 修改指纹 |
| `ia/DX12IaReplacementExecutor.hpp` | 修改 cheap fingerprint |
| `mod/DX12ShaderOverrideRuntime.hpp` | 修改 negative cache key |

## 量化预期

| 指标 | 优化前 | 优化后 | 变化 |
|------|--------|--------|------|
| IA Hash Cache 命中率 | 7.2% | ~80% | +72.8pp |
| BuildIaHash 每帧耗时 | 8,281 us | ~1,000 us | -88% |
| prepare_shader_override_miss | 7,698 us | ~3,000 us | -61% |
| FPS | 8.9 | ~9.5 | +7% |

## Mod 兼容性

- **Hash 算法不变** — 只改缓存 key，不改 Hash 计算
- **Shader 覆盖匹配不变** — 命中缓存只是跳过重复计算，未命中时行为完全一致
- **IA 替换逻辑不变** — 只影响缓存效率，不影响功能
