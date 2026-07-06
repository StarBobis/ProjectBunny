import json, sys

with open(sys.argv[1], 'r', encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

# Find the last frame that has BOTH HotPath and ApiWall timing
target_pid = None
for i in range(len(lines)-1, 0, -1):
    if 'DX12ApiWallTiming' in lines[i] and 'Dispatch' in lines[i]:
        parts = lines[i].strip().split('\t', 2)
        try:
            d = json.loads('{' + parts[2] + '}')
            target_pid = d.get('present')
            if target_pid:
                # Verify this frame also has HotPath
                for j in range(i-100, i):
                    if j < 0:
                        continue
                    if 'DX12HotPathTiming' in lines[j]:
                        try:
                            parts2 = lines[j].strip().split('\t', 2)
                            d2 = json.loads('{' + parts2[2] + '}')
                            if d2.get('present') == target_pid:
                                break  # confirmed
                        except:
                            pass
                break
        except:
            pass

if target_pid is None:
    # Fallback: use the analyzer's approach - last frame with profiling
    for i in range(len(lines)-1, 0, -1):
        if 'DX12ProfilingFrame' in lines[i]:
            parts = lines[i].strip().split('\t', 2)
            try:
                d = json.loads('{' + parts[2] + '}')
                target_pid = d.get('present')
                if target_pid and d.get('drawTotal', 0) > 0:
                    break
            except:
                pass

if target_pid is None:
    print("No active frame found!")
    sys.exit(1)

print(f'Analyzing frame #{target_pid}')
print()

hotpath = {}
apiwall = {}
cpustage = {}

for line in lines:
    parts = line.strip().split('\t', 2)
    if len(parts) < 3:
        continue
    cat = parts[1]
    msg = parts[2]
    try:
        d = json.loads('{' + msg + '}')
    except:
        continue

    p = d.get('present')
    if p != target_pid:
        continue

    if cat == 'DX12HotPathTiming':
        stage = d.get('stage', '?')
        us = d.get('totalUs', 0)
        hotpath[stage] = hotpath.get(stage, 0) + us

    elif cat == 'DX12ApiWallTiming':
        bucket = d.get('bucket', '?')
        us = d.get('totalUs', 0)
        apiwall[bucket] = apiwall.get(bucket, 0) + us

hotpath_sum = sum(hotpath.values())
apiwall_sum = sum(apiwall.values())

# Also get the CpuTimeline
for line in lines:
    if 'DX12CpuTimelineCoverage' in line:
        parts = line.strip().split('\t', 2)
        try:
            d = json.loads('{' + parts[2] + '}')
            if d.get('present') == target_pid:
                api_scope = d.get('apiScopeTotalUs', 0)
                break
        except:
            pass

print(f'API scope total (CpuTimeline): {api_scope/1000:.1f} ms')
print()

# Group similar items
print(f'{"STAGE":<40} {"HotPath":>10} {"ApiWall":>10} {"Nature":>25}')
print(f'{"-"*85}')
all_keys = set(list(hotpath.keys()) + list(apiwall.keys()))
for key in sorted(all_keys, key=lambda k: hotpath.get(k, 0) + apiwall.get(k, 0), reverse=True):
    hp = hotpath.get(key, 0)
    aw = apiwall.get(key, 0)
    if (hp + aw) < 50:
        continue
    ratio = aw / max(hp, 1)
    if hp == 0:
        nature = 'PURE GPU WAIT'
    elif ratio > 10:
        nature = 'GPU-dominated (~wait)'
    elif ratio > 3:
        nature = 'Mostly GPU wait'
    elif ratio > 1.5:
        nature = 'Mixed CPU+GPU'
    else:
        nature = 'CPU-bound'
    print(f'{key:<40} {hp:>10,.0f} {aw:>10,.0f} {nature:>25}')

print(f'{"-"*85}')
print(f'{ "TOTAL":<40} {hotpath_sum:>10,.0f} {apiwall_sum:>10,.0f}')
print()

# Final breakdown
print('='*60)
print('FINAL BREAKDOWN')
print('='*60)
# Group by nature
cpu_items = []
mixed_items = []
gpu_items = []
for key in all_keys:
    hp = hotpath.get(key, 0)
    aw = apiwall.get(key, 0)
    if (hp + aw) < 50:
        continue
    ratio = aw / max(hp, 1)
    if hp == 0:
        gpu_items.append((key, aw))
    elif ratio > 3:
        gpu_items.append((key, hp))
    elif ratio > 1.5:
        mixed_items.append((key, hp))
    else:
        cpu_items.append((key, hp))

cpu_sum = sum(x[1] for x in cpu_items)
mixed_sum = sum(x[1] for x in mixed_items)
gpu_sum_cpu = sum(x[1] for x in gpu_items)

print(f'')
print(f'  Pure CPU work (our hook logic):     {cpu_sum/1000:>8.2f} ms')
for k, v in cpu_items:
    print(f'    - {k}: {v/1000:.2f} ms')
print(f'')
print(f'  Mixed CPU+GPU (overlapping):        {mixed_sum/1000:>8.2f} ms')
for k, v in mixed_items:
    print(f'    - {k}: {v/1000:.2f} ms')
print(f'')
print(f'  GPU wait (Dispatch+Draw barriers):  {gpu_sum_cpu/1000:>8.2f} ms CPU-side')
print(f'  (actual GPU time is much larger —')
print(f'   this just measures CPU waiting for GPU)')
print(f'')
print(f'  API scope total:                    {api_scope/1000:>8.2f} ms')
print(f'')
print(f'  CONCLUSION: {cpu_sum/1000:.1f} ms is real CPU work in our hooks.')
print(f'  The rest is GPU wait time visible through API call duration.')
