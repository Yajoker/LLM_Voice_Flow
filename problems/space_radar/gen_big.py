import random,sys
random.seed(int(sys.argv[1]))
n=random.randint(1,12); q=random.randint(1,12)
S=random.choice([50,1000,100000,1000000000])  # 不同坐标尺度
out=[f'{n} {q}']
ortho=[[(1,0,0),(0,1,0),(0,0,1)],[(1,1,0),(1,-1,0),(0,0,1)],[(1,1,1),(1,-1,0),(1,1,-2)],[(2,1,0),(-1,2,0),(0,0,1)]]
def rc(): return random.randint(-S,S)
for _ in range(n):
    t=random.randint(0,3)
    if t==0: out.append(f'0 {rc()} {rc()} {rc()} {random.randint(0,S)}')
    elif t==1:
        b=random.choice(ortho); ax=[]
        for (x,y,z) in b:
            s=random.randint(1,max(1,S//10)); ax+=[x*s,y*s,z*s]
        out.append('1 '+f'{rc()} {rc()} {rc()} '+' '.join(map(str,ax)))
    elif t==2:
        while True:
            a=(random.randint(-20,20),random.randint(-20,20),random.randint(-20,20))
            if a!=(0,0,0):break
        out.append(f'2 {rc()} {rc()} {rc()} {a[0]} {a[1]} {a[2]} {random.randint(0,S)} {random.randint(0,S)}')
    else:
        out.append('3 '+' '.join(str(rc()) for _ in range(9)))
for _ in range(q):
    while True:
        d=(random.randint(-5,5),random.randint(-5,5),random.randint(-5,5))
        if d!=(0,0,0):break
    out.append(f'2 {rc()} {rc()} {rc()} {d[0]} {d[1]} {d[2]} {random.randint(0,100000)}')
print('\n'.join(out))
