#!/usr/bin/env python3
"""
Frame-level CPU timing analyzer for d3d12_log.jsonl.

Collects:
  - DX12ProfilingFrame  (per-frame counters + cpuUs_total)
  - DX12HotPathTiming   (hook hotspots per stage, uses current present from ProfilingFrame)
  - DX12CpuStageTiming  (fine-grained stage breakdown with present id)
  - DX12ApiWallTiming   (API wall-clock timing per bucket)
  - DX12CpuTimelineCoverage (frame wall / api scope wrap-up)
  - DX12LogCost         (log overhead)
  - DX12Perf            (misc scoped timers)

Usage:
  python tools/analyze_frame_log.py [log_path]
  Default: D:\\SSMTCacheFolder\\3Dmigoto\\ZZMIDX12\\d3d12_log.jsonl
"""

import sys
import json
import os
from collections import defaultdict

DEFAULT_LOG = r"D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl"


def parse_message_json(text):
    """Parse a log message payload into a dict. Supports both legacy JSON format
    (bare key:value with quotes) and the new key=value comma-separated format."""
    if not text:
        return {}
    text = text.strip()
    if not text:
        return {}
    # Legacy JSON format (wrapped or bare with escaped quotes)
    if text.startswith("{") or '"' in text:
        try:
            return json.loads(text) if text.startswith("{") else json.loads("{" + text + "}")
        except json.JSONDecodeError:
            pass
    # New key=value,key=value format
    return parse_key_value(text)


def parse_key_value(text):
    """Parse key=value,key=value format into a dict. Values are auto-typed."""
    result = {}
    for pair in text.split(","):
        pair = pair.strip()
        if "=" not in pair:
            continue
        key, _, value = pair.partition("=")
        key = key.strip()
        value = value.strip()
        if not key:
            continue
        # Try integer
        try:
            if value.startswith("0x") or value.startswith("0X"):
                result[key] = int(value, 16)
            else:
                result[key] = int(value)
            continue
        except ValueError:
            pass
        # Try float
        try:
            result[key] = float(value)
            continue
        except ValueError:
            pass
        # Try boolean
        if value in ("true", "True"):
            result[key] = True
        elif value in ("false", "False"):
            result[key] = False
        else:
            result[key] = value
    return result


def main():
    log_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LOG

    if not os.path.exists(log_path):
        print(f"ERROR: Log file not found: {log_path}")
        sys.exit(1)

    file_size_mb = os.path.getsize(log_path) / (1024 * 1024)
    print(f"Reading: {log_path} ({file_size_mb:.1f} MB)")

    # === Data Collectors ===
    frames = []  # list of {present, cpuUsTotal, drawTotal, dispatch, bindingsTotal, fastForward, ...}

    # HotPath: (stage) -> list of (present_id, calls, totalUs)
    hotpath = defaultdict(list)

    # CpuStage: (category, stage) -> list of (present_id, calls, totalUs)
    cpustage = defaultdict(list)

    # ApiWall: (bucket) -> list of (present_id, calls, totalUs)
    apiwall = defaultdict(list)

    # CpuTimeline: per present
    timelines = {}  # present_id -> dict

    # LogCost: per present
    logcosts = {}  # present_id -> dict

    # Perf: (scope) -> list of (present_id, us)
    perf_events = defaultdict(list)

    current_present = 0
    line_count = 0

    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line_count += 1
            line = line.strip()
            if not line:
                continue

            parts = line.split("\t", 2)
            if len(parts) < 3:
                continue

            ts_str, category, message = parts

            d = parse_message_json(message)
            if not d:
                continue

            present = d.get("present")

            if category == "DX12ProfilingFrame":
                p = present
                if p is not None:
                    current_present = p
                    frames.append({
                        "present": p,
                        "cpuUsTotal": float(d.get("cpuUs_total", 0)),
                        "drawTotal": int(d.get("drawTotal", 0)),
                        "dispatch": int(d.get("dispatch", 0)),
                        "bindingsTotal": int(d.get("bindingsTotal", 0)),
                        "fastForward": int(d.get("fastForwardHits", 0)),
                        "commandsTotal": int(d.get("commandsTotal", 0)),
                        "setDescriptorHeaps": int(d.get("setDescriptorHeaps", 0)),
                        "setRootSignatures": int(d.get("setRootSignatures", 0)),
                        "setRootDescriptorTables": int(d.get("setRootDescriptorTables", 0)),
                    })

            elif category == "DX12HotPathTiming":
                stage = d.get("stage", "?")
                calls = int(d.get("calls", 0))
                total_us = int(d.get("totalUs", 0))
                if current_present > 0:
                    hotpath[stage].append((current_present, calls, total_us))

            elif category == "DX12CpuStageTiming":
                if present is not None:
                    cat = d.get("category", "?")
                    stage = d.get("stage", "?")
                    calls = int(d.get("calls", 0))
                    total_us = int(d.get("totalUs", 0))
                    cpustage[(cat, stage)].append((present, calls, total_us))

            elif category == "DX12ApiWallTiming":
                if present is not None:
                    bucket = d.get("bucket", "?")
                    calls = int(d.get("calls", 0))
                    total_us = int(d.get("totalUs", 0))
                    apiwall[bucket].append((present, calls, total_us))

            elif category == "DX12CpuTimelineCoverage":
                if present is not None:
                    timelines[present] = {
                        "frameWallUs": int(d.get("frameWallUs", 0)),
                        "presentWallUs": int(d.get("presentWallUs", 0)),
                        "apiScopeTotalUs": int(d.get("apiScopeTotalUs", 0)),
                        "apiScopeSpanUs": int(d.get("apiScopeSpanUs", 0)),
                        "apiScopeCalls": int(d.get("apiScopeCalls", 0)),
                        "apiTotalPctX100": int(d.get("apiTotalPctX100", 0)),
                        "apiSpanPctX100": int(d.get("apiSpanPctX100", 0)),
                    }

            elif category == "DX12LogCost":
                if present is not None:
                    logcosts[present] = {
                        "totalUs": int(d.get("totalUs", 0)),
                        "formatUs": int(d.get("formatUs", 0)),
                        "enqueueUs": int(d.get("enqueueUs", 0)),
                        "jsonCalls": int(d.get("jsonCalls", 0)),
                        "textCalls": int(d.get("textCalls", 0)),
                        "debugJsonCalls": int(d.get("debugJsonCalls", 0)),
                        "enqueueCalls": int(d.get("enqueueCalls", 0)),
                        "droppedLines": int(d.get("droppedLines", 0)),
                    }

            elif category == "DX12Perf":
                scope = d.get("scope", "?")
                us_val = int(d.get("us", 0))
                p = present if present is not None else current_present
                perf_events[scope].append((p, us_val))

    if len(frames) == 0:
        print("\nNo frames found. Check log format.")
        return

    # Pick last N frames for averaging
    LAST_N = min(20, len(frames))
    last_frames = frames[-LAST_N:]
    last_present_ids = {f["present"] for f in last_frames}
    last_present = frames[-1]["present"]

    # ============================
    # FRAME OVERVIEW
    # ============================
    print("\n" + "=" * 100)
    print("  FRAME ANALYSIS REPORT")
    print("=" * 100)
    print(f"  Total frames logged: {len(frames)}")
    print(f"  Last frame present ID: #{last_present}")
    print()

    # Last frame details
    lf = frames[-1]
    tl = timelines.get(last_present, {})
    lc = logcosts.get(last_present, {})

    frame_wall = tl.get("frameWallUs", 0)
    present_wall = tl.get("presentWallUs", 0)
    api_scope_total = tl.get("apiScopeTotalUs", 0)
    api_scope_span = tl.get("apiScopeSpanUs", 0)
    api_scope_calls = tl.get("apiScopeCalls", 0)
    log_total = lc.get("totalUs", 0)
    cpu_total = lf["cpuUsTotal"]

    print(f"  {'LA ST FRAME (#%d) METRICS' % last_present:>60}")
    print(f"  {'-'*60}")
    print(f"  Frame wall time:          {frame_wall:>10,} us  ({frame_wall/1000:>8.2f} ms)")
    print(f"  Present wall time:        {present_wall:>10,} us  ({present_wall/1000:>8.2f} ms)")
    print(f"  API scope total (sum):    {api_scope_total:>10,} us  ({api_scope_total/1000:>8.2f} ms)  [{api_scope_total/max(frame_wall,1)*100:.1f}%]")
    print(f"  API scope span:           {api_scope_span:>10,} us  ({api_scope_span/1000:>8.2f} ms)  [{api_scope_span/max(frame_wall,1)*100:.1f}%]")
    print(f"  API scope calls:          {api_scope_calls:>10,}")
    print(f"  CPU us (hook logic):      {cpu_total:>10,.1f} us  ({cpu_total/1000:>8.2f} ms)  [{cpu_total/max(frame_wall,1)*100:.1f}%]")
    print(f"  Log cost (total):         {log_total:>10,} us  ({log_total/1000:>8.2f} ms)  [{log_total/max(frame_wall,1)*100:.1f}%]")
    if frame_wall > 0:
        print(f"  Expected FPS:             {1_000_000/frame_wall:>10.1f}")
    print()

    # Profiling frame counters
    print(f"  {'Profiling Counters':>60}")
    print(f"  {'-'*60}")
    print(f"  Draws (instanced):        {lf['drawTotal']:>10,}")
    print(f"  Dispatch (compute):       {lf['dispatch']:>10,}")
    print(f"  Bindings total:           {lf['bindingsTotal']:>10,}")
    print(f"  Commands total:           {lf['commandsTotal']:>10,}")
    print(f"  Fast-forward hits:        {lf['fastForward']:>10,}")
    print(f"  SetDescriptorHeaps:       {lf['setDescriptorHeaps']:>10,}")
    print(f"  SetRootSignatures:        {lf['setRootSignatures']:>10,}")
    print(f"  SetRootDescriptorTables:  {lf['setRootDescriptorTables']:>10,}")
    print()

    # ============================
    # HOT PATH TIMING (Last Frame)
    # ============================
    print("=" * 100)
    print(f"  HOT PATH TIMING (DX12HotPathTiming) - Frame #{last_present}")
    print("=" * 100)

    hp_last = []
    for stage, entries in hotpath.items():
        for (p, calls, total_us) in entries:
            if p == last_present and total_us > 0:
                hp_last.append((stage, calls, total_us))

    hp_last.sort(key=lambda x: x[2], reverse=True)
    hp_sum = sum(x[2] for x in hp_last)

    print(f"  {'Stage':<40} {'Calls':>8} {'TotalUs':>12} {'AvgUs':>10} {'%HotPath':>10} {'%Frame':>10}")
    print(f"  {'-'*40} {'-'*8} {'-'*12} {'-'*10} {'-'*10} {'-'*10}")
    for stage, calls, total_us in hp_last:
        avg = total_us // max(calls, 1)
        pct_hp = total_us / max(hp_sum, 1) * 100
        pct_frame = total_us / max(frame_wall, 1) * 100
        print(f"  {stage:<40} {calls:>8,} {total_us:>12,} {avg:>10,} {pct_hp:>9.1f}% {pct_frame:>9.1f}%")
    print(f"  {'-'*40} {'-'*8} {'-'*12} {'-'*10} {'-'*10} {'-'*10}")
    print(f"  {'TOTAL':<40} {'':>8} {hp_sum:>12,} {'':>10} {'100.0%':>10} {hp_sum/max(frame_wall,1)*100:>9.1f}%")
    print()

    # ============================
    # CPU STAGE TIMING (Last Frame)
    # ============================
    print("=" * 100)
    print(f"  FINE-GRAINED STAGE TIMING (DX12CpuStageTiming) - Frame #{last_present}")
    print("=" * 100)

    cs_last = []
    for (cat, stage), entries in cpustage.items():
        for (p, calls, total_us) in entries:
            if p == last_present and total_us > 0:
                cs_last.append((cat, stage, calls, total_us))

    cs_last.sort(key=lambda x: x[3], reverse=True)
    cs_sum = sum(x[3] for x in cs_last)

    print(f"  {'Category':<20} {'Stage':<45} {'Calls':>8} {'TotalUs':>12} {'%Stage':>10} {'%Frame':>10}")
    print(f"  {'-'*20} {'-'*45} {'-'*8} {'-'*12} {'-'*10} {'-'*10}")
    for cat, stage, calls, total_us in cs_last:
        pct_cs = total_us / max(cs_sum, 1) * 100
        pct_frame = total_us / max(frame_wall, 1) * 100
        print(f"  {cat:<20} {stage:<45} {calls:>8,} {total_us:>12,} {pct_cs:>9.1f}% {pct_frame:>9.1f}%")
    print(f"  {'-'*20} {'-'*45} {'-'*8} {'-'*12} {'-'*10} {'-'*10}")
    print(f"  {'TOTAL':<20} {'':45} {'':>8} {cs_sum:>12,} {'':>10} {cs_sum/max(frame_wall,1)*100:>9.1f}%")
    print()

    # ============================
    # API WALL TIMING (Last Frame)
    # ============================
    print("=" * 100)
    print(f"  API WALL TIMING (DX12ApiWallTiming) - Frame #{last_present}")
    print("=" * 100)

    aw_last = []
    for bucket, entries in apiwall.items():
        for (p, calls, total_us) in entries:
            if p == last_present and total_us > 0:
                aw_last.append((bucket, calls, total_us))

    aw_last.sort(key=lambda x: x[2], reverse=True)
    aw_sum = sum(x[2] for x in aw_last)

    print(f"  {'Bucket':<30} {'Calls':>8} {'TotalUs':>12} {'AvgUs':>10} {'%ApiWall':>10} {'%Frame':>10}")
    print(f"  {'-'*30} {'-'*8} {'-'*12} {'-'*10} {'-'*10} {'-'*10}")
    for bucket, calls, total_us in aw_last:
        avg = total_us // max(calls, 1)
        pct_aw = total_us / max(aw_sum, 1) * 100
        pct_frame = total_us / max(frame_wall, 1) * 100
        print(f"  {bucket:<30} {calls:>8,} {total_us:>12,} {avg:>10,} {pct_aw:>9.1f}% {pct_frame:>9.1f}%")
    print(f"  {'-'*30} {'-'*8} {'-'*12} {'-'*10} {'-'*10} {'-'*10}")
    print(f"  {'TOTAL':<30} {'':>8} {aw_sum:>12,} {'':>10} {'100.0%':>10} {aw_sum/max(frame_wall,1)*100:>9.1f}%")

    # ============================
    # AVERAGE OVER LAST N FRAMES
    # ============================
    print("\n" + "=" * 100)
    print(f"  AVERAGE OVER LAST {LAST_N} FRAMES (#{last_frames[0]['present']} - #{last_frames[-1]['present']})")
    print("=" * 100)

    # Compute average frame wall
    avg_frame_wall = 0.0
    avg_cpu_total = 0.0
    avg_log_cost = 0.0
    avg_api_scope = 0.0
    for f in last_frames:
        p = f["present"]
        avg_cpu_total += f["cpuUsTotal"]
        if p in timelines:
            avg_frame_wall += timelines[p].get("frameWallUs", 0)
            avg_api_scope += timelines[p].get("apiScopeTotalUs", 0)
        if p in logcosts:
            avg_log_cost += logcosts[p].get("totalUs", 0)

    n_valid = max(len(last_frames), 1)
    avg_frame_wall /= n_valid
    avg_cpu_total /= n_valid
    avg_log_cost /= n_valid
    avg_api_scope /= n_valid
    avg_interval = avg_frame_wall  # approximate

    print(f"  Avg frame wall:          {avg_frame_wall:>10,.0f} us  ({avg_frame_wall/1000:.2f} ms)")
    print(f"  Avg API scope total:     {avg_api_scope:>10,.0f} us  ({avg_api_scope/1000:.2f} ms)")
    print(f"  Avg CPU (hook) total:    {avg_cpu_total:>10,.1f} us  ({avg_cpu_total/1000:.2f} ms)")
    print(f"  Avg log cost:            {avg_log_cost:>10,.0f} us  ({avg_log_cost/1000:.2f} ms)")
    if avg_frame_wall > 0:
        print(f"  Avg expected FPS:        {1_000_000/avg_frame_wall:>10.1f}")
    print()

    # Avg HotPath
    hp_avg = defaultdict(lambda: {"totalUs": 0, "calls": 0, "nFrames": 0})
    for stage, entries in hotpath.items():
        for (p, calls, total_us) in entries:
            if p in last_present_ids:
                hp_avg[stage]["totalUs"] += total_us
                hp_avg[stage]["calls"] += calls
                hp_avg[stage]["nFrames"] += 1

    print("  --- Average HotPath Timing ---")
    hp_avg_list = []
    for stage, a in hp_avg.items():
        n = max(a["nFrames"], 1)
        avg_us = a["totalUs"] / n
        hp_avg_list.append((stage, a["calls"] / n, avg_us))

    hp_avg_list.sort(key=lambda x: x[2], reverse=True)
    total_hp_avg = sum(x[2] for x in hp_avg_list)
    print(f"  {'Stage':<40} {'AvgCalls':>10} {'AvgUs':>12} {'%HotPath':>10} {'%Frame':>10}")
    print(f"  {'-'*40} {'-'*10} {'-'*12} {'-'*10} {'-'*10}")
    for stage, avg_calls, avg_us in hp_avg_list[:20]:
        pct_hp = avg_us / max(total_hp_avg, 1) * 100
        pct_frame = avg_us / max(avg_frame_wall, 1) * 100
        print(f"  {stage:<40} {avg_calls:>10,.0f} {avg_us:>12,.0f} {pct_hp:>9.1f}% {pct_frame:>9.1f}%")

    # ============================
    # TOP HOTSPOTS (grouped, avg)
    # ============================
    print("\n" + "=" * 100)
    print(f"  TOP CPU HOTSPOTS (grouped, avg over last {LAST_N} frames)")
    print("=" * 100)

    # Group by stage name across categories
    grouped = defaultdict(lambda: {"avgUs": 0.0, "avgCalls": 0.0})
    for (cat, stage), entries in cpustage.items():
        total_us = 0
        total_calls = 0
        n = 0
        for (p, calls, us) in entries:
            if p in last_present_ids:
                total_us += us
                total_calls += calls
                n += 1
        if n > 0:
            grouped[stage]["avgUs"] += total_us / n
            grouped[stage]["avgCalls"] += total_calls / n

    gl = sorted(grouped.items(), key=lambda x: x[1]["avgUs"], reverse=True)

    print(f"  {'Rank':<5} {'Stage':<55} {'AvgUs/Frame':>14} {'Frame%':>10}")
    print(f"  {'-'*5} {'-'*55} {'-'*14} {'-'*10}")
    for rank, (name, g) in enumerate(gl[:25], 1):
        pct = g["avgUs"] / avg_frame_wall * 100 if avg_frame_wall > 0 else 0
        print(f"  {rank:<5} {name:<55} {g['avgUs']:>14,.0f} {pct:>9.1f}%")

    # ============================
    # ASCII BAR CHART
    # ============================
    print("\n" + "=" * 100)
    print(f"  CPU COST BREAKDOWN (avg over last {LAST_N} frames)")
    print("=" * 100)

    max_us = gl[0][1]["avgUs"] if gl else 1
    max_bar_width = 50

    for name, g in gl[:20]:
        bar_len = int(g["avgUs"] / max(max_us, 1) * max_bar_width)
        bar = "#" * bar_len
        pct = g["avgUs"] / max(avg_frame_wall, 1) * 100
        print(f"  {name:<55} {bar:<50} {g['avgUs']:>8,.0f} us ({pct:>5.1f}%)")

    # ============================
    # PERF EVENTS (last frame)
    # ============================
    print("\n" + "=" * 100)
    print(f"  MISC PERF SCOPES (DX12Perf) - Frame #{last_present}")
    print("=" * 100)

    # Aggregate perf by scope for last frame
    perf_last = defaultdict(lambda: {"totalUs": 0, "count": 0})
    for scope, entries in perf_events.items():
        for (p, us_val) in entries:
            if p == last_present:
                perf_last[scope]["totalUs"] += us_val
                perf_last[scope]["count"] += 1

    pf_list = sorted(perf_last.items(), key=lambda x: x[1]["totalUs"], reverse=True)
    print(f"  {'Scope':<30} {'Count':>8} {'TotalUs':>12} {'%Frame':>10}")
    print(f"  {'-'*30} {'-'*8} {'-'*12} {'-'*10}")
    for scope, a in pf_list:
        pct = a["totalUs"] / max(frame_wall, 1) * 100
        print(f"  {scope:<30} {a['count']:>8,} {a['totalUs']:>12,} {pct:>9.1f}%")

    # ============================
    # LOG COST DETAILS (last frame)
    # ============================
    if lc:
        print("\n" + "=" * 100)
        print(f"  LOG COST DETAILS - Frame #{last_present}")
        print("=" * 100)
        print(f"  JSON calls:       {lc.get('jsonCalls', 0):>10,}")
        print(f"  Text calls:       {lc.get('textCalls', 0):>10,}")
        print(f"  Debug+JSON calls: {lc.get('debugJsonCalls', 0):>10,}")
        print(f"  Enqueue calls:    {lc.get('enqueueCalls', 0):>10,}")
        print(f"  Format us:        {lc.get('formatUs', 0):>10,}  ({lc.get('formatUs',0)/max(log_total,1)*100:.1f}%)")
        print(f"  Enqueue us:       {lc.get('enqueueUs', 0):>10,}  ({lc.get('enqueueUs',0)/max(log_total,1)*100:.1f}%)")
        print(f"  Total us:         {log_total:>10,}  ({log_total/max(frame_wall,1)*100:.1f}% of frame)")
        print(f"  Dropped lines:    {lc.get('droppedLines', 0):>10,}")

    # ============================
    # SUMMARY
    # ============================
    print("\n" + "=" * 100)
    print("  EXECUTIVE SUMMARY")
    print("=" * 100)
    print(f"  Frame wall avg:       {avg_frame_wall:>12,.0f} us  ({avg_frame_wall/1000:.2f} ms)")
    print(f"  API scope total avg:  {avg_api_scope:>12,.0f} us  ({avg_api_scope/1000:.2f} ms)  [{avg_api_scope/max(avg_frame_wall,1)*100:.1f}%]")
    print(f"  Hook CPU avg:         {avg_cpu_total:>12,.1f} us  ({avg_cpu_total/1000:.2f} ms)  [{avg_cpu_total/max(avg_frame_wall,1)*100:.1f}%]")
    print(f"  Log cost avg:         {avg_log_cost:>12,.0f} us  ({avg_log_cost/1000:.2f} ms)  [{avg_log_cost/max(avg_frame_wall,1)*100:.1f}%]")
    print()
    if avg_frame_wall > 0:
        print(f"  Expected max FPS:     {1_000_000/avg_frame_wall:>12.1f}")
        overhead_pct = (avg_cpu_total + avg_log_cost) / avg_frame_wall * 100
        print(f"  Total overhead:       {avg_cpu_total + avg_log_cost:>12,.0f} us  ({overhead_pct:.1f}% of frame)")
    print()
    if gl:
        print(f"  #1 hotspot: {gl[0][0]}  ({gl[0][1]['avgUs']:,.0f} us/frame)")
        if len(gl) > 1:
            print(f"  #2 hotspot: {gl[1][0]}  ({gl[1][1]['avgUs']:,.0f} us/frame)")
        if len(gl) > 2:
            print(f"  #3 hotspot: {gl[2][0]}  ({gl[2][1]['avgUs']:,.0f} us/frame)")
    print()


if __name__ == "__main__":
    main()
