import collections, codecs

def openauto(path):
    with open(path,'rb') as fb:
        head = fb.read(4)
    if head.startswith(codecs.BOM_UTF16_LE) or head.startswith(codecs.BOM_UTF16_BE):
        return open(path, encoding='utf-16')
    return open(path, encoding='utf-8', errors='replace')

def parse(line):
    d = {}
    for kv in line.rstrip('\n').split(','):
        i = kv.find('=')
        if i > 0:
            d[kv[:i]] = kv[i+1:]
    return d

# cpustage
agg = collections.defaultdict(lambda: [0,0,0,0])
presents=set()
with openauto(r'D:\Dev\ProjectBunny\.scratch\cpustage.txt') as f:
    for line in f:
        d = parse(line)
        key = d.get('category','?')+'/'+d.get('stage','?')
        presents.add(d.get('present'))
        a = agg[key]
        a[0] += int(d.get('totalUs',0))
        a[1] += int(d.get('calls',0))
        a[2] += int(d.get('items',0))
        a[3] = max(a[3], int(d.get('maxUs',0)))
np=max(len(presents),1)
print('=== DX12CpuStageTiming (presents:',np,') ===')
print(f'{"category/stage":52s} {"totalUs":>10s} {"calls":>9s} {"items":>9s} {"maxUs":>8s} {"us/frame":>9s}')
for k,v in sorted(agg.items(), key=lambda kv:-kv[1][0])[:30]:
    print(f'{k:52s} {v[0]:10d} {v[1]:9d} {v[2]:9d} {v[3]:8d} {v[0]/np:9.2f}')

# hotpath
agg2 = collections.defaultdict(lambda: [0,0,0])
with openauto(r'D:\Dev\ProjectBunny\.scratch\hotpath.txt') as f:
    for line in f:
        d = parse(line)
        s = d.get('stage','?')
        a = agg2[s]
        a[0] += int(d.get('totalUs',0))
        a[1] += int(d.get('calls',0))
        a[2] = max(a[2], int(d.get('maxUs',0)))
print()
print('=== DX12HotPathTiming ===')
print(f'{"stage":44s} {"totalUs":>12s} {"calls":>12s} {"maxUs":>10s} {"us/frame":>9s}')
for k,v in sorted(agg2.items(), key=lambda kv:-kv[1][0])[:35]:
    print(f'{k:44s} {v[0]:12d} {v[1]:12d} {v[2]:10d} {v[0]/np:9.2f}')
