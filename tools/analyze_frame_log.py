#!/usr/bin/env python3
"""
Frame-level CPU timing analyzer for d3d12_log.jsonl.

Produces a sorted report of every function/stage by total us and frame %,
both in-frame and out-of-frame.

Usage: python tools/analyze_frame_log.py [log_path]
  Default: D:\\SSMTCacheFolder\\3Dmigoto\\ZZMIDX12\\d3d12_log.jsonl
"""

import sys
import json
import os
from collections import defaultdict

DEFAULT_LOG = r"D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl"


def parse_bare_json(text):
    """Parse a bare JSON string (no surrounding braces) into a dict."""
    if not text:
        return {}
    text = text.strip()
    if not text:
        return {}
    if text.startswith("{"):
        return json.loads(text)
    # Wrap in braces
    try:
        return json.loads("{" + text + "}")
    except json.JSONDecodeError:
        return {}


def main():
    log_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LOG

    if not os.path.exists(log_path):
        print(f"ERROR: Log file not found: {log_path}")
        sys.exit(1)

    file_size_mb = os.path.getsize(log_path) / (1024 * 1024)
    print(f"Reading: {log_path} ({file_size_mb:.1f} MB)")

    # Data structures
    frames = []  # list of {present_id, frameWallUs, frameIntervalUs, ...}

    # In-frame: keyed by (category, stage) -> [totalUs_per_frame]
    in_frame_timing = defaultdict(list)   # (cat, stage) -> [(present_id, totalUs, calls)]

    # Out-of-frame: same structure but for non-present events
    # We accumulate all timing events, then subtract in-frame from total
    all_timing = defaultdict(list)
    in_frame_present_ids = set()

    # Also track: DX12HotPathTiming, DX12CpuStageTiming, DX12ApiWallTiming, DX12Perf, DX12LogCost,
    # DX12ProfilingFrame, DX12CpuTimelineCoverage, DX12ResidentMaintenance
    # plus any DX12Perf scope events

    line_count = 0
    timeline_count = 0
    current_present_id = None
    log_cost_events = []  # (present_id, totalUs)

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
            try:
                timestamp = int(ts_str)
            except ValueError:
                continue

            data = parse_bare_json(message)

            # Track present boundaries from DX12FrameTimeline
            if category == "DX12FrameTimeline":
                timeline_count += 1
                present_id = data.get("present", 0)
                frame_wall_us = data.get("frameWallUs", 0)
                frame_interval_us = data.get("frameIntervalUs", 0)
                present_wall_us = data.get("presentWallUs", 0)
                outside_present_us = data.get("outsidePresentUs", 0)

                frames.append({
                    "present": present_id,
                    "frameWallUs": frame_wall_us,
                    "frameIntervalUs": frame_interval_us,
                    "presentWallUs": present_wall_us,
                    "outsidePresentUs": outside_present_us,
                })
                current_present_id = present_id
                in_frame_present_ids.add(present_id)

            # DX12HotPathTiming
            if category == "DX12HotPathTiming":
                stage = data.get("stage", "?")
                total_us = data.get("totalUs", 0)
                calls = data.get("calls", 0)
                if current_present_id is not None:
                    in_frame_timing[("HotPath", stage)].append((current_present_id, total_us, calls))
                all_timing[("HotPath", stage)].append((current_present_id, total_us, calls))

            # DX12CpuStageTiming
            if category == "DX12CpuStageTiming":
                cat = data.get("category", "?")
                stage = data.get("stage", "?")
                kind = data.get("kind", 0)
                total_us = data.get("totalUs", 0)
                calls = data.get("calls", 0)
                key = ("CpuStage", f"{cat}/{stage}/k{kind}")
                if current_present_id is not None:
                    in_frame_timing[key].append((current_present_id, total_us, calls))
                all_timing[key].append((current_present_id, total_us, calls))

            # DX12ApiWallTiming
            if category == "DX12ApiWallTiming":
                bucket = data.get("bucket", "?")
                total_us = data.get("totalUs", 0)
                calls = data.get("calls", 0)
                key = ("ApiWall", bucket)
                if current_present_id is not None:
                    in_frame_timing[key].append((current_present_id, total_us, calls))
                all_timing[key].append((current_present_id, total_us, calls))

            # DX12Perf - scope timing
            if category == "DX12Perf":
                scope = data.get("scope", "?")
                us_val = data.get("us", 0)
                key = ("PerfScope", scope)
                if current_present_id is not None:
                    in_frame_timing[key].append((current_present_id, us_val, 1))
                all_timing[key].append((current_present_id, us_val, 1))

            # DX12LogCost
            if category == "DX12LogCost":
                total_us = data.get("totalUs", 0)
                if current_present_id is not None:
                    log_cost_events.append((current_present_id, total_us))
                    in_frame_timing[("LogCost", "total")].append((current_present_id, total_us, 1))

            # DX12ProfilingFrame - overall CPU summary
            if category == "DX12ProfilingFrame":
                cpu_total = data.get("cpuUs_total", 0)
                if current_present_id is not None and cpu_total > 0:
                    in_frame_timing[("Profiling", "cpuUs_total")].append((current_present_id, cpu_total, 1))

            # DX12CpuTimelineCoverage
            if category == "DX12CpuTimelineCoverage":
                api_total = data.get("apiScopeTotalUs", 0)
                if current_present_id is not None and api_total > 0:
                    in_frame_timing[("CpuTimeline", "apiScopeTotalUs")].append((current_present_id, api_total, 1))

    print(f"Total lines: {line_count}, Timeline events: {timeline_count}, Frames: {len(frames)}")

    if len(frames) == 0:
        print("\nNo frames found. Check log format.")
        return

    # Pick the last frame for detailed analysis (most representative, fully warmed up)
    # Also compute averages over the last N frames
    LAST_N = min(20, len(frames))
    last_frames = frames[-LAST_N:]
    last_present_ids = {f["present"] for f in last_frames}

    # ============================================================
    # REPORT: Last frame detailed
    # ============================================================
    last_frame = frames[-1]
    last_present_id = last_frame["present"]
    frame_wall_us = last_frame["frameWallUs"]
    frame_interval_us = last_frame["frameIntervalUs"]
    present_wall_us = last_frame["presentWallUs"]
    outside_present_us = last_frame["outsidePresentUs"]

    print("\n" + "=" * 90)
    print(f"  FRAME ANALYSIS REPORT")
    print("=" * 90)
    print(f"  Analyzed frame: present #{last_present_id}")
    print(f"  Frame wall time:        {frame_wall_us:>10,} us  ({frame_wall_us/1000:>8.2f} ms)")
    print(f"  Frame interval:         {frame_interval_us:>10,} us  ({frame_interval_us/1000:>8.2f} ms)")
    if frame_interval_us > 0:
        fps = 1_000_000.0 / frame_interval_us
        print(f"  Expected FPS:           {fps:>10.1f}")
    print(f"  Present wall time:      {present_wall_us:>10,} us  ({present_wall_us/1000:>8.2f} ms)")
    print(f"  Outside-Present time:   {outside_present_us:>10,} us  ({outside_present_us/1000:>8.2f} ms)")
    print()

    # Collect all timing for the last frame
    last_frame_timings = []

    for key, entries in in_frame_timing.items():
        for (pid, total_us, calls) in entries:
            if pid == last_present_id and total_us > 0:
                last_frame_timings.append({
                    "category": key[0],
                    "stage": key[1],
                    "totalUs": total_us,
                    "calls": calls,
                    "pct": (total_us / frame_wall_us * 100.0) if frame_wall_us > 0 else 0,
                })

    # Sort by totalUs desc
    last_frame_timings.sort(key=lambda x: x["totalUs"], reverse=True)

    # Print table
    print(f"  {'Category':<14} {'Stage':<50} {'Calls':>7} {'TotalUs':>12} {'Frame%':>8}")
    print(f"  {'-'*14} {'-'*50} {'-'*7} {'-'*12} {'-'*8}")
    total_accounted = 0
    for t in last_frame_timings:
        print(f"  {t['category']:<14} {t['stage']:<50} {t['calls']:>7,} {t['totalUs']:>12,} {t['pct']:>7.2f}%")
        total_accounted += t["totalUs"]

    print(f"  {'-'*14} {'-'*50} {'-'*7} {'-'*12} {'-'*8}")
    print(f"  {'(sum)':<14} {'':50} {'':>7} {total_accounted:>12,} {total_accounted/frame_wall_us*100:>7.2f}%")
    if frame_wall_us > 0:
        unaccounted = frame_wall_us - total_accounted
        if unaccounted > 0:
            print(f"  {'(unaccounted)':<14} {'(app GPU work, driver, etc.)':50} {'':>7} {unaccounted:>12,} {unaccounted/frame_wall_us*100:>7.2f}%")

    # ============================================================
    # REPORT: Average over last N frames
    # ============================================================
    print("\n" + "=" * 90)
    print(f"  AVERAGE OVER LAST {LAST_N} FRAMES (present #{last_frames[0]['present']} - #{last_frames[-1]['present']})")
    print("=" * 90)

    # Aggregate per key across last frames
    avg_timings = defaultdict(lambda: {"totalUs": 0, "calls": 0, "frameCount": 0, "framesSeen": set()})

    for key, entries in in_frame_timing.items():
        for (pid, total_us, calls) in entries:
            if pid in last_present_ids:
                ak = avg_timings[key]
                ak["totalUs"] += total_us
                ak["calls"] += calls
                ak["frameCount"] += 1
                ak["framesSeen"].add(pid)

    # Average frame wall
    avg_frame_wall = sum(f["frameWallUs"] for f in last_frames) / len(last_frames)

    print(f"  Avg frame wall: {avg_frame_wall:,.0f} us ({avg_frame_wall/1000:.2f} ms)")
    print(f"  Avg frame interval: {sum(f['frameIntervalUs'] for f in last_frames)/len(last_frames):,.0f} us")
    print()

    avg_list = []
    for key, ak in avg_timings.items():
        n_frames = len(ak["framesSeen"])
        avg_us = ak["totalUs"] / n_frames if n_frames > 0 else 0
        avg_calls = ak["calls"] / n_frames if n_frames > 0 else 0
        avg_list.append({
            "category": key[0],
            "stage": key[1],
            "avgUs": avg_us,
            "avgCalls": avg_calls,
            "pct": (avg_us / avg_frame_wall * 100.0) if avg_frame_wall > 0 else 0,
            "nFrames": n_frames,
        })

    avg_list.sort(key=lambda x: x["avgUs"], reverse=True)

    print(f"  {'Category':<14} {'Stage':<50} {'AvgCalls':>8} {'AvgUs':>12} {'Frame%':>8} {'Frames':>7}")
    print(f"  {'-'*14} {'-'*50} {'-'*8} {'-'*12} {'-'*8} {'-'*7}")
    total_avg = 0
    for t in avg_list:
        print(f"  {t['category']:<14} {t['stage']:<50} {t['avgCalls']:>8,.0f} {t['avgUs']:>12,.0f} {t['pct']:>7.2f}% {t['nFrames']:>7}")
        total_avg += t["avgUs"]

    print(f"  {'-'*14} {'-'*50} {'-'*8} {'-'*12} {'-'*8} {'-'*7}")
    print(f"  {'(sum)':<14} {'':50} {'':>8} {total_avg:>12,.0f} {total_avg/avg_frame_wall*100:>7.2f}%")

    # ============================================================
    # REPORT: Out-of-frame timing (during startup, between frames)
    # ============================================================
    print("\n" + "=" * 90)
    print(f"  OUT-OF-FRAME TIMING (non-Present events, e.g. startup / loading)")
    print("=" * 90)

    oof_timings = defaultdict(lambda: {"totalUs": 0, "calls": 0, "count": 0})

    for key, entries in all_timing.items():
        for (pid, total_us, calls) in entries:
            if pid is None or pid not in in_frame_present_ids:
                oof_timings[key]["totalUs"] += total_us
                oof_timings[key]["calls"] += calls
                oof_timings[key]["count"] += 1

    oof_list = []
    for key, ak in oof_timings.items():
        if ak["totalUs"] > 0:
            oof_list.append({
                "category": key[0],
                "stage": key[1],
                "totalUs": ak["totalUs"],
                "calls": ak["calls"],
                "count": ak["count"],
            })

    oof_list.sort(key=lambda x: x["totalUs"], reverse=True)

    if oof_list:
        print(f"  {'Category':<14} {'Stage':<50} {'Events':>7} {'Calls':>8} {'TotalUs':>12}")
        print(f"  {'-'*14} {'-'*50} {'-'*7} {'-'*8} {'-'*12}")
        oof_total = 0
        for t in oof_list:
            print(f"  {t['category']:<14} {t['stage']:<50} {t['count']:>7} {t['calls']:>8} {t['totalUs']:>12,}")
            oof_total += t["totalUs"]
        print(f"  {'-'*14} {'-'*50} {'-'*7} {'-'*8} {'-'*12}")
        print(f"  {'(sum)':<14} {'':50} {'':>7} {'':>8} {oof_total:>12,}  ({oof_total/1000:.2f} ms)")
    else:
        print("  (none)")

    # ============================================================
    # SUMMARY: Top 10 hotspots
    # ============================================================
    print("\n" + "=" * 90)
    print(f"  TOP CPU HOTSPOTS (by avg per-frame cost, last {LAST_N} frames)")
    print("=" * 90)

    # Group by stage name (merge categories)
    grouped = defaultdict(lambda: {"avgUs": 0, "avgCalls": 0})
    for t in avg_list:
        name = t["stage"]
        grouped[name]["avgUs"] += t["avgUs"]
        grouped[name]["avgCalls"] += t["avgCalls"]

    grouped_list = sorted(grouped.items(), key=lambda x: x[1]["avgUs"], reverse=True)

    print(f"  {'Rank':<5} {'Stage':<55} {'AvgUs/Frame':>14} {'Frame%':>8}")
    print(f"  {'-'*5} {'-'*55} {'-'*14} {'-'*8}")
    for rank, (name, g) in enumerate(grouped_list[:20], 1):
        pct = g["avgUs"] / avg_frame_wall * 100 if avg_frame_wall > 0 else 0
        print(f"  {rank:<5} {name:<55} {g['avgUs']:>14,.0f} {pct:>7.2f}%")

    # ============================================================
    # ASCII bar chart: top 15 functions by avg cost
    # ============================================================
    print("\n" + "=" * 90)
    print(f"  CPU COST BREAKDOWN (avg over last {LAST_N} frames)")
    print("=" * 90)

    max_us = grouped_list[0][1]["avgUs"] if grouped_list else 1
    max_bar_width = 50

    for name, g in grouped_list[:15]:
        bar_len = int(g["avgUs"] / max_us * max_bar_width) if max_us > 0 else 0
        bar = "#" * bar_len
        pct = g["avgUs"] / avg_frame_wall * 100 if avg_frame_wall > 0 else 0
        print(f"  {name:<55} {bar:<50} {g['avgUs']:>8,.0f} us ({pct:>5.1f}%)")

    print()


if __name__ == "__main__":
    main()
