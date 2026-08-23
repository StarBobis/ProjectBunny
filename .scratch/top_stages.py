import re, collections
import sys
tot = collections.defaultdict(lambda: [0,0])
pat = re.compile(r'func=DX12CpuStageTiming,present=\d+,category=([^,]+),stage=([^,]+),kind=\d+,calls=(\d+),items=\d+,totalUs=(\d+)')
with open(sys.argv[1] if len(sys.argv) > 1 else r'D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt', encoding='utf-8', errors='replace') as f:
    for line in f:
        if 'DX12CpuStageTiming' not in line: continue
        m = pat.search(line)
        if m:
            key = m.group(1)+'/'+m.group(2)
            tot[key][0]+=int(m.group(3)); tot[key][1]+=int(m.group(4))
rows = sorted(tot.items(), key=lambda kv:-kv[1][1])
print('%-40s %12s %10s %8s' % ('stage','calls','totalMs','avgNs'))
for k,(c,u) in rows[:24]:
    print('%-40s %12d %10.1f %8d' % (k, c, u/1000.0, u*1000//max(c,1)))
