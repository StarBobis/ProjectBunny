#!/usr/bin/env python3
"""
Analyze the actual API call timeline for a specific frame to understand
where the 'frame minus API' time really goes.
"""
import json
import sys

log_path = sys.argv[1] if len(sys.argv) > 1 else r'/d/SSMTCacheFolder/3Dmigoto/ZZMIDX12/d3d12_log.jsonl'
target_present = int(sys.argv[2]) if len(sys.argv) > 2 else 1741

# Collect all API-scoped events for this frame in timestamp order
# We need to find the Begin/End pairs of API scope probes
timeline_events = []

with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        parts = line.strip().split('\t', 2)
        if len(parts) < 3:
            continue
        try:
            ts = int(parts[0])
        except:
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
        if pn != target_present:
            continue

        if cat == 'DX12ApiWallTiming':
            timeline_events.append(('API', ts, data))
        elif cat == 'DX12CpuStageTiming':
            timeline_events.append(('STAGE', ts, data))
        elif cat == 'DX12FrameTimeline':
            timeline_events.append(('FRAME', ts, data))
        elif cat == 'DX12CpuTimelineCoverage':
            timeline_events.append(('COVERAGE', ts, data))
        elif cat == 'DX12ProfilingFrame':
            timeline_events.append(('PROFILE', ts, data))

# Sort by timestamp
timeline_events.sort(key=lambda x: x[1])

# Print coverage data
print('=' * 90)
print('FRAME #{} TIMELINE ANALYSIS'.format(target_present))
print('=' * 90)

for kind, ts, data in timeline_events:
    if kind == 'COVERAGE':
        print()
        print('--- CPU Timeline Coverage ---')
        print('  apiScopeCalls:       {}'.format(data.get('apiScopeCalls', 0)))
        print('  apiScopeNestedCalls: {}'.format(data.get('apiScopeNestedCalls', 0)))
        print('  apiScopeTotalUs:     {:.2f} ms'.format(data.get('apiScopeTotalUs', 0) / 1000.0))
        print('  apiScopeSpanUs:      {:.2f} ms'.format(data.get('apiScopeSpanUs', 0) / 1000.0))
        print('  apiScopeMaxGapUs:    {:.2f} ms'.format(data.get('apiScopeMaxGapUs', 0) / 1000.0))
        print('  apiBeforeFirstScopeUs: {:.2f} us'.format(data.get('apiBeforeFirstScopeUs', 0)))
        print('  apiAfterLastScopeUs:   {:.2f} us'.format(data.get('apiAfterLastScopeUs', 0)))
        print('  apiFirstBeginToPresentUs: {:.2f} ms'.format(data.get('apiFirstBeginToPresentUs', 0) / 1000.0))
        print('  apiLastEndToPresentUs:    {:.2f} us'.format(data.get('apiLastEndToPresentUs', 0)))
        print('  frameWallUs:          {:.2f} ms'.format(data.get('frameWallUs', 0) / 1000.0))
        print('  frameMinusApiTotalUs: {:.2f} ms'.format(data.get('frameMinusApiTotalUs', 0) / 1000.0))
        print('  frameMinusApiSpanUs:  {:.2f} ms'.format(data.get('frameMinusApiSpanUs', 0) / 1000.0))
        print('  apiTotalPctX100:      {:.2f}%'.format(data.get('apiTotalPctX100', 0) / 100.0))
        print('  apiSpanPctX100:       {:.2f}%'.format(data.get('apiSpanPctX100', 0) / 100.0))

        # Calculate gap distribution
        api_total = data.get('apiScopeTotalUs', 0)
        api_span = data.get('apiScopeSpanUs', 0)
        api_calls = data.get('apiScopeCalls', 0)
        frame_wall = data.get('frameWallUs', 0)
        frame_minus_total = data.get('frameMinusApiTotalUs', 0)
        frame_minus_span = data.get('frameMinusApiSpanUs', 0)

        print()
        print('--- GAP ANALYSIS ---')
        if api_calls > 1:
            num_gaps = api_calls - 1  # gaps between consecutive API calls
            total_gap_time = api_span - api_total  # time between first begin to last end minus sum of durations
            avg_gap = total_gap_time / num_gaps if num_gaps > 0 else 0
            print('  Number of API calls:     {}'.format(api_calls))
            print('  Number of gaps:          {}'.format(num_gaps))
            print('  API Span (first→last):   {:.2f} ms'.format(api_span / 1000.0))
            print('  API Total (sum dur):     {:.2f} ms'.format(api_total / 1000.0))
            print('  Gap Total (span-total):  {:.2f} ms'.format(total_gap_time / 1000.0))
            print('  Avg gap between calls:   {:.1f} us'.format(avg_gap))
            print('  Max gap:                 {:.2f} ms'.format(data.get('apiScopeMaxGapUs', 0) / 1000.0))
            print('  Avg API call duration:   {:.1f} us'.format(api_total / api_calls))

        print()
        print('--- WHERE DOES THE TIME GO? ---')
        print('  frameWallUs (total frame time):     {:.2f} ms (100%)'.format(frame_wall / 1000.0))
        print()
        print('  Time INSIDE D3D12 API hooks:        {:.2f} ms ({:.1f}%)'.format(
            api_total / 1000.0, api_total / frame_wall * 100))
        print('  Time BETWEEN D3D12 API calls:       {:.2f} ms ({:.1f}%)'.format(
            total_gap_time / 1000.0, total_gap_time / frame_wall * 100))
        print('  Time BEFORE first API call:         {:.2f} ms ({:.1f}%)'.format(
            data.get('apiBeforeFirstScopeUs', 0) / 1000.0,
            data.get('apiBeforeFirstScopeUs', 0) / frame_wall * 100))
        print('  Time AFTER last API to Present:     {:.2f} ms ({:.1f}%)'.format(
            data.get('apiLastEndToPresentUs', 0) / 1000.0,
            data.get('apiLastEndToPresentUs', 0) / frame_wall * 100))
        print('  (Span gap = total gap - before - after):  {:.2f} ms'.format(
            (total_gap_time - data.get('apiBeforeFirstScopeUs', 0) - data.get('apiLastEndToPresentUs', 0)) / 1000.0))

    elif kind == 'FRAME':
        print()
        print('--- Frame Timeline ---')
        fw = data.get('frameWallUs', 0)
        fi = data.get('frameIntervalUs', 0)
        print('  frameWallUs:       {:.2f} ms'.format(fw / 1000.0))
        print('  frameIntervalUs:   {:.2f} ms'.format(fi / 1000.0))
        print('  outsidePresentUs:  {:.2f} ms'.format(data.get('outsidePresentUs', 0) / 1000.0))
        print('  presentWallUs:     {:.2f} ms'.format(data.get('presentWallUs', 0) / 1000.0))
        print('  workerTaskUs:      {:.2f} ms'.format(data.get('workerTaskUs', 0) / 1000.0))
        worker_idle = data.get('workerIdleWaitUs', 0)
        print('  workerIdleWaitUs:  {:.2f} ms (cumulative, not per-frame!)'.format(worker_idle / 1000.0))

    elif kind == 'PROFILE':
        bt = data.get('bindingsTotal', 0)
        ct = data.get('commandsTotal', 0)
        ff = data.get('fastForwardHits', 0)
        cpu = data.get('cpuUs_total', 0)
        print()
        print('--- Profiling Summary ---')
        print('  bindingsTotal:   {}'.format(bt))
        print('  commandsTotal:   {}'.format(ct))
        print('  fastForwardHits: {}'.format(ff))
        print('  cpuUs_total:     {:.1f} ms'.format(cpu / 1000.0))

print()
print('=' * 90)
print('KEY INSIGHT:')
print('  "Frame Minus API Total" is the time the game thread spends BETWEEN')
print('  D3D12 API calls - NOT "physics/AI" logic time.')
print('  It is the game rendering engine preparing draw call parameters,')
print('  culling objects, sorting draws, etc. - accumulated over ~3000 API calls.')
print('  Our mod makes this worse by adding ~80us overhead PER API call.')
print('=' * 90)
