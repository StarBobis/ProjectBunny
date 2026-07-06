#!/usr/bin/env python3
"""Analyze a specific frame by present number. Fast single-frame analysis."""
import json
import sys
import os
from collections import defaultdict

log_path = sys.argv[1] if len(sys.argv) > 1 else r'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl'
target_present = int(sys.argv[2]) if len(sys.argv) > 2 else 1367


def parse_log(filepath, present_num):
    entries = []
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
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

            pn = data.get('present')
            if pn != present_num:
                continue
            entries.append((ts, cat, data))
    return entries


def format_us(us):
    if us >= 1000000:
        return f"{us/1000000:.2f} s"
    elif us >= 1000:
        return f"{us/1000:.2f} ms"
    else:
        return f"{us:.0f} us"


entries = parse_log(log_path, target_present)
print(f"Parsed {len(entries)} entries for Present #{target_present}")

# Collect data
ft = None
pf = None
stage_timings = []
api_timings = []
log_costs = []
ct = None
perf_scopes = []

for ts, cat, data in entries:
    if cat == 'DX12FrameTimeline':
        ft = data
    elif cat == 'DX12ProfilingFrame':
        pf = data
    elif cat == 'DX12CpuStageTiming':
        stage_timings.append(data)
    elif cat == 'DX12ApiWallTiming':
        api_timings.append(data)
    elif cat == 'DX12LogCost':
        log_costs.append(data)
    elif cat == 'DX12CpuTimelineCoverage':
        ct = data
    elif cat == 'DX12Perf':
        perf_scopes.append(data)

if not ft:
    print("ERROR: No DX12FrameTimeline data for this frame!")
    sys.exit(1)

frame_wall_us = ft['frameWallUs']
frame_interval_us = ft.get('frameIntervalUs', 0)
present_wall_us = ft.get('presentWallUs', 0)

print()
print("=" * 90)
print(f"  FRAME ANALYSIS: Present #{target_present}")
print(f"  Frame Wall:    {format_us(frame_wall_us)} ({frame_wall_us:,} us)")
print(f"  Frame Interval:{format_us(frame_interval_us)} ({frame_interval_us:,} us)")
if frame_wall_us > 0:
    print(f"  Expected FPS:  {1000000.0/frame_wall_us:.1f}")
if frame_interval_us > 0:
    print(f"  Target FPS:    {1000000.0/frame_interval_us:.1f}")
print("=" * 90)

# --- TOP-LEVEL ---
print()
print("-" * 90)
print("  TOP-LEVEL OVERVIEW")
print("-" * 90)
print(f"  {'Category':<45s} {'Time':>14s}  {'%Frame':>8s}")
print(f"  {'-'*45} {'-'*14}  {'-'*8}")

outside = ft.get('outsidePresentUs', 0)
present_wall = ft.get('presentWallUs', 0)
api_total = ct.get('apiScopeTotalUs', 0) if ct else 0
api_max_gap = ct.get('apiScopeMaxGapUs', 0) if ct else 0
frame_minus_api = ct.get('frameMinusApiTotalUs', 0) if ct else 0

items = [
    ("Inside Present() call", present_wall),
    ("Outside Present() (game logic)", outside),
    ("Our API Hook Total (scoped)", api_total),
    ("Max Gap Between API Calls", api_max_gap),
    ("Frame Minus API Total", frame_minus_api),
]
for name, val in items:
    pct = val / frame_wall_us * 100 if frame_wall_us > 0 else 0
    bar = '#' * min(int(pct * 2), 80)
    print(f"  {name:<45s} {format_us(val):>14s}  {pct:>7.2f}% {bar}")

# --- PER-FUNCTION COST (sorted by total time) ---
print()
print("-" * 90)
print("  PER-FUNCTION COST BREAKDOWN (sorted by total ms, within this frame)")
print("-" * 90)

all_funcs = []

# CPU Stages
stage_agg = defaultdict(lambda: {'totalUs': 0, 'calls': 0, 'items': 0})
for s in stage_timings:
    key = f"{s.get('category','?')}::{s.get('stage','?')}"
    stage_agg[key]['totalUs'] += s.get('totalUs', 0)
    stage_agg[key]['calls'] += s.get('calls', 0)
    stage_agg[key]['items'] += s.get('items', 0)

for key, agg in stage_agg.items():
    if agg['totalUs'] > 0:
        all_funcs.append((key, agg['totalUs'], agg['calls'], 'ModStage'))

# API Wall
api_agg = defaultdict(lambda: {'totalUs': 0, 'calls': 0})
for a in api_timings:
    bucket = a.get('bucket', '?')
    api_agg[bucket]['totalUs'] += a.get('totalUs', 0)
    api_agg[bucket]['calls'] += a.get('calls', 0)

for bucket, agg in api_agg.items():
    if agg['totalUs'] > 0:
        all_funcs.append((f"API: {bucket}", agg['totalUs'], agg['calls'], 'API'))

# Log cost
for lc in log_costs:
    all_funcs.append(("Logging Total", lc.get('totalUs', 0), 0, 'Log'))

# Big categories
if ct:
    all_funcs.append(("Game CPU (outside API hooks)", frame_minus_api, 0, 'Game'))
    all_funcs.append(("Our API Hook Overhead", api_total, 0, 'Summary'))

# Sort descending
all_funcs.sort(key=lambda x: x[1], reverse=True)

print(f"  {'Rank':<5s} {'Function':<50s} {'Time':>12s}  {'%Frame':>8s}  {'Calls':>7s}")
print(f"  {'-'*5} {'-'*50} {'-'*12}  {'-'*8}  {'-'*7}")
for i, (name, us_val, calls, kind) in enumerate(all_funcs, 1):
    if us_val <= 10:
        continue
    pct = us_val / frame_wall_us * 100 if frame_wall_us > 0 else 0
    bar = '#' * min(int(pct * 5), 50) if pct > 0.2 else ''
    calls_str = str(calls) if calls > 0 else ''
    print(f"  {i:<5d} {name:<50s} {format_us(us_val):>12s}  {pct:>7.2f}%  {calls_str:>7s} {bar}")

# --- PRESENT BREAKDOWN ---
if ft:
    print()
    print("-" * 90)
    print("  PRESENT CALL BREAKDOWN")
    print("-" * 90)
    present_slices = [
        ('Original (game Present)', ft.get('presentOriginalUs', 0)),
        ('Profiling Flush', ft.get('presentProfilingFlushUs', 0)),
        ('ModBeginFrame', ft.get('presentModBeginFrameUs', 0)),
        ('Post-Original Runtime', ft.get('presentPostOriginalRuntimeUs', 0)),
        ('HotPath Update', ft.get('presentHotPathUpdateUs', 0)),
        ('Capture Bookkeeping', ft.get('presentCaptureBookkeepingUs', 0)),
        ('Consume UI Actions', ft.get('presentConsumeUiActionsUs', 0)),
        ('Per-Frame Counters', ft.get('presentPerFrameCountersUs', 0)),
        ('Other', ft.get('presentOtherUs', 0) + ft.get('presentWallMinusKnownSlicesUs', 0)),
    ]
    present_slices.sort(key=lambda x: x[1], reverse=True)
    for name, us_val in present_slices:
        if us_val > 0:
            pct_p = us_val / present_wall * 100 if present_wall > 0 else 0
            pct_f = us_val / frame_wall_us * 100 if frame_wall_us > 0 else 0
            print(f"  {name:<40s} {format_us(us_val):>12s}  {pct_p:>7.1f}% Present  {pct_f:>7.2f}% Frame")

# --- QUEUE BREAKDOWN ---
if ft:
    print()
    print("-" * 90)
    print("  QUEUE EXECUTE BREAKDOWN")
    print("-" * 90)
    queue_total = ft.get('queueWrapperTotalUs', 0)
    queue_slices = [
        ('Original ExecuteCommandLists', ft.get('queueOriginalUs', 0)),
        ('Notify (post-execute)', ft.get('queueNotifyUs', 0)),
        ('Register (cmd list tracking)', ft.get('queueRegisterUs', 0)),
        ('Signal Fence', ft.get('queueSignalUs', 0)),
        ('Wait Fence', ft.get('queueWaitUs', 0)),
        ('Unwrap', ft.get('queueUnwrapUs', 0)),
        ('RebuildWorkFlags', ft.get('queueRebuildWorkFlagsUs', 0)),
        ('Other', ft.get('queueOtherUs', 0)),
    ]
    queue_slices.sort(key=lambda x: x[1], reverse=True)
    for name, us_val in queue_slices:
        if us_val > 0 and queue_total > 0:
            pct_q = us_val / queue_total * 100
            pct_f = us_val / frame_wall_us * 100 if frame_wall_us > 0 else 0
            print(f"  {name:<40s} {format_us(us_val):>12s}  {pct_q:>7.1f}% Queue  {pct_f:>7.2f}% Frame")

# --- CPU COVERAGE ---
if ct:
    print()
    print("-" * 90)
    print("  CPU TIMELINE COVERAGE")
    print("-" * 90)
    print(f"  API Scoped Calls:     {ct.get('apiScopeCalls', 0)}")
    print(f"  API Scope Total:      {format_us(ct.get('apiScopeTotalUs', 0))}")
    print(f"  API Scope Span:       {format_us(ct.get('apiScopeSpanUs', 0))}")
    print(f"  API Max Gap:          {format_us(ct.get('apiScopeMaxGapUs', 0))}")
    print(f"  Frame - API Total:    {format_us(ct.get('frameMinusApiTotalUs', 0))}")
    print(f"  API Total %Frame:     {ct.get('apiTotalPctX100', 0)/100.0:.2f}%")

# --- GRAND SUMMARY ---
print()
print("=" * 90)
print("  GRAND SUMMARY - All Costs Sorted")
print("=" * 90)

# Merge stage costs into broader categories
descriptor_total = sum(agg['totalUs'] for key, agg in stage_agg.items() if 'DescriptorCollect' in key)
ia_total = sum(agg['totalUs'] for key, agg in stage_agg.items() if 'IaReplacement' in key or 'ia_' in key.lower())
resource_total = sum(agg['totalUs'] for key, agg in stage_agg.items() if 'ResourceCollect' in key)
log_total = sum(lc.get('totalUs', 0) for lc in log_costs)

summary = [
    ("Game CPU (outside API hooks)", frame_minus_api),
    ("DescriptorCollectPipeline (Mod)", descriptor_total),
    ("IaReplacement (Mod)", ia_total),
    ("API Hook Overhead (scoped)", api_total),
    ("ResourceCollectQueue (Mod)", resource_total),
    ("Logging", log_total),
    ("Present Hook", present_wall),
]
summary = [(n, v) for n, v in summary if v > 0]
summary.sort(key=lambda x: x[1], reverse=True)

print(f"  {'Rank':<5s} {'Category':<45s} {'Time':>14s}  {'%Frame':>8s}")
print(f"  {'-'*5} {'-'*45} {'-'*14}  {'-'*8}")
for i, (name, us_val) in enumerate(summary, 1):
    pct = us_val / frame_wall_us * 100 if frame_wall_us > 0 else 0
    bar = '#' * min(int(pct * 2), 80)
    print(f"  {i:<5d} {name:<45s} {format_us(us_val):>14s}  {pct:>7.2f}% {bar}")

print()
print("=" * 90)
print("  END OF REPORT")
print("=" * 90)
