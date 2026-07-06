# PSO 替换决策缓存 + PreSkin 绑定增量更新

## 日期
2025-07-17

## 优化1: SetPipelineState PSO缓存 (14.1ms → ~5ms)

### 根因
625次 SetPipelineState/帧，每次 ~11us。PsoCacheLookup 已极快(12us总计)，瓶颈在 lookup 之后的 Hook 逻辑。

### 方案
对同一 PSO 的重复 Set，缓存 Mod 替换决策结果。PSO 未变化时跳过完整决策。

## 优化2: PreSkin collect_bindings 增量更新 (4.9ms → ~1ms)

### 根因  
47次/帧，每次遍历 root signature 收集所有 CBV/SRV/UAV/Constants 绑定。

### 方案
用 generation counter 追踪绑定变化，仅在变化时重新收集。

## 修改范围
TBD — 代码探查后确定
