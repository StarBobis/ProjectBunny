"""Analyze DX12 wrapper log file for performance data."""
import json, os, sys

path = r"D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.jsonl"
if len(sys.argv) > 1:
    path = sys.argv[1]

size = os.path.getsize(path)
print(f"Log file: {path} ({size/1024/1024:.0f}MB)")
print()

# Read last 3MB for latest frame summary
with open(path, "r", encoding="utf-8", errors="ignore") as f:
    f.seek(max(0, size - 3000000))
    lines = f.readlines()

hotpath = {}
profiling = {}
resident = {}
logcost = {}
timeline = {}

for line in lines:
    parts = line.strip().split("\t", 2)
    if len(parts) < 3:
        continue
    _, func, data = parts
    try:
        d = json.loads("{" + data + "}")
    except:
        continue

    if func == "DX12HotPathTiming":
        hotpath[d.get("stage", "")] = d
    elif func == "DX12ProfilingFrame":
        if d.get("present", 0) > profiling.get("present", 0):
            profiling = d
    elif func == "DX12LogCost":
        if d.get("present", 0) > logcost.get("present", 0):
            logcost = d
    elif func == "DX12FrameTimeline":
        if d.get("present", 0) > timeline.get("present", 0):
            timeline = d
    elif func == "DX12ResidentMaintenance":
        cat = d.get("category", "")
        if d.get("present", 0) > resident.get(cat, {}).get("present", 0):
            resident[cat] = d

present = timeline.get("present", profiling.get("present", "?"))
frameWall = timeline.get("frameWallUs", 0)
print(f"Frame {present}" + "=" * 60)

if timeline:
    print(f"Frame wall: {frameWall}us ({round(1000000/max(frameWall,1),1)}fps)")
    print(f"  Outside Present: {timeline.get('outsidePresentUs')}us")
    print(f"  Present wall: {timeline.get('presentWallUs')}us")
    print(f"    Wait: {timeline.get('presentSliceKnownUs')}us  "
          f"ModBegin: {timeline.get('presentModBeginFrameUs')}us  "
          f"ProfilingFlush: {timeline.get('presentProfilingFlushUs')}us")
    print(f"  Queue: wrapper={timeline.get('queueWrapperTotalUs')}us "
          f"orig={timeline.get('queueOriginalUs')}us "
          f"notify={timeline.get('queueNotifyUs')}us")

print()
print("TOP STAGES BY CPU TIME:")
stages = sorted(hotpath.items(), key=lambda x: x[1].get("totalUs", 0), reverse=True)[:12]
if stages:
    print(f"  {'Stage':30s} {'Calls':>6s} {'TotalUs':>8s} {'AvgUs':>6s} {'%Frame':>7s}")
    print(f"  {'-'*60}")
    for s, d in stages:
        calls = d.get("calls", 0)
        total = d.get("totalUs", 0)
        avg = d.get("avgUs", 0)
        pct = round(total / max(frameWall, 1) * 100, 1)
        print(f"  {s:30s} {calls:6d} {total:8d} {avg:6d}us {pct:7.1f}%")
else:
    print("  (no hotpath data - check if game is in menu/loading)")
print()

if profiling:
    print(f"Profiling: draws={profiling.get('drawTotal')} "
          f"dispatch={profiling.get('dispatch')} "
          f"binds={profiling.get('bindingsTotal')} "
          f"sPSO={profiling.get('setPipelineState')} "
          f"ffHits={profiling.get('fastForwardHits')}")
    print(f"  cpu_total={profiling.get('cpuUs_total')}us "
          f"draws={profiling.get('cpuUs_draws')}us "
          f"binds={profiling.get('cpuUs_bindings')}us")

if logcost:
    print(f"LogCost: jsonCalls={logcost.get('jsonCalls')} "
          f"text={logcost.get('textCalls')} "
          f"debug={logcost.get('debugJsonCalls')} "
          f"fmtUs={logcost.get('formatUs')} "
          f"enqUs={logcost.get('enqueueUs')} "
          f"dropped={logcost.get('droppedLines')}")

if resident:
    print()
    for cat, d in resident.items():
        if cat == "draw_decision":
            print(f"Draw decision: cacheHits={d.get('cacheHits')} "
                  f"fastRejects={d.get('fastRejects')} "
                  f"iaHashHits={d.get('iaHashCacheHits')} "
                  f"iaHashMisses={d.get('iaHashCacheMisses')} "
                  f"descriptorDraws={d.get('descriptorDraws')}")
        elif cat == "preskin_plan":
            print(f"PreSkin plan: lookups={d.get('lookups')} "
                  f"hits={d.get('hits')} "
                  f"negative={d.get('negativeHits')} "
                  f"positive={d.get('positiveHits')} "
                  f"misses={d.get('misses')}")
        elif cat == "dispatch_decision":
            print(f"Dispatch decision: cacheHits={d.get('cacheHits')} "
                  f"fastRejects={d.get('fastRejects')} "
                  f"preSkinCand={d.get('preSkinCandidateDispatches')}")
        elif cat == "preskin_bind_cache":
            print(f"PreSkin bind cache: calls={d.get('calls')} "
                  f"hits={d.get('hits')} "
                  f"misses={d.get('misses')} "
                  f"stores={d.get('stores')}")
        elif cat == "root":
            print(f"Root tables: gfxCalls={d.get('gfxTableCalls')} "
                  f"gfxTlsHits={d.get('gfxTableTlsHits')} "
                  f"gfxChanged={d.get('gfxTableChanged')} "
                  f"compCalls={d.get('compTableCalls')} "
                  f"compTlsHits={d.get('compTableTlsHits')}")
        elif cat == "ia":
            print(f"IA: ibCalls={d.get('ibCalls')} "
                  f"vbCalls={d.get('vbCalls')} "
                  f"runtimeIb={d.get('runtimeIbCalls')} "
                  f"runtimeVb={d.get('runtimeVbCalls')}")

print()
