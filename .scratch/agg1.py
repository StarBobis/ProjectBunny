import re, collections, codecs

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

# frames: count presents + wall
nf=0; wallsum=0; frames=set()
with openauto(r'D:\Dev\ProjectBunny\.scratch\frames.txt') as f:
    for line in f:
        d=parse(line)
        if d.get('func')=='DX12FrameTimeline':
            nf+=1
            wallsum+=int(d.get('presentWallUs',0))
            frames.add(d.get('present'))
print('frames:', nf, 'unique presents:', len(frames), 'sum presentWallUs:', wallsum)

# apiwall
agg = collections.defaultdict(lambda: [0,0,0])
presents=set()
with openauto(r'D:\Dev\ProjectBunny\.scratch\apiwall.txt') as f:
    for line in f:
        d = parse(line)
        b = d.get('bucket','?')
        presents.add(d.get('present'))
        a = agg[b]
        a[0] += int(d.get('totalUs',0))
        a[1] += int(d.get('calls',0))
        a[2] = max(a[2], int(d.get('maxUs',0)))

np=max(len(presents),1)
print('=== DX12ApiWallTiming (presents:',np,') ===')
print(f'{"bucket":42s} {"totalUs":>12s} {"calls":>12s} {"maxUs":>10s} {"us/frame":>10s} {"calls/frame":>11s}')
rows = sorted(agg.items(), key=lambda kv:-kv[1][0])
tot = sum(v[0] for _,v in rows)
for k,v in rows[:30]:
    print(f'{k:42s} {v[0]:12d} {v[1]:12d} {v[2]:10d} {v[0]/np:10.1f} {v[1]/np:11.2f}')
print('TOTAL totalUs =', tot, '=> us/frame =', tot/np)
