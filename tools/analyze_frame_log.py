#!/usr/bin/env python3
"""
Frame Analysis Tool for 3DMigoto DX12 log (d3d12_log.jsonl).
Finds the longest frame and generates a detailed timing breakdown report.
"""
import json
import sys
import os
from collections import defaultdict

def parse_log(filepath):
    """Parse the tab-separated log file. Returns list of (timestamp, category, data_dict)."""
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
                category = parts[1]
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
                entries.append((ts, category, data))
            except (ValueError, json.JSONDecodeError):
                continue
    return entries

def find_longest_frame(entries, min_draws=0):
    """Find the frame with the longest frameWallUs, optionally filtering by min draw calls."""
    frames = {}
    profiling = {}
    for ts, cat, data in entries:
        if cat == 'DX12FrameTimeline':
            present = data.get('present')
            frame_wall_us = data.get('frameWallUs', 0)
            if present is not None:
                frames[present] = {'present': present, 'frameWallUs': frame_wall_us, 'ts': ts, **data}
        elif cat == 'DX12ProfilingFrame':
            present = data.get('present')
            if present is not None:
                profiling[present] = data

    if not frames:
        return None

    # Filter by min draw calls if requested
    candidates = []
    for present, ft_data in frames.items():
        pf = profiling.get(present, {})
        draw_total = pf.get('drawTotal', 0)
        dispatch = pf.get('dispatch', 0)
        if draw_total + dispatch >= min_draws:
            candidates.append((present, ft_data['frameWallUs'], draw_total + dispatch))

    if not candidates:
        # Fallback to all frames
        candidates = [(p, d['frameWallUs'], 0) for p, d in frames.items()]

    candidates.sort(key=lambda x: x[1], reverse=True)
    best = candidates[0]
    return frames[best[0]], profiling.get(best[0], {}), best[2]

def collect_frame_data(entries, present_num):
    """Collect all timing data for a specific frame (by present number)."""
    frame_timeline = None
    profiling_frame = None
    cpu_stage_timings = []
    api_wall_timings = []
    log_costs = []
    cpu_timeline = None
    perf_scopes = []

    for ts, cat, data in entries:
        pn = data.get('present')
        if pn != present_num:
            continue

        if cat == 'DX12FrameTimeline':
            frame_timeline = data
        elif cat == 'DX12ProfilingFrame':
            profiling_frame = data
        elif cat == 'DX12CpuStageTiming':
            cpu_stage_timings.append(data)
        elif cat == 'DX12ApiWallTiming':
            api_wall_timings.append(data)
        elif cat == 'DX12LogCost':
            log_costs.append(data)
        elif cat == 'DX12CpuTimelineCoverage':
            cpu_timeline = data
        elif cat == 'DX12Perf':
            perf_scopes.append(data)

    return {
        'frame_timeline': frame_timeline,
        'profiling_frame': profiling_frame,
        'cpu_stage_timings': cpu_stage_timings,
        'api_wall_timings': api_wall_timings,
        'log_costs': log_costs,
        'cpu_timeline': cpu_timeline,
        'perf_scopes': perf_scopes,
    }

def format_us(us):
    """Format microseconds to human readable."""
    if us >= 1_000_000:
        return f"{us/1_000_000:.2f} s"
    elif us >= 1_000:
        return f"{us/1_000:.2f} ms"
    else:
        return f"{us:.0f} us"

def get_prev_worker_wait(entries, present_num):
    """Get worker wait from previous frame to calculate delta."""
    for ts, cat, data in entries:
        if cat == 'DX12FrameTimeline' and data.get('present') == present_num - 1:
            return data.get('workerIdleWaitUs', 0), data.get('workerWaitUs', 0)
    return 0, 0

def generate_report(filepath):
    print("=" * 100)
    print("  3DMigoto DX12 Frame Analysis Report")
    print("=" * 100)
    print("  Log file: {}".format(filepath))
    if os.path.exists(filepath):
        print("  File size: {:,} bytes".format(os.path.getsize(filepath)))
    print()

    print("[1/4] Parsing log entries...")
    entries = parse_log(filepath)
    print("  Parsed {:,} entries".format(len(entries)))

    # Find longest frame WITH draw calls
    print("[2/4] Finding longest rendering frame (with draw/dispatch calls)...")
    ft_data, pf_data, num_draws = find_longest_frame(entries, min_draws=1)
    if not ft_data:
        print("ERROR: No frame timeline data found!")
        return

    present_num = ft_data['present']
    frame_wall_us = ft_data['frameWallUs']
    frame_interval_us = ft_data.get('frameIntervalUs', 0)
    print("  Longest rendering frame: Present #{}".format(present_num))
    print("  Frame wall time: {}".format(format_us(frame_wall_us)))
    print("  Frame interval:  {}".format(format_us(frame_interval_us)))
    print("  Draws+Dispatches: {}".format(num_draws))

    # Get previous frame's worker wait for delta calculation
    prev_worker_idle, prev_worker_wait = get_prev_worker_wait(entries, present_num)

    print("[3/4] Collecting frame timing data...")
    frame_data = collect_frame_data(entries, present_num)

    # ======================= REPORT =======================
    print()
    print("=" * 100)
    print("  FRAME ANALYSIS: Present #{} (Longest Rendering Frame)".format(present_num))
    print("  Frame Wall Time: {} ({:,} us)".format(format_us(frame_wall_us), frame_wall_us))
    print("  Frame Interval:  {} ({:,} us)".format(format_us(frame_interval_us), frame_interval_us))
    if frame_interval_us > 0:
        expected_fps = 1_000_000 / frame_wall_us
        target_fps = 1_000_000 / frame_interval_us
        print("  Expected FPS (this frame):  {:.1f}".format(expected_fps))
        print("  Target FPS (by interval):   {:.1f}".format(target_fps))
    print("=" * 100)

    ft = frame_data['frame_timeline']
    pf = frame_data['profiling_frame']
    stage_timings = frame_data['cpu_stage_timings']
    api_timings = frame_data['api_wall_timings']
    log_costs = frame_data['log_costs']
    ct = frame_data['cpu_timeline']

    # ======================= SECTION 1: Top-Level Overview =======================
    print()
    print("-" * 100)
    print("  TOP-LEVEL FRAME OVERVIEW")
    print("-" * 100)

    if ft:
        present_wall = ft.get('presentWallUs', 0)
        outside_present = ft.get('outsidePresentUs', 0)
        worker_task_us = ft.get('workerTaskUs', 0)
        worker_idle_raw = ft.get('workerIdleWaitUs', 0)
        worker_drain_raw = ft.get('workerDrainWaitUs', 0)
        worker_wait_raw = ft.get('workerWaitUs', 0)
        worker_task_calls = ft.get('workerTaskCalls', 0)

        # Calculate per-frame deltas for worker metrics
        worker_idle_delta = worker_idle_raw - prev_worker_idle
        worker_wait_delta = worker_wait_raw - prev_worker_wait
        if worker_idle_delta < 0:
            worker_idle_delta = worker_idle_raw  # wrap-around or first frame
        if worker_wait_delta < 0:
            worker_wait_delta = worker_wait_raw

        api_total = ct.get('apiScopeTotalUs', 0) if ct else 0
        api_max_gap = ct.get('apiScopeMaxGapUs', 0) if ct else 0

        print("  {:<40s} {:>14s}  {:>12s}".format("Category", "Time", "% of Frame"))
        print("  {:<40s} {:>14s}  {:>12s}".format("-"*40, "-"*14, "-"*12))

        items = [
            ("Inside Present() call", present_wall),
            ("Outside Present() (game logic)", outside_present),
            ("Our API Hook Total (scoped)", api_total),
            ("Max Gap Between API Calls", api_max_gap),
            ("Worker Task Processing", worker_task_us),
            ("Worker Idle (per-frame delta)", worker_idle_delta),
            ("Worker Drain Wait", worker_drain_raw),
            ("Frame Minus API Total", ct.get('frameMinusApiTotalUs', 0) if ct else 0),
        ]
        for name, val in items:
            pct = (val / frame_wall_us * 100) if frame_wall_us > 0 else 0
            bar = '|' + '#' * min(int(pct), 80)
            print("  {:<40s} {:>14s}  {:>11.2f}% {}".format(name, format_us(val), pct, bar))
        print()

    # ======================= SECTION 2: Present Breakdown =======================
    if ft:
        print("-" * 100)
        print("  PRESENT CALL BREAKDOWN (inside our Present hook)")
        print("-" * 100)
        present_wall = ft.get('presentWallUs', 0)
        present_slices = [
            ('Original (game Present)', ft.get('presentOriginalUs', 0)),
            ('Profiling Flush', ft.get('presentProfilingFlushUs', 0)),
            ('ModBeginFrame', ft.get('presentModBeginFrameUs', 0)),
            ('Post-Original Runtime', ft.get('presentPostOriginalRuntimeUs', 0)),
            ('HotPath Update', ft.get('presentHotPathUpdateUs', 0)),
            ('Capture Bookkeeping', ft.get('presentCaptureBookkeepingUs', 0)),
            ('Consume UI Actions', ft.get('presentConsumeUiActionsUs', 0)),
            ('Per-Frame Counters', ft.get('presentPerFrameCountersUs', 0)),
            ('Other/Unaccounted', ft.get('presentOtherUs', 0) + ft.get('presentWallMinusKnownSlicesUs', 0)),
        ]
        present_slices_sorted = sorted(present_slices, key=lambda x: x[1], reverse=True)
        print("  {:<40s} {:>12s}  {:>10s}  {:>10s}".format("Present Slice", "Time", "%Present", "%Frame"))
        print("  {:<40s} {:>12s}  {:>10s}  {:>10s}".format("-"*40, "-"*12, "-"*10, "-"*10))
        for name, us_val in present_slices_sorted:
            if us_val > 0:
                pct_p = (us_val / present_wall * 100) if present_wall > 0 else 0
                pct_f = (us_val / frame_wall_us * 100) if frame_wall_us > 0 else 0
                print("  {:<40s} {:>12s}  {:>9.1f}%  {:>9.2f}%".format(
                    name, format_us(us_val), pct_p, pct_f))
        print("  {:<40s} {:>12s}".format("Present Wall Total", format_us(present_wall)))
        print()

    # ======================= SECTION 3: Queue Breakdown =======================
    if ft:
        print("-" * 100)
        print("  QUEUE EXECUTE BREAKDOWN")
        print("-" * 100)
        queue_total = ft.get('queueWrapperTotalUs', 0)
        queue_slices = [
            ('Original ExecuteCommandLists', ft.get('queueOriginalUs', 0)),
            ('Notify (post-execute hooks)', ft.get('queueNotifyUs', 0)),
            ('Register (cmd list tracking)', ft.get('queueRegisterUs', 0)),
            ('Signal Fence', ft.get('queueSignalUs', 0)),
            ('Wait Fence', ft.get('queueWaitUs', 0)),
            ('Unwrap (wrapper->real)', ft.get('queueUnwrapUs', 0)),
            ('RebuildWorkFlags', ft.get('queueRebuildWorkFlagsUs', 0)),
            ('Other', ft.get('queueOtherUs', 0)),
        ]
        queue_slices_sorted = sorted(queue_slices, key=lambda x: x[1], reverse=True)
        print("  {:<40s} {:>12s}  {:>10s}  {:>10s}".format("Queue Slice", "Time", "%Queue", "%Frame"))
        print("  {:<40s} {:>12s}  {:>10s}  {:>10s}".format("-"*40, "-"*12, "-"*10, "-"*10))
        for name, us_val in queue_slices_sorted:
            if us_val > 0:
                pct_q = (us_val / queue_total * 100) if queue_total > 0 else 0
                pct_f = (us_val / frame_wall_us * 100) if frame_wall_us > 0 else 0
                print("  {:<40s} {:>12s}  {:>9.1f}%  {:>9.2f}%".format(
                    name, format_us(us_val), pct_q, pct_f))
        print("  {:<40s} {:>12s}".format("Queue Wrapper Total", format_us(queue_total)))
        print("  Execute Calls: {}  |  Command Lists: {}  |  Stage Samples: {}".format(
            ft.get('queueExecuteWallCalls', 0),
            ft.get('queueExecuteWallCommandLists', 0),
            ft.get('queueExecuteStageSamples', 0)))
        print()

    # ======================= SECTION 4: Worker Thread =======================
    if ft:
        print("-" * 100)
        print("  WORKER THREAD ANALYSIS")
        print("-" * 100)
        worker_task_us_ft = ft.get('workerTaskUs', 0)
        worker_idle_raw = ft.get('workerIdleWaitUs', 0)
        worker_drain_raw = ft.get('workerDrainWaitUs', 0)
        worker_wait_raw = ft.get('workerWaitUs', 0)
        worker_task_calls = ft.get('workerTaskCalls', 0)
        worker_wait_calls = ft.get('workerWaitCalls', 0)
        worker_idle_delta = worker_idle_raw - prev_worker_idle
        if worker_idle_delta < 0:
            worker_idle_delta = worker_idle_raw

        print("  Note: Worker metrics are CUMULATIVE counters. Per-frame deltas shown where possible.")
        print()
        print("  {:<40s} {:>14s}".format("Metric", "Value"))
        print("  {:<40s} {:>14s}".format("-"*40, "-"*14))
        print("  {:<40s} {:>14s}".format("Worker Task Time", format_us(worker_task_us_ft)))
        print("  {:<40s} {:>14d}".format("Worker Task Calls", worker_task_calls))
        print("  {:<40s} {:>14s}".format("Worker Idle (raw cumulative)", format_us(worker_idle_raw)))
        print("  {:<40s} {:>14s}".format("Worker Idle (per-frame delta)", format_us(worker_idle_delta)))
        print("  {:<40s} {:>14s}".format("Worker Drain Wait", format_us(worker_drain_raw)))
        print("  {:<40s} {:>14s}  ** GPU STARVATION INDICATOR **".format(
            "Worker Idle %Frame", "{:.1f}%".format(worker_idle_delta / frame_wall_us * 100) if frame_wall_us > 0 else "N/A"))
        print()

    # ======================= SECTION 5: CPU Stage Timings =======================
    if stage_timings:
        print("-" * 100)
        print("  CPU STAGE TIMING (our mod's internal stages)")
        print("-" * 100)

        stage_agg = defaultdict(lambda: {'totalUs': 0, 'calls': 0, 'items': 0, 'maxUs': 0})
        for s in stage_timings:
            key = "{}::{}".format(s.get('category', '?'), s.get('stage', '?'))
            stage_agg[key]['totalUs'] += s.get('totalUs', 0)
            stage_agg[key]['calls'] += s.get('calls', 0)
            stage_agg[key]['items'] += s.get('items', 0)
            stage_agg[key]['maxUs'] = max(stage_agg[key]['maxUs'], s.get('maxUs', 0))

        sorted_stages = sorted(stage_agg.items(), key=lambda x: x[1]['totalUs'], reverse=True)
        print("  {:<55s} {:>12s}  {:>6s}  {:>7s}  {:>10s}  {:>8s}".format(
            "Category::Stage", "Total", "Calls", "Items", "Max", "%Frame"))
        print("  {:<55s} {:>12s}  {:>6s}  {:>7s}  {:>10s}  {:>8s}".format(
            "-"*55, "-"*12, "-"*6, "-"*7, "-"*10, "-"*8))
        for key, agg in sorted_stages:
            if agg['totalUs'] > 10:  # Filter noise
                pct = (agg['totalUs'] / frame_wall_us * 100) if frame_wall_us > 0 else 0
                bar = '#' * int(pct * 4) if pct > 0.25 else ''
                print("  {:<55s} {:>12s}  {:>6d}  {:>7d}  {:>10s}  {:>7.2f}% {}".format(
                    key, format_us(agg['totalUs']), agg['calls'], agg['items'],
                    format_us(agg['maxUs']), pct, bar))
        print()

    # ======================= SECTION 6: API Wall Timings =======================
    if api_timings:
        print("-" * 100)
        print("  API WALL TIMING (time spent inside each D3D12 API hook)")
        print("-" * 100)

        api_agg = defaultdict(lambda: {'totalUs': 0, 'calls': 0, 'maxUs': 0})
        for a in api_timings:
            bucket = a.get('bucket', '?')
            api_agg[bucket]['totalUs'] += a.get('totalUs', 0)
            api_agg[bucket]['calls'] += a.get('calls', 0)
            api_agg[bucket]['maxUs'] = max(api_agg[bucket]['maxUs'], a.get('maxUs', 0))

        sorted_apis = sorted(api_agg.items(), key=lambda x: x[1]['totalUs'], reverse=True)
        print("  {:<40s} {:>12s}  {:>6s}  {:>10s}  {:>8s}  {:>10s}".format(
            "API Bucket", "Total", "Calls", "Max", "%Frame", "Avg/Call"))
        print("  {:<40s} {:>12s}  {:>6s}  {:>10s}  {:>8s}  {:>10s}".format(
            "-"*40, "-"*12, "-"*6, "-"*10, "-"*8, "-"*10))
        for key, agg in sorted_apis:
            if agg['totalUs'] > 10:
                pct = (agg['totalUs'] / frame_wall_us * 100) if frame_wall_us > 0 else 0
                if agg['calls'] > 0:
                    avg = agg['totalUs'] / agg['calls']
                else:
                    avg = 0
                bar = '#' * int(pct * 4) if pct > 0.25 else ''
                print("  {:<40s} {:>12s}  {:>6d}  {:>10s}  {:>7.2f}%  {:>9.1f}us {}".format(
                    key, format_us(agg['totalUs']), agg['calls'],
                    format_us(agg['maxUs']), pct, avg, bar))
        print()

    # ======================= SECTION 7: Logging Cost =======================
    if log_costs:
        print("-" * 100)
        print("  LOGGING COST")
        print("-" * 100)
        for lc in log_costs:
            log_total = lc.get('totalUs', 0)
            format_us_v = lc.get('formatUs', 0)
            enqueue_us = lc.get('enqueueUs', 0)
            pct = (log_total / frame_wall_us * 100) if frame_wall_us > 0 else 0
            print("  Total:     {} ({:.2f}% of frame)".format(format_us(log_total), pct))
            print("  Format:    {}  |  Enqueue: {}".format(format_us(format_us_v), format_us(enqueue_us)))
            print("  JSON: {}  |  Text: {}  |  Dropped: {}".format(
                lc.get('jsonCalls', 0), lc.get('textCalls', 0), lc.get('droppedLines', 0)))
        print()

    # ======================= SECTION 8: CPU Coverage =======================
    if ct:
        print("-" * 100)
        print("  CPU TIMELINE COVERAGE (how much of the frame is observed)")
        print("-" * 100)
        print("  API Scoped Calls:     {}".format(ct.get('apiScopeCalls', 0)))
        print("  API Scope Total:      {} (sum of all API call durations)".format(format_us(ct.get('apiScopeTotalUs', 0))))
        print("  API Scope Span:       {} (first API to last API)".format(format_us(ct.get('apiScopeSpanUs', 0))))
        print("  API Max Gap:          {} (largest gap between consecutive API calls)".format(format_us(ct.get('apiScopeMaxGapUs', 0))))
        print("  Frame - API Total:    {} (time NOT in API hooks)".format(format_us(ct.get('frameMinusApiTotalUs', 0))))
        print("  API Total %Frame:     {:.2f}%".format(ct.get('apiTotalPctX100', 0) / 100.0))
        print("  API Span %Frame:      {:.2f}%".format(ct.get('apiSpanPctX100', 0) / 100.0))
        print()

    # ======================= SECTION 9: Profiling Summary =======================
    if pf:
        print("-" * 100)
        print("  PROFILING FRAME SUMMARY")
        print("-" * 100)
        items_pf = [
            ("Draws (Instanced+Indexed)", pf.get('drawTotal', 0)),
            ("Dispatches", pf.get('dispatch', 0)),
            ("SetPipelineState", pf.get('setPipelineState', 0)),
            ("SetRootSignatures", pf.get('setRootSignatures', 0)),
            ("SetRootDescriptorTables", pf.get('setRootDescriptorTables', 0)),
            ("IA SetVertexBuffers", pf.get('iaSetVertexBuffers', 0)),
            ("IA SetIndexBuffer", pf.get('iaSetIndexBuffer', 0)),
            ("IA SetPrimitiveTopology", pf.get('iaSetPrimitiveTopology', 0)),
            ("Bindings Total", pf.get('bindingsTotal', 0)),
            ("Resource Barriers", pf.get('resourceBarrier', 0)),
            ("ExecuteCommandLists", pf.get('executeCommandLists', 0)),
            ("Commands Total", pf.get('commandsTotal', 0)),
            ("Fast-Forward Hits", pf.get('fastForwardHits', 0)),
            ("CPU Total (bindings)", pf.get('cpuUs_total', 0)),
        ]
        for name, val in items_pf:
            if isinstance(val, float):
                print("  {:<30s} {:>10.1f}".format(name, val))
            else:
                print("  {:<30s} {:>10d}".format(name, val))
        print()

    # ======================= SECTION 10: GRAND SUMMARY =======================
    print("=" * 100)
    print("  GRAND SUMMARY - All Costs Sorted ({} frame, {} wall)".format(
        "rendering" if num_draws > 0 else "loading", format_us(frame_wall_us)))
    print("=" * 100)

    all_costs = []

    # Add the big categories
    if ct:
        all_costs.append(("Game CPU (outside API hooks, including app logic, physics, etc.)",
                          ct.get('frameMinusApiTotalUs', 0)))

    if ft:
        all_costs.append(("Worker Idle Wait (per-frame delta)", worker_idle_delta))

    # Our API total
    if ct:
        all_costs.append(("Our API Hook Overhead (apiScopeTotalUs)", ct.get('apiScopeTotalUs', 0)))

    # CPU Stages (aggregated by category)
    if stage_timings:
        stage_by_cat = defaultdict(int)
        for s in stage_timings:
            cat = s.get('category', '?')
            stage_by_cat[cat] += s.get('totalUs', 0)
        for cat, total in sorted(stage_by_cat.items(), key=lambda x: x[1], reverse=True):
            if total > 10:
                all_costs.append(("Mod Stage: {}".format(cat), total))

    # Top API calls (aggregated)
    if api_timings:
        api_by_bucket = defaultdict(int)
        for a in api_timings:
            api_by_bucket[a.get('bucket', '?')] += a.get('totalUs', 0)
        # Only top 10
        top_apis = sorted(api_by_bucket.items(), key=lambda x: x[1], reverse=True)[:10]
        for bucket, total in top_apis:
            if total > 10:
                all_costs.append(("API Hook: {}".format(bucket), total))

    # Log cost
    if log_costs:
        for lc in log_costs:
            all_costs.append(("Logging Total", lc.get('totalUs', 0)))

    # Filter and sort
    all_costs = [(n, v) for n, v in all_costs if v > 0]
    all_costs.sort(key=lambda x: x[1], reverse=True)

    print("  {:<5s} {:<60s} {:>14s}  {:>12s}".format("Rank", "Category", "Time", "% of Frame"))
    print("  {:<5s} {:<60s} {:>14s}  {:>12s}".format("-"*5, "-"*60, "-"*14, "-"*12))
    for i, (name, us_val) in enumerate(all_costs, 1):
        pct = (us_val / frame_wall_us * 100) if frame_wall_us > 0 else 0
        bar = '#' * min(int(pct), 100) if pct > 0.5 else ''
        print("  {:<5d} {:<60s} {:>14s}  {:>11.2f}%  {}".format(i, name, format_us(us_val), pct, bar))

    print()
    print("=" * 100)

    # ======================= ANALYSIS & RECOMMENDATIONS =======================
    print()
    print("=" * 100)
    print("  ARCHITECTURAL ANALYSIS & RECOMMENDATIONS")
    print("=" * 100)

    if ct:
        api_total = ct.get('apiScopeTotalUs', 0)
        frame_minus_api = ct.get('frameMinusApiTotalUs', 0)
        api_max_gap = ct.get('apiScopeMaxGapUs', 0)

        print()
        print("  [1] CPU BOTTLENECK IDENTIFICATION:")
        print("  -----------------------------------")
        print("  Total frame time:             {}".format(format_us(frame_wall_us)))
        print("  Time in D3D12 API hooks:      {} ({:.2f}%)".format(format_us(api_total), api_total/frame_wall_us*100 if frame_wall_us > 0 else 0))
        print("  Time OUTSIDE D3D12 API hooks: {} ({:.2f}%)".format(format_us(frame_minus_api), frame_minus_api/frame_wall_us*100 if frame_wall_us > 0 else 0))
        print("  Max gap between API calls:    {}".format(format_us(api_max_gap)))
        print()
        if frame_minus_api > api_total * 10:
            print("  >>> CRITICAL: The MAJORITY of frame time ({:.1f}%) is spent OUTSIDE D3D12 API")
            print("      calls. This means the game is doing heavy CPU work (physics, AI, asset")
            print("      streaming, game logic) that does NOT go through D3D12. Our mod cannot")
            print("      optimize this - it's the game engine's bottleneck.".format(
                frame_minus_api/frame_wall_us*100 if frame_wall_us > 0 else 0))
            print()
            print("      The GPU is idle during these {} gaps between API calls.".format(format_us(api_max_gap)))
        else:
            print("  >>> Our mod's API hooks are a significant portion of frame time.")

        # Calculate our mod's overhead
        our_total = 0
        if stage_timings:
            for s in stage_timings:
                our_total += s.get('totalUs', 0)
        if log_costs:
            for lc in log_costs:
                our_total += lc.get('totalUs', 0)

        if our_total > 0:
            print()
            print("  [2] OUR MOD'S OVERHEAD:")
            print("  -----------------------------------")
            print("  Mod CPU stages total:  {}".format(format_us(our_total)))
            print("  As % of frame:         {:.2f}%".format(our_total/frame_wall_us*100 if frame_wall_us > 0 else 0))
            print("  As % of API hook time: {:.2f}%".format(our_total/api_total*100 if api_total > 0 else 0))
            print()

            if stage_timings:
                stage_by_cat = defaultdict(int)
                for s in stage_timings:
                    stage_by_cat[s.get('category', '?')] += s.get('totalUs', 0)
                sorted_cats = sorted(stage_by_cat.items(), key=lambda x: x[1], reverse=True)
                print("  Per-subsystem breakdown:")
                for cat, total in sorted_cats:
                    if total > 10:
                        print("    {:<40s} {} ({:.2f}% of mod overhead)".format(
                            cat, format_us(total), total/our_total*100 if our_total > 0 else 0))

        # Recommendations
        print()
        print("  [3] KEY OPTIMIZATION TARGETS (in priority order):")
        print("  -----------------------------------")

        if frame_minus_api > frame_wall_us * 0.5:
            print("  #1 (GAME-SIDE): The game spends {:.1f}% of frame time OUTSIDE D3D12.")
            print("      This is the PRIMARY cause of GPU starvation. GPU is idle while")
            print("      the CPU processes game logic, physics, etc.")
            print("      - This is a GAME ENGINE limitation, not something we can fix in the mod.")
            print("      - Consider reducing game graphics settings to lower CPU load.")
            if num_draws == 0:
                print("      - NOTE: This frame had 0 draw calls - it may be a loading/streaming frame.")
                print("        The 'longest frame' may be an outlier. Consider analyzing average frames.")
            print()

        print("  MOD-SIDE OPTIMIZATIONS (what we CAN improve):")
        if stage_timings:
            stage_by_cat = defaultdict(int)
            for s in stage_timings:
                stage_by_cat[s.get('category', '?')] += s.get('totalUs', 0)
            sorted_cats = sorted(stage_by_cat.items(), key=lambda x: x[1], reverse=True)

            rank = 2
            for cat, total in sorted_cats:
                if total > 1000:  # Only mention > 1ms
                    pct = total/our_total*100 if our_total > 0 else 0
                    print("  #{} {} ({}, {:.1f}% of mod overhead)".format(rank, cat, format_us(total), pct))

                    if 'Descriptor' in cat:
                        print("     - Descriptor collection pipeline: consider batching, lock-free queues,")
                        print("       or reducing the scope of descriptor tracking.")
                    elif 'IaReplacement' in cat:
                        print("     - IA replacement: optimize hash computation, increase cache hit rate,")
                        print("       or use faster hashing algorithm.")
                    elif 'ResourceCollect' in cat:
                        print("     - Resource collection: consider reducing tracking granularity")
                        print("       or implementing a more efficient data structure.")
                    rank += 1

        if log_costs:
            for lc in log_costs:
                log_total = lc.get('totalUs', 0)
                if log_total > 1000:
                    print("  #{} Logging Cost: {} ({:.1f}% of mod overhead)".format(
                        rank, format_us(log_total), log_total/our_total*100 if our_total > 0 else 0))
                    print("     - Consider async logging or reducing log verbosity for release builds.")
                    rank += 1

    print()
    print("=" * 100)
    print("  END OF REPORT")
    print("=" * 100)

    return {
        'longest_frame': present_num,
        'frame_wall_us': frame_wall_us,
        'frame_interval_us': frame_interval_us,
        'frame_data': frame_data,
        'num_draws': num_draws,
    }


if __name__ == '__main__':
    if len(sys.argv) > 1:
        filepath = sys.argv[1]
    else:
        filepath = r'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl'

    if not os.path.exists(filepath):
        print("ERROR: Log file not found: {}".format(filepath))
        sys.exit(1)

    generate_report(filepath)
