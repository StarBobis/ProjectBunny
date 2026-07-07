#!/usr/bin/env python3
import json, os

log = r'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl'
renders = []
with open(log, 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        if 'DX12ProfilingFrame' not in line:
            continue
        parts = line.strip().split('\t', 2)
        if len(parts) < 3:
            continue
        try:
            d = json.loads('{' + parts[2] + '}')
        except:
            continue
        pid = d.get('present', 0)
        draws = d.get('drawTotal', 0)
        wall = d.get('frameWallUs', 0)
        if draws >= 50 and wall > 10000:
            renders.append((pid, draws, wall, d.get('fastForwardHits', 0), 
                           d.get('cpuUs_total', 0), d.get('bindingsTotal', 0)))

print(f'Total render frames: {len(renders)}')

# Closest to 300 draws
renders.sort(key=lambda x: abs(x[1] - 300))
print('\n--- 300-draw frames (old baseline comparable) ---')
print(f'  {"PID":>6s}  {"Wall(ms)":>9s}  {"Draws":>6s}  {"us/draw":>8s}  {"FFHits":>7s}  {"CPUus":>7s}')
for pid, draws, wall, ff, cpu, binds in renders[:10]:
    print(f'  {pid:6d}  {wall/1000:9.1f}  {draws:6d}  {wall/max(draws,1):8.0f}  {ff:7d}  {cpu:7.0f}')

# Heavy frames
print('\n--- Heaviest frames ---')
renders.sort(key=lambda x: -x[1])
for pid, draws, wall, ff, cpu, binds in renders[:10]:
    print(f'  {pid:6d}  {wall/1000:9.1f}  {draws:6d}  {wall/max(draws,1):8.0f}  {ff:7d}  {cpu:7.0f}')
