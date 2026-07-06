#!/usr/bin/env python3
"""Quick diagnostic: check runtime recording counts and errors."""
import json, sys

log_path = sys.argv[1] if len(sys.argv) > 1 else r'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl'

counts = {
    'record_runtime_descriptor': 0,
    'record_runtime_copy_range': 0,
}
errors = []

with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        line_s = line.strip()
        if not line_s: continue
        parts = line_s.split('\t', 2)
        if len(parts) < 3: continue
        cat = parts[1]
        raw = parts[2]
        
        for k in counts:
            if k in raw:
                counts[k] += 1
        
        # Find descriptor-related failures
        if cat in ('DX12DescriptorTargetedResolve', 'DX12DescriptorCollectPipelineWait'):
            try:
                data = json.loads(raw)
            except: continue
            print(f"{cat}: {raw[:200]}")
        
        # Find any "blocked" or "miss" in descriptor context
        if ('blocked' in raw.lower() or 'miss' in raw.lower()) and 'descriptor' in raw.lower():
            print(f"DESC_ISSUE: {raw[:250]}")

print(f"\nrecord_runtime_descriptor: {counts['record_runtime_descriptor']}")
print(f"record_runtime_copy_range: {counts['record_runtime_copy_range']}")
