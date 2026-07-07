#!/usr/bin/env python3
"""Quick frame comparison: extract hotpath data for specific present IDs."""
import sys, json, os

log_path = r"D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl"
targets = {1622, 1623}  # most recent heavy render frames

hotpath = {}  # present -> {stage -> data}
profiling = {}
logcost = {}
resident = {}

size = os.path.getsize(log_path)
with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
    f.seek(max(0, size - 10000000))  # last 10MB
    for line in f:
        parts = line.strip().split('\t', 2)
        if len(parts) < 3:
            continue
        _, func, data = parts
        try:
            d = json.loads('{' + data + '}')
        except:
            continue
        
        pid = d.get('present', 0)
        if pid not in targets:
            continue
        
        if func == 'DX12HotPathTiming':
            hp = hotpath.setdefault(pid, {})
            stage = d.get('stage', '')
            hp[stage] = d
        elif func == 'DX12ProfilingFrame':
            if d.get('present', 0) > profiling.get('present', 0) or d['present'] in targets:
                profiling[d['present']] = d
        elif func == 'DX12LogCost':
            logcost[d['present']] = d

for pid in sorted(targets):
    pf = profiling.get(pid)
    hp = hotpath.get(pid, {})
    lc = logcost.get(pid, {})
    
    if not pf:
        print(f'\nPresent #{pid}: NO DATA')
        continue
    
    frameWall = pf.get('frameWallUs', 0)
    draws = pf.get('drawTotal', 0)
    dispatch = pf.get('dispatch', 0)
    binds = pf.get('bindingsTotal', 0)
    ffHits = pf.get('fastForwardHits', 0)
    cpuTotal = pf.get('cpuUs_total', 0)
    
    print(f'\n{"="*80}')
    print(f'  FRAME #{pid}: {frameWall}us ({frameWall/1000:.1f}ms) | {1000000/max(frameWall,1):.1f} FPS')
    print(f'  Draws={draws}  Dispatch={dispatch}  Binds={binds}  FFHits={ffHits}  CPU={cpuTotal}us')
    
    if lc:
        print(f'  LogCost: fmt={lc.get("formatUs",0)}us  enq={lc.get("enqueueUs",0)}us  total={lc.get("formatUs",0)+lc.get("enqueueUs",0)}us')
    
    print(f'\n  {"Stage":35s} {"Calls":>6s} {"TotalUs":>8s} {"AvgUs":>6s} {"%Frame":>7s}')
    print(f'  {"-"*65}')
    stages = sorted(hp.items(), key=lambda x: x[1].get('totalUs', 0), reverse=True)
    for stage, d in stages:
        calls = d.get('calls', 0)
        total = d.get('totalUs', 0)
        avg = d.get('avgUs', 0)
        pct = round(total / max(frameWall, 1) * 100, 1)
        marker = '#' * max(1, int(pct))
        print(f'  {stage:35s} {calls:6d} {total:8d} {avg:6d}us {pct:7.1f}%  {marker}')

print(f'\n{"="*80}')
print('COMPARISON: Old vs New')
print(f'  {"Metric":40s} {"Before(2392)":>15s} {"After(1622)":>15s} {"Delta":>10s}')
print(f'  {"-"*80}')
# Fallback data from earlier analysis
old = {2392: {'wall': 315531, 'draws': 1690, 'fps': 3.2,
    'DrawHook': 143910, 'DrawOverride': 136852, 'ModPrepareOverride': 110295,
    'SetPipelineState': 10214, 'BuildIaHash': 9283, 'DispatchHook': 2852}}
for pid in sorted(targets):
    pf = profiling.get(pid, {})
    hp = hotpath.get(pid, {})
    wall = pf.get('frameWallUs', 0)
    if not wall: continue
    
    # Get equivalent stages
    dh = hp.get('DrawHookTotal', {}).get('totalUs', 0)
    dov = hp.get('DrawOverrideTotal', {}).get('totalUs', 0)
    dmp = hp.get('DrawModPrepareOverride', {}).get('totalUs', 0)
    sps = hp.get('SetPipelineStateTotal', {}).get('totalUs', 0)
    bih = hp.get('DrawBuildIaHash', {}).get('totalUs', 0)
    dsh = hp.get('DispatchHookTotal', {}).get('totalUs', 0)
    
    pct = lambda new, old: f'{((new-old)/old*100):+.0f}%' if old else 'N/A'
    
    print(f'  frameWall: {old[2392]["wall"]}us -> {wall}us ({pct(wall, old[2392]["wall"])})')
    for name, key in [('DrawHook', 'DrawHookTotal'), ('DrawOverride', 'DrawOverrideTotal'),
        ('ModPrepareOverride', 'DrawModPrepareOverride'), ('SetPipelineState', 'SetPipelineStateTotal'),
        ('BuildIaHash', 'DrawBuildIaHash'), ('DispatchHook', 'DispatchHookTotal')]:
        oldv = old[2392][name]
        newv = hp.get(key, {}).get('totalUs', 0)
        print(f'  {name}: {oldv}us -> {newv}us ({pct(newv, oldv)})')
