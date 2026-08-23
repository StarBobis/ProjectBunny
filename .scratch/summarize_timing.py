import sys, collections, codecs

def openauto(path):
    with open(path, 'rb') as fb:
        head = fb.read(4)
    if head.startswith(codecs.BOM_UTF16_LE) or head.startswith(codecs.BOM_UTF16_BE):
        return open(path, encoding='utf-16')
    return open(path, encoding='utf-8', errors='replace')

def parse(line):
    d = {}
    for kv in line.rstrip('\n').split(','):
        i = kv.find('=')
        if i > 0:
            d[kv[:i]] = kv[i + 1:]
    return d

def toint(v):
    try:
        return int(v)
    except (TypeError, ValueError):
        return 0

def main(path):
    # All three timing families are Take/reset per-frame dumps, so the
    # correct aggregation is to SUM every dump line. We keep two windows:
    # "all" (whole log) and "steady" (skips the first WARMUP_MS to exclude
    # startup costs like deferred mod loads).
    WARMUP_MS = 15000
    hotpath = collections.defaultdict(lambda: [0, 0, 0, 0])   # callsAll, usAll, callsSteady, usSteady
    apiwall = collections.defaultdict(lambda: [0, 0, 0, 0])
    cpustage = collections.defaultdict(lambda: [0, 0, 0, 0])
    perf = collections.defaultdict(list)
    framecpu = []  # (wallUs, threadCpuUs) per frame
    first_time = None
    last_time = None

    with openauto(path) as f:
        for line in f:
            if 'func=DX12' not in line:
                continue
            d = parse(line)
            func = d.get('func')
            t = toint(d.get('time'))
            if t:
                if first_time is None:
                    first_time = t
                last_time = t
            steady = first_time is not None and (t - first_time) >= WARMUP_MS

            def accum(table, key):
                calls = toint(d.get('calls'))
                us = toint(d.get('totalUs'))
                rec = table[key]
                rec[0] += calls
                rec[1] += us
                if steady:
                    rec[2] += calls
                    rec[3] += us

            if func == 'DX12HotPathTiming':
                accum(hotpath, d.get('stage'))
            elif func == 'DX12ApiWallTiming':
                accum(apiwall, d.get('api') or d.get('bucket') or d.get('stage'))
            elif func == 'DX12CpuStageTiming':
                accum(cpustage, (d.get('category'), d.get('stage')))
            elif func == 'DX12FrameCpuDiag':
                if steady:
                    framecpu.append((toint(d.get('wallUs')), toint(d.get('threadCpuUs'))))
            elif func == 'DX12Perf':
                if steady:
                    perf[d.get('scope')].append(toint(d.get('us')))

    print('=== log:', path)
    print('=== timespan ms:', (last_time - first_time) if first_time and last_time else 0)

    def dump(table, title, name_fn, top=None):
        print()
        print(title)
        rows = []
        for key, (ca, ua, cs, us) in table.items():
            if cs > 0:
                rows.append((us, cs, ua, ca, name_fn(key)))
        rows.sort(reverse=True)
        for us, cs, ua, ca, name in (rows[:top] if top else rows):
            print('%-55s steady: calls=%-8d totalUs=%-10d avgNs=%-8d | all: calls=%-8d avgNs=%d'
                  % (name, cs, us, us * 1000 // cs, ca, ua * 1000 // ca if ca else 0))

    dump(hotpath, '=== DX12HotPathTiming (sum of per-frame dumps; steady = skip first %ds) ===' % (WARMUP_MS // 1000), lambda k: k)
    dump(apiwall, '=== DX12ApiWallTiming ===', lambda k: k, top=25)
    dump(cpustage, '=== DX12CpuStageTiming (top 30) ===', lambda k: '%s/%s' % k, top=30)

    print()
    print('=== DX12FrameCpuDiag (steady) ===')
    if framecpu:
        wall = sum(w for w, _ in framecpu)
        cpu = sum(c for _, c in framecpu)
        print('frames=%d  wallMs=%d  threadCpuMs=%d  threadCpuPct=%d'
              % (len(framecpu), wall // 1000, cpu // 1000, (cpu * 100 // wall) if wall else 0))

    print()
    print('=== DX12Perf scopes, steady (count, totalUs, avgUs, maxUs) ===')
    for scope, vals in sorted(perf.items(), key=lambda kv: -sum(kv[1])):
        if vals:
            print('%-30s n=%-6d total=%-10d avg=%-8d max=%d' % (scope, len(vals), sum(vals), sum(vals) // len(vals), max(vals)))

if __name__ == '__main__':
    main(sys.argv[1])
