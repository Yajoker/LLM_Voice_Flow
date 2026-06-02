import random, sys
seed = int(sys.argv[1])
random.seed(seed)

# 一些整数正交三元组(用于 OBB 非轴对齐半轴)
ortho_bases = [
    [(1,0,0),(0,1,0),(0,0,1)],
    [(1,1,0),(1,-1,0),(0,0,1)],
    [(1,1,1),(1,-1,0),(1,1,-2)],
    [(2,1,0),(-1,2,0),(0,0,1)],
    [(0,0,1),(1,1,0),(1,-1,0)],
]

def rc():  # 随机坐标
    return random.randint(-5,5)

n = random.randint(1,8)
q = random.randint(1,8)
lines = [f"{n} {q}"]
for _ in range(n):
    t = random.randint(0,3)
    if t==0:
        lines.append(f"0 {rc()} {rc()} {rc()} {random.randint(0,4)}")
    elif t==1:
        base = random.choice(ortho_bases)
        cx,cy,cz = rc(),rc(),rc()
        axes=[]
        for (vx,vy,vz) in base:
            s = random.randint(1,3)
            axes += [vx*s, vy*s, vz*s]
        lines.append("1 "+f"{cx} {cy} {cz} "+" ".join(map(str,axes)))
    elif t==2:
        # 非零轴向量
        while True:
            ax,ay,az = rc(),rc(),rc()
            if (ax,ay,az)!=(0,0,0): break
        lines.append(f"2 {rc()} {rc()} {rc()} {ax} {ay} {az} {random.randint(0,4)} {random.randint(0,4)}")
    else:
        lines.append("3 "+" ".join(str(rc()) for _ in range(9)))
for _ in range(q):
    while True:
        dx,dy,dz = random.randint(-3,3),random.randint(-3,3),random.randint(-3,3)
        if (dx,dy,dz)!=(0,0,0): break
    lines.append(f"2 {rc()} {rc()} {rc()} {dx} {dy} {dz} {random.randint(0,6)}")
print("\n".join(lines))
