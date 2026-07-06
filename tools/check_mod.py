#!/usr/bin/env python3
"""Check Mod health from log: shader overrides, PreSkin, draw decisions."""
import json, sys

log_path = sys.argv[1] if len(sys.argv) > 1 else r'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl'

# Stage timing counts from DX12CpuStageTiming
stage_counts = {}
# Mod-specific event counts
mod_events = {}

with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        line = line.strip()
        if not line: continue
        parts = line.split('\t', 2)
        if len(parts) < 3: continue
        cat = parts[1]
        try:
            raw = parts[2]
            try: data = json.loads(raw)
            except:
                if raw.startswith('"') and not raw.startswith('{'):
                    raw = '{' + raw + '}'
                try: data = json.loads(raw)
                except: continue
        except: continue

        # Count stage timings
        stage = data.get('stage', '')
        if stage:
            stage_counts[stage] = stage_counts.get(stage, 0) + 1

        # Count mod events
        if cat in ('DX12ModRuntime', 'DX12HotPathState', 'DX12ShaderOverrideRuntime'):
            print(f"{line[:300]}")

# Print critical mod indicators
key_stages = [
    'prepare_shader_override_match', 'prepare_shader_override_miss',
    'apply_and_execute_match',
    'dispatch_prepare_probe', 'apply_replacement_match', 'apply_replacement_miss',
    'descriptor_table_patch_hit',
    'ia_hash_cache_hit', 'ia_hash_cache_miss',
    'pso_ia_negative_miss',
    'cheap_negative_lookup', 'cheap_full_negative_hit',
]
print("\n=== Mod 关键指标 ===")
for s in key_stages:
    if s in stage_counts:
        print(f"  {s}: {stage_counts[s]} 次")
