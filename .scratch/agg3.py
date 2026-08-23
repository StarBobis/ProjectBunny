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

agg = collections.defaultdict(lambda: [0,0,0])
presents=set()
with openauto(r'D:\Dev\ProjectBunny\.scratch\createtiming.txt') as f:
    for line in f:
        d = parse(line)
        key = d.get('api','?')+'/'+d.get('stage','?')
        presents.add(d.get('present'))
        a = agg[key]
        a[0] += int(d.get('totalUs',0))
        a[1] += int(d.get('calls',0))
        a[2] = max(a[2], int(d.get('maxUs',0)))
np=max(len(presents),1)
print('=== DX12CreateTiming (presents:',np,') ===')
print(f'{"api/stage":56s} {"totalUs":>10s} {"calls":>10s} {"maxUs":>8s} {"us/frame":>9s} {"ns/call":>8s}')
for k,v in sorted(agg.items(), key=lambda kv:-kv[1][0])[:40]:
    ns = v[0]*1000.0/v[1] if v[1] else 0
    print(f'{k:56s} {v[0]:10d} {v[1]:10d} {v[2]:8d} {v[0]/np:9.2f} {ns:8.0f}')
