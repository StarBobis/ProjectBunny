#!/usr/bin/env python3
"""Find rendering frames by counting Draw/Dispatch API calls per frame."""
import json
import sys

log_path = sys.argv[1] if len(sys.argv) > 1 else r'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl'

frames = {}
api_counts = {}

with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        parts = line.split('\t', 2)
        if len(parts) < 3:
            continue
        try:
            ts = int(parts[0])
            cat = parts[1]
            raw = parts[2]
            try:
                data = json.loads(raw)
            except json.JSONDecodeError:
                if raw.startswith('"') and not raw.startswith('{'):
                    raw = '{' + raw + '}'
                try:
                    data = json.loads(raw)
                except json.JSONDecodeError:
                    continue
        except (ValueError, json.JSONDecodeError):
            continue

        if cat == 'DX12FrameTimeline':
            present = data.get('present')
            if present is not None:
                frames[present] = data

        elif cat == 'DX12ApiWallTiming':
            present = data.get('present')
            bucket = data.get('bucket', '')
            calls = data.get('calls', 0)
            if present is not None and ('Draw' in bucket or 'Dispatch' in bucket):
                if present not in api_counts:
                    api_counts[present] = 0
                api_counts[present] += calls

# Find frames with draws and reasonable frame times
candidates = []
for present, ft in frames.items():
    draws = api_counts.get(present, 0)
    wall_us = ft.get('frameWallUs', 0)
    interval_us = ft.get('frameIntervalUs', 0)

    if draws >= 50 and wall_us >= 10000 and wall_us < 1000000:
        candidates.append((present, wall_us, draws, interval_us))

candidates.sort(key=lambda x: x[1], reverse=True)

print(f"Total frames: {len(frames)}")
print(f"Rendering frames (draws>=50 via API, 10ms<wall<1s): {len(candidates)}")
print()
print(f"{'Present':>8s}  {'Wall(ms)':>10s}  {'Interval(ms)':>12s}  {'Draws':>6s}  {'FPS':>7s}")
print('-' * 60)
for present, wall_us, draws, interval_us in candidates[:30]:
    fps = 1000000.0 / wall_us if wall_us > 0 else 0
    print(f"{present:>8d}  {wall_us/1000:>10.2f}  {interval_us/1000:>12.2f}  {draws:>6d}  {fps:>7.1f}")
