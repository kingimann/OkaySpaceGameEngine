import sys,re,collections
pid=sys.argv[1]
cur=None; agg=collections.Counter(); big=[]
for line in open(f'/proc/{pid}/smaps'):
    if re.match(r'^[0-9a-f]+-[0-9a-f]+ ',line):
        parts=line.split()
        name=parts[5] if len(parts)>5 else '[anon]'
        cur=name
    elif line.startswith('Anonymous:'):
        kb=int(line.split()[1]); agg[cur]+=kb
        if cur=='[anon]' and kb>4096: big.append(kb)
for k,v in agg.most_common(12): print(f'{v/1024:8.1f} MB anon  {k}')
print('anon mmaps >4MB:', sorted(big,reverse=True)[:20])
