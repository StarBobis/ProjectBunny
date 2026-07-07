#!/usr/bin/env python3
"""Find and compare rendering frames — streams the full log."""
import json, sys

log_path = r"D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl"
targets = {174, 198, 298, 341, 498, 537, 1059, 1182, 1506, 1622, 1623}

hotpath = {}
profiling = {}

with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
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
        
        if func == 'DX12HotPathTiming':
            stage = d.get('stage', '')
            if pid not in hotpath:
                hotpath[pid] = {}
            hotpath[pid][stage] = d
        elif func == 'DX12ProfilingFrame':
            profiling[pid] = d

# Find PIDs with both profiling and hotpath data
render_pids = set(hotpath.keys()) & set(profiling.keys())

print(f"Render frames in log: {len(render_pids)}")
print(f"{'PID':>8s} {'Wall(ms)':>10s} {'Draws':>6s} {'FPS':>6s} {'DrawHook':>8s} {'ModPrep':>8s} {'IaHash':>8s}")
print("-" * 70)

results = []
for pid in sorted(render_pids):
    pf = profiling[pid]
    hp = hotpath[pid]
    wall = pf.get('frameWallUs', 0)
    draws = pf.get('drawTotal', 0)
    if draws < 50 or wall < 10000:
        continue
    
    dh = hp.get('DrawHookTotal', {}).get('totalUs', 0)
    dmp = hp.get('DrawModPrepareOverride', {}).get('totalUs', 0)
    bih = hp.get('DrawBuildIaHash', {}).get('totalUs', 0)
    fps = 1000000 / max(wall, 1)
    
    results.append((pid, wall, draws, fps, dh, dmp, bih))
    print(f"{pid:8d} {wall/1000:10.1f} {draws:6d} {fps:6.1f} {dh:8d} {dmp:8d} {bih:8d}")

# Find closest match to old frame (1690 draws, 315ms)
print("\n--- TOP 10 by draw count (closest to 1690) ---")
results.sort(key=lambda x: abs(x[2] - 1690))
for pid, wall, draws, fps, dh, dmp, bih in results[:10]:
    pct_dh = round(dh / max(wall, 1) * 100, 1)
    pct_dmp = round(dmp / max(wall, 1) * 100, 1)
    print(f"  #{pid}: {wall/1000:.0f}ms {draws}draws {fps:.1f}fps  DrawHook={dh}us({pct_dh}%) ModPrep={dmp}us({pct_dmp}%) IaHash={bih}us")

# OLD BASELINE for ref
print("\n--- OLD BASELINE (#2392) ---")
print("  315ms 1690draws 3.2fps  DrawHook=143910us(45.6%) ModPrep=110295us(35.0%) IaHash=9283us(2.9%)")
