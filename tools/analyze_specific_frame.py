#!/usr/bin/env python3
"""Analyze a specific frame by present number."""
import json
import sys
from collections import defaultdict

log_path = sys.argv[1] if len(sys.argv) > 1 else r'/d/SSMTCacheFolder/3Dmigoto/ZZMIDX12/d3d12_log.jsonl'
present_num = int(sys.argv[2]) if len(sys.argv) > 2 else 1741

# Also get previous frame for worker delta
prev_present = present_num - 1

# Collect data
frame_timeline = None
profiling_frame = None
cpu_stage_timings = []
api_wall_timings = []
log_costs = []
cpu_timeline = None
prev_worker_idle = 0

with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        parts = line.strip().split('\t', 2)
        if len(parts) < 3:
            continue
        cat = parts[1]
        raw = parts[2]
        try:
            data = json.loads(raw)
        except:
            try:
                data = json.loads('{' + raw + '}')
            except:
                continue

        pn = data.get('present')

        if cat == 'DX12FrameTimeline':
            if pn == present_num:
                frame_timeline = data
            elif pn == prev_present:
                prev_worker_idle = data.get('workerIdleWaitUs', 0)
        elif pn != present_num:
            continue

        if cat == 'DX12ProfilingFrame':
            profiling_frame = data
        elif cat == 'DX12CpuStageTiming':
            cpu_stage_timings.append(data)
        elif cat == 'DX12ApiWallTiming':
            api_wall_timings.append(data)
        elif cat == 'DX12LogCost':
            log_costs.append(data)
        elif cat == 'DX12CpuTimelineCoverage':
            cpu_timeline = data

ft = frame_timeline
pf = profiling_frame
ct = cpu_timeline

if not ft:
    print('ERROR: No frame timeline for present={}'.format(present_num))
    sys.exit(1)

frame_wall_us = ft.get('frameWallUs', 0)
frame_interval_us = ft.get('frameIntervalUs', 0)

def fmt(us):
    if us >= 1_000_000:
        return '{:.2f} s'.format(us/1_000_000)
    elif us >= 1000:
        return '{:.2f} ms'.format(us/1000)
    else:
        return '{} us'.format(int(us))

print('=' * 100)
print('  FRAME #{} ANALYSIS'.format(present_num))
print('=' * 100)
print('  Frame Wall:   {}  |  Frame Interval: {}  |  Outside Present: {}'.format(
    fmt(frame_wall_us), fmt(frame_interval_us), fmt(ft.get('outsidePresentUs', 0))))

# Top-level
print()
print('--- TOP-LEVEL ---')
present_wall = ft.get('presentWallUs', 0)
api_total = ct.get('apiScopeTotalUs', 0) if ct else 0
api_max_gap = ct.get('apiScopeMaxGapUs', 0) if ct else 0
frame_minus_api = ct.get('frameMinusApiTotalUs', 0) if ct else 0
worker_task_us = ft.get('workerTaskUs', 0)
worker_idle_delta = ft.get('workerIdleWaitUs', 0) - prev_worker_idle
if worker_idle_delta < 0:
    worker_idle_delta = ft.get('workerIdleWaitUs', 0)

print('  Present Wall:             {}'.format(fmt(present_wall)))
print('  Outside Present:          {}'.format(fmt(ft.get('outsidePresentUs', 0))))
print('  API Scope Total:          {} ({:.1f}% frame)'.format(fmt(api_total), api_total/frame_wall_us*100 if frame_wall_us > 0 else 0))
print('  API Max Gap:              {} ({:.1f}% frame)'.format(fmt(api_max_gap), api_max_gap/frame_wall_us*100 if frame_wall_us > 0 else 0))
print('  Frame - API Total:        {} ({:.1f}% frame)'.format(fmt(frame_minus_api), frame_minus_api/frame_wall_us*100 if frame_wall_us > 0 else 0))
print('  Worker Task:              {}'.format(fmt(worker_task_us)))
print('  Worker Idle (delta):      {} ({:.1f}% frame)'.format(fmt(worker_idle_delta), worker_idle_delta/frame_wall_us*100 if frame_wall_us > 0 else 0))

# API Wall
print()
print('--- API WALL TIMING ---')
api_agg = defaultdict(lambda: {'totalUs': 0, 'calls': 0})
for a in api_wall_timings:
    bucket = a.get('bucket', '?')
    api_agg[bucket]['totalUs'] += a.get('totalUs', 0)
    api_agg[bucket]['calls'] += a.get('calls', 0)
sorted_apis = sorted(api_agg.items(), key=lambda x: x[1]['totalUs'], reverse=True)
for key, agg in sorted_apis:
    if agg['totalUs'] > 0:
        pct = agg['totalUs'] / frame_wall_us * 100 if frame_wall_us > 0 else 0
        avg = agg['totalUs'] / agg['calls'] if agg['calls'] > 0 else 0
        print('  {:35s} {:>10s}  {:>5d} calls  {:>7.1f}us avg  {:>5.2f}% frame'.format(
            key, fmt(agg['totalUs']), agg['calls'], avg, pct))

# CPU Stages
print()
print('--- CPU STAGE TIMING ---')
stage_agg = defaultdict(lambda: {'totalUs': 0, 'calls': 0})
for s in cpu_stage_timings:
    key = '{}::{}'.format(s.get('category', '?'), s.get('stage', '?'))
    stage_agg[key]['totalUs'] += s.get('totalUs', 0)
    stage_agg[key]['calls'] += s.get('calls', 0)
sorted_stages = sorted(stage_agg.items(), key=lambda x: x[1]['totalUs'], reverse=True)
for key, agg in sorted_stages:
    if agg['totalUs'] > 10:
        pct = agg['totalUs'] / frame_wall_us * 100 if frame_wall_us > 0 else 0
        print('  {:55s} {:>10s}  {:>6d} calls  {:>5.2f}% frame'.format(
            key, fmt(agg['totalUs']), agg['calls'], pct))

# Log cost
print()
print('--- LOG COST ---')
for lc in log_costs:
    log_total = lc.get('totalUs', 0)
    pct = log_total / frame_wall_us * 100 if frame_wall_us > 0 else 0
    print('  Total: {} ({:.2f}% frame)  Format: {}  Enqueue: {}'.format(
        fmt(log_total), pct, fmt(lc.get('formatUs', 0)), fmt(lc.get('enqueueUs', 0))))

# Profiling summary
if pf:
    print()
    print('--- PROFILING ---')
    print('  drawTotal={}  dispatch={}  bindingsTotal={}  commandsTotal={}  ffHits={}  cpuUs_total={:.0f}'.format(
        pf.get('drawTotal',0), pf.get('dispatch',0), pf.get('bindingsTotal',0),
        pf.get('commandsTotal',0), pf.get('fastForwardHits',0), pf.get('cpuUs_total',0)))

# Quick: what API calls are happening between the big gaps?
print()
print('--- PRESENT SLICES ---')
slices = [
    ('Original', ft.get('presentOriginalUs', 0)),
    ('PostOriginal', ft.get('presentPostOriginalRuntimeUs', 0)),
    ('ModBeginFrame', ft.get('presentModBeginFrameUs', 0)),
    ('ProfilingFlush', ft.get('presentProfilingFlushUs', 0)),
    ('Other', ft.get('presentOtherUs', 0) + ft.get('presentWallMinusKnownSlicesUs', 0)),
]
for name, val in sorted(slices, key=lambda x: x[1], reverse=True):
    if val > 0:
        print('  {:25s} {}'.format(name, fmt(val)))

print()
print('--- QUEUE SLICES ---')
qslices = [
    ('Original', ft.get('queueOriginalUs', 0)),
    ('Notify', ft.get('queueNotifyUs', 0)),
    ('Register', ft.get('queueRegisterUs', 0)),
    ('Signal', ft.get('queueSignalUs', 0)),
    ('Wait', ft.get('queueWaitUs', 0)),
]
for name, val in sorted(qslices, key=lambda x: x[1], reverse=True):
    if val > 0:
        print('  {:25s} {}'.format(name, fmt(val)))
