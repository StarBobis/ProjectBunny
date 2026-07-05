#!/usr/bin/env python3
"""Find frames with actual draw calls and their wall times."""
import json
import sys

log_path = sys.argv[1] if len(sys.argv) > 1 else r'/d/SSMTCacheFolder/3Dmigoto/ZZMIDX12/d3d12_log.jsonl'

# First pass: collect frameWallUs from DX12FrameTimeline
frame_walls = {}
with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        parts = line.strip().split('\t', 2)
        if len(parts) < 3:
            continue
        if parts[1] == 'DX12FrameTimeline':
            raw = parts[2]
            try:
                data = json.loads(raw)
            except:
                try:
                    data = json.loads('{' + raw + '}')
                except:
                    continue
            frame_walls[data.get('present')] = data.get('frameWallUs', 0)

# Second pass: find frames with draws
draw_frames = []
with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        parts = line.strip().split('\t', 2)
        if len(parts) < 3:
            continue
        if parts[1] == 'DX12ProfilingFrame':
            raw = parts[2]
            try:
                data = json.loads(raw)
            except:
                try:
                    data = json.loads('{' + raw + '}')
                except:
                    continue
            pn = data.get('present')
            dt = data.get('drawTotal', 0)
            disp = data.get('dispatch', 0)
            cpu = data.get('cpuUs_total', 0)
            if dt > 0 or disp > 0:
                fw = frame_walls.get(pn, 0)
                draw_frames.append((pn, fw, dt, disp, cpu))

draw_frames.sort(key=lambda x: x[1], reverse=True)
print('Top 15 longest frames WITH draw/dispatch calls:')
for i, (pn, fw, dt, disp, cpu) in enumerate(draw_frames[:15]):
    print('  #{}: present={} wallUs={} ({:.1f}ms) drawTotal={} dispatch={} cpuUs={:.0f}'.format(
        i+1, pn, fw, fw/1000.0, dt, disp, cpu))

draw_frames_by_cpu = sorted(draw_frames, key=lambda x: x[4], reverse=True)
print()
print('Top 15 frames by CPU binding time:')
for i, (pn, fw, dt, disp, cpu) in enumerate(draw_frames_by_cpu[:15]):
    print('  #{}: present={} wallUs={} ({:.1f}ms) drawTotal={} dispatch={} cpuUs={:.0f}'.format(
        i+1, pn, fw, fw/1000.0, dt, disp, cpu))

# Show frame distribution
print()
print('All frames with draws by wall time:')
for pn, fw, dt, disp, cpu in draw_frames:
    print('  present={} wallUs={} ({:.1f}ms) drawTotal={} cpuUs={:.0f}'.format(
        pn, fw, fw/1000.0, dt, cpu))
