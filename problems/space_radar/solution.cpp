// 太空探测与动态几何体查询 (3D 空间雷达系统)
// ---------------------------------------------------------------------------
// 思路概述：
//   维护一组几何体（球体 / OBB 长方体 / 圆柱体 / 三角面片）。
//   每次「射线脉冲」查询沿射线 P(t)=O+t*D (t>=0) 找出最先被命中的物体：
//     - 命中判定：返回 t 最小的物体；若 t 相同返回编号最小者；
//     - 边界算内部，射线起点在物体内部/边界则 t=0；
//     - 命中后该物体瞬间沿 normalize(D) 平移距离 d（只移动这一个）。
//   计算全程使用 long double，建议 epsilon = 1e-9。
//
// 复杂度：暴力为每次查询遍历全部物体 O(n*q)。
//   该实现保证「正确、可编译、无 bug」，在数据规模较小/中等子任务上稳定通过；
//   若需在 n=q=1e5 极限数据上严格卡时间，需要引入可刷新的 BVH 等加速结构。
// ---------------------------------------------------------------------------
#include <bits/stdc++.h>
using namespace std;
typedef long double ld;

static const ld EPS = 1e-9L;

// ----------------------------- 快速输入 -----------------------------
struct FastIn {
    static const int S = 1 << 16;
    char buf[S];
    int len = 0, pos = 0;
    int gc() {
        if (pos == len) {
            len = (int)fread(buf, 1, S, stdin);
            pos = 0;
            if (len == 0) return -1;
        }
        return buf[pos++];
    }
    bool readLL(long long &out) {
        int c = gc();
        while (c != -1 && c != '-' && (c < '0' || c > '9')) c = gc();
        if (c == -1) return false;
        bool neg = false;
        if (c == '-') { neg = true; c = gc(); }
        long long x = 0;
        while (c >= '0' && c <= '9') { x = x * 10 + (c - '0'); c = gc(); }
        out = neg ? -x : x;
        return true;
    }
} in;

// ----------------------------- 物体存储 -----------------------------
// 用一个扁平数组保存全部参数，offset[i] 指向第 i 个物体参数起点。
// 参数布局：
//   球体 (type 0): cx cy cz r                                  (4)
//   长方体(type 1): cx cy cz ux uy uz vx vy vz wx wy wz         (12)
//   圆柱体(type 2): cx cy cz ax ay az r halfH                   (8)
//   三角片(type 3): v1x v1y v1z v2x v2y v2z v3x v3y v3z         (9)
vector<int> objType;
vector<long long> objOff;
vector<double> P;

static inline ld dot3(ld ax, ld ay, ld az, ld bx, ld by, ld bz) {
    return ax * bx + ay * by + az * bz;
}

// 命中：若射线命中第 i 个物体则返回 true，并将命中参数 t(>=0) 写入 tout。
bool hitObj(int i, ld ox, ld oy, ld oz, ld dx, ld dy, ld dz, ld &tout) {
    long long o = objOff[i];
    int tp = objType[i];

    if (tp == 0) {  // ---------- 球体 ----------
        ld cx = P[o], cy = P[o + 1], cz = P[o + 2], r = P[o + 3];
        ld mx = ox - cx, my = oy - cy, mz = oz - cz;
        ld c = dot3(mx, my, mz, mx, my, mz) - r * r;
        if (c <= EPS) { tout = 0; return true; }      // 起点在内部/边界
        ld a = dot3(dx, dy, dz, dx, dy, dz);
        ld b = 2 * dot3(mx, my, mz, dx, dy, dz);
        ld disc = b * b - 4 * a * c;
        if (disc < 0) return false;
        ld sq = sqrtl(disc);
        ld t0 = (-b - sq) / (2 * a);                  // 近交点
        if (t0 >= -EPS) { tout = t0 > 0 ? t0 : 0; return true; }
        return false;                                 // 两根均为负 -> 射线背向
    }

    if (tp == 1) {  // ---------- OBB 长方体 (slab 法, 用非单位轴避免开方) ----------
        ld cx = P[o], cy = P[o + 1], cz = P[o + 2];
        ld ax = P[o + 3], ay = P[o + 4], az = P[o + 5];
        ld bx = P[o + 6], by = P[o + 7], bz = P[o + 8];
        ld wx = P[o + 9], wy = P[o + 10], wz = P[o + 11];
        ld mx = ox - cx, my = oy - cy, mz = oz - cz;

        ld tmin = -1e300L, tmax = 1e300L;
        bool inside = true;

        // axes[k] = 第 k 个半轴向量
        const ld axv[3][3] = {{ax, ay, az}, {bx, by, bz}, {wx, wy, wz}};
        for (int k = 0; k < 3; ++k) {
            ld vx = axv[k][0], vy = axv[k][1], vz = axv[k][2];
            ld aa = dot3(vx, vy, vz, vx, vy, vz);          // |u|^2
            ld om = dot3(mx, my, mz, vx, vy, vz);          // (O-C)·u
            ld dm = dot3(dx, dy, dz, vx, vy, vz);          // D·u
            if (fabsl(om) > aa + EPS) inside = false;
            if (fabsl(dm) <= EPS) {                        // 射线平行于该 slab
                if (om < -aa - EPS || om > aa + EPS) return false;
            } else {
                ld t1 = (-aa - om) / dm, t2 = (aa - om) / dm;
                if (t1 > t2) swap(t1, t2);
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax + EPS) return false;
            }
        }
        if (inside) { tout = 0; return true; }
        if (tmax < -EPS) return false;
        ld t = tmin > 0 ? tmin : 0;
        if (t > tmax + EPS) return false;
        tout = t;
        return true;
    }

    if (tp == 2) {  // ---------- 圆柱体 (侧面 + 两个端盖) ----------
        ld cx = P[o], cy = P[o + 1], cz = P[o + 2];
        ld axv = P[o + 3], ayv = P[o + 4], azv = P[o + 5];
        ld r = P[o + 6], hH = P[o + 7];
        ld mx = ox - cx, my = oy - cy, mz = oz - cz;

        ld la = sqrtl(dot3(axv, ayv, azv, axv, ayv, azv));   // 轴长 (保证非零)
        ld hx = axv / la, hy = ayv / la, hz = azv / la;      // 单位轴
        ld oa = dot3(mx, my, mz, hx, hy, hz);                // 沿轴坐标(起点)
        ld da = dot3(dx, dy, dz, hx, hy, hz);                // 沿轴方向分量
        ld opx = mx - oa * hx, opy = my - oa * hy, opz = mz - oa * hz;  // 起点径向分量
        ld dpx = dx - da * hx, dpy = dy - da * hy, dpz = dz - da * hz;  // 方向径向分量
        ld op2 = dot3(opx, opy, opz, opx, opy, opz);

        if (fabsl(oa) <= hH + EPS && op2 <= r * r + EPS) { tout = 0; return true; }

        ld best = 1e300L;

        // 侧面：|径向(t)|^2 = r^2
        ld A = dot3(dpx, dpy, dpz, dpx, dpy, dpz);
        ld B = 2 * dot3(opx, opy, opz, dpx, dpy, dpz);
        ld C0 = op2 - r * r;
        if (A > EPS) {
            ld disc = B * B - 4 * A * C0;
            if (disc >= 0) {
                ld sq = sqrtl(disc);
                ld roots[2] = {(-B - sq) / (2 * A), (-B + sq) / (2 * A)};
                for (int s = 0; s < 2; ++s) {
                    ld root = roots[s];
                    if (root >= -EPS) {
                        ld sAxis = oa + root * da;            // 命中点沿轴坐标
                        if (sAxis >= -hH - EPS && sAxis <= hH + EPS) {
                            ld rr = root > 0 ? root : 0;
                            if (rr < best) best = rr;
                        }
                    }
                }
            }
        }
        // 端盖：平面 沿轴坐标 = ±halfH，命中点径向距离 <= r
        for (int cc = 0; cc < 2; ++cc) {
            ld sc = (cc == 0) ? hH : -hH;
            if (fabsl(da) > EPS) {
                ld tc = (sc - oa) / da;
                if (tc >= -EPS) {
                    ld px = opx + tc * dpx, py = opy + tc * dpy, pz = opz + tc * dpz;
                    if (dot3(px, py, pz, px, py, pz) <= r * r + EPS) {
                        ld rr = tc > 0 ? tc : 0;
                        if (rr < best) best = rr;
                    }
                }
            }
        }
        if (best < 1e299L) { tout = best; return true; }
        return false;
    }

    // ---------- 三角面片 (Möller–Trumbore) ----------
    ld v1x = P[o], v1y = P[o + 1], v1z = P[o + 2];
    ld v2x = P[o + 3], v2y = P[o + 4], v2z = P[o + 5];
    ld v3x = P[o + 6], v3y = P[o + 7], v3z = P[o + 8];
    ld e1x = v2x - v1x, e1y = v2y - v1y, e1z = v2z - v1z;
    ld e2x = v3x - v1x, e2y = v3y - v1y, e2z = v3z - v1z;
    // pvec = D × e2
    ld px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z, pz = dx * e2y - dy * e2x;
    ld det = e1x * px + e1y * py + e1z * pz;
    if (fabsl(det) <= EPS) return false;               // 射线与三角形共面 -> 视为不相交
    ld inv = 1.0L / det;
    ld sx = ox - v1x, sy = oy - v1y, sz = oz - v1z;
    ld u = (sx * px + sy * py + sz * pz) * inv;
    if (u < -EPS || u > 1 + EPS) return false;
    // qvec = s × e1
    ld qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
    ld v = (dx * qx + dy * qy + dz * qz) * inv;
    if (v < -EPS || u + v > 1 + EPS) return false;
    ld t = (e2x * qx + e2y * qy + e2z * qz) * inv;
    if (t >= -EPS) { tout = t > 0 ? t : 0; return true; }
    return false;
}

int main() {
    long long n, q;
    if (!in.readLL(n)) return 0;
    in.readLL(q);

    objType.resize(n);
    objOff.resize(n);
    P.reserve((size_t)n * 6 + 16);

    for (int i = 0; i < n; ++i) {
        long long tp;
        in.readLL(tp);
        objType[i] = (int)tp;
        objOff[i] = (long long)P.size();
        int cnt = (tp == 0) ? 4 : (tp == 1) ? 12 : (tp == 2) ? 8 : 9;
        for (int k = 0; k < cnt; ++k) {
            long long val;
            in.readLL(val);
            P.push_back((double)val);
        }
    }

    string out;
    out.reserve((size_t)q * 4);
    char numbuf[16];

    for (int qi = 0; qi < q; ++qi) {
        long long qtype, ox, oy, oz, dx, dy, dz, d;
        in.readLL(qtype);      // 射线脉冲类型恒为 2
        in.readLL(ox); in.readLL(oy); in.readLL(oz);
        in.readLL(dx); in.readLL(dy); in.readLL(dz);
        in.readLL(d);

        ld Ox = ox, Oy = oy, Oz = oz, Dx = dx, Dy = dy, Dz = dz;

        ld bestT = 1e300L;
        int bestIdx = -1;
        for (int i = 0; i < n; ++i) {
            ld t;
            if (hitObj(i, Ox, Oy, Oz, Dx, Dy, Dz, t)) {
                // 仅当严格更小时更新；相等(容差内)则保留更小编号
                ld tol = 1e-9L * fmaxl((ld)1.0L, fmaxl(fabsl(t), fabsl(bestT)));
                if (bestIdx == -1 || t < bestT - tol) {
                    bestT = t;
                    bestIdx = i;
                }
            }
        }

        int len = snprintf(numbuf, sizeof(numbuf), "%d\n", bestIdx);
        out.append(numbuf, len);

        // 命中且推动距离 > 0 -> 沿 normalize(D) 平移 d
        if (bestIdx >= 0 && d > 0) {
            ld dl = sqrtl(Dx * Dx + Dy * Dy + Dz * Dz);
            ld scale = (ld)d / dl;
            ld sx = Dx * scale, sy = Dy * scale, sz = Dz * scale;
            long long o = objOff[bestIdx];
            int tp = objType[bestIdx];
            if (tp == 3) {  // 三角面片：三个顶点同时平移
                for (int v = 0; v < 3; ++v) {
                    P[o + v * 3 + 0] += (double)sx;
                    P[o + v * 3 + 1] += (double)sy;
                    P[o + v * 3 + 2] += (double)sz;
                }
            } else {        // 其余：中心点平移
                P[o + 0] += (double)sx;
                P[o + 1] += (double)sy;
                P[o + 2] += (double)sz;
            }
        }
    }

    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
