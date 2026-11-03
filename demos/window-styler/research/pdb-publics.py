import urllib.request, struct, json, pathlib, concurrent.futures, re, sys, time, hashlib

url, output, pattern = sys.argv[1:4]
cache=pathlib.Path(output).parent/('range-cache-'+hashlib.sha256(url.encode()).hexdigest()[:12])
cache.mkdir(exist_ok=True)
def fetch(start, size):
    if not size: return b''
    local=pathlib.Path(url)
    if '://' not in url and local.is_file():
        with local.open('rb') as f:
            f.seek(start)
            value=f.read(size)
            if len(value)!=size:raise RuntimeError('Short local PDB read')
            return value
    path=cache/f'{start}-{size}'
    if path.exists() and path.stat().st_size==size:return path.read_bytes()
    for attempt in range(4):
        try:
            request = urllib.request.Request(url, headers={'Range': f'bytes={start}-{start+size-1}'})
            with urllib.request.urlopen(request, timeout=30) as response:
                if response.status != 206: raise RuntimeError(f'Range ignored: {response.status}')
                if not response.headers.get('Content-Range','').startswith(f'bytes {start}-'):raise RuntimeError('Wrong range')
                value=response.read(size+1)
                if len(value)!=size: raise RuntimeError(f'Short range: {len(value)}/{size}')
                path.write_bytes(value)
                return value
        except Exception:
            if attempt==3:raise
            time.sleep(attempt+1)
header=fetch(0,4096)
if not header.startswith(b'Microsoft C/C++ MSF 7.00'): raise RuntimeError('Not MSF7')
block, _, count, directory_size, _, blockmap = struct.unpack_from('<6I',header,32)
def blocks_data(blocks, size):
    runs=[]
    for b in blocks:
        if runs and b==runs[-1][0]+runs[-1][1] and runs[-1][1]<256: runs[-1][1]+=1
        else: runs.append([b,1])
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        chunks=list(pool.map(lambda run:fetch(run[0]*block,run[1]*block),runs))
    return b''.join(chunks)[:size]
directory_count=(directory_size+block-1)//block
map_data=fetch(blockmap*block,directory_count*4)
directory=blocks_data(struct.unpack(f'<{directory_count}I',map_data),directory_size)
n=struct.unpack_from('<I',directory)[0]
sizes=struct.unpack_from(f'<{n}I',directory,4)
streams=[]; cursor=4+n*4
for size in sizes:
    pages=0 if size==0xffffffff else (size+block-1)//block
    streams.append((size,struct.unpack_from(f'<{pages}I',directory,cursor)))
    cursor+=pages*4
def stream(i):
    size,blocks=streams[i]
    print(f'stream {i}: {size} bytes',flush=True)
    return blocks_data(blocks,size)
dbi=fetch(streams[3][1][0]*block,64)
sym_index=struct.unpack_from('<H',dbi,20)[0]
records=stream(sym_index)
results=[]; cursor=0; match=re.compile(pattern,re.I)
while cursor+4<=len(records):
    length,kind=struct.unpack_from('<HH',records,cursor)
    if length<2: break
    if kind==0x110e and length>=12:
        flags,offset,segment=struct.unpack_from('<IIH',records,cursor+4)
        name=records[cursor+14:cursor+2+length].split(b'\0',1)[0].decode('utf-8','replace')
        if match.search(name): results.append(dict(segment=segment,offset=offset,name=name))
    cursor+=length+2
pathlib.Path(output).write_text(json.dumps(results,indent=2),encoding='utf-8')
print(f'{len(results)} matching symbols',flush=True)
