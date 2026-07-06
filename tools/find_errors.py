#!/usr/bin/env python3
"""Find Mod/descriptor errors in log."""
import json
import sys

log_path = sys.argv[1] if len(sys.argv) > 1 else r'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl'
keywords = ['miss', 'fail', 'error', 'not found', 'empty', 'blocked']

with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        line_lower = line.lower()
        hit = False
        for kw in keywords:
            if kw in line_lower:
                hit = True
                break
        if not hit:
            continue
        
        line = line.strip()
        if not line:
            continue
        parts = line.split('\t', 2)
        if len(parts) < 3:
            continue
        cat = parts[1]
        # Skip frame-level categories
        if cat in ('DX12FrameTimeline', 'DX12ProfilingFrame', 'DX12LogCost', 'DX12CpuStageTiming', 'DX12ApiWallTiming'):
            continue
        
        raw = parts[2]
        try:
            data = json.loads(raw)
        except:
            if raw.startswith('"') and not raw.startswith('{'):
                raw = '{' + raw + '}'
            try:
                data = json.loads(raw)
            except:
                continue
        
        # Check relevant keys
        event = data.get('event', '')
        stage = data.get('stage', '')
        
        text = str(data)
        interesting = any(kw in text.lower() for kw in ['miss', 'fail', 'blocked', 'not found'])
        if interesting:
            print(line[:400])
