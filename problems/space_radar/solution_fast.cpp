// 太空探测与动态几何体查询 —— 加速版 (BVH + 命中后刷新包围盒)
// ---------------------------------------------------------------------------
// 与暴力版判定逻辑完全一致，但用 BVH 做空间剪枝：
//   - 建树一次；每次查询沿射线遍历 BVH，按包围盒入射 t 剪枝，找最近命中；
//   - 物体被推动后只重算其叶子 AABB 并自底向上刷新祖先 AABB(始终保守正确)；
//   - 平局规则：t 相等(容差内)取编号最小者；剪枝阈值带容差，避免漏掉同 t 小编号。
// 复杂度约 O((n+q)·log n)（包围盒变松时退化，但结果恒正确）。
// ---------------------------------------------------------------------------
#include <bits/stdc++.h>
using namespace std;
typedef long double ld;

static const ld EPS = 1e-9L;

struct FastIn {
    static const int S = 1 << 16;
    char buf[S];
    int len = 0, pos = 0;
    int gc() {
        if (pos == len) { len = (int)fread(buf, 1, S, stdin); pos = 0; if (len == 0) return -1; }
        return buf[pos++];
    }
    bool readLL(long long &out) {
        int c = gc();
        while (c != -1 && c != '-' && (c < '0' || c > '9')) c = gc();
        if (c == -1) return false;
        bool neg = false; if (c == '-') { neg = true; c = gc(); }
        long long x = 0; while (c >= '0' && c <= '9') { x = x * 10 + (c - '0'); c = gc(); }
        out = neg ? -x : x; return true;
    }
} in;

vector<int> objType;
vector<long long> objOff;
vector<double> P;

static inline ld dot3(ld ax, ld ay, ld az, ld bx, ld by, ld bz) { return ax * bx + ay * by + az * bz; }

// ----------------------- 精确命中判定 (与暴力版一致) -----------------------
bool hitObj(int i, ld ox, ld oy, ld oz, ld dx, ld dy, ld dz, ld &tout) {
    long long o = objOff[i];
    int tp = objType[i];
    if (tp == 0) {
        ld cx = P[o], cy = P[o + 1], cz = P[o + 2], r = P[o + 3];
        ld mx = ox - cx, my = oy - cy, mz = oz - cz;
        ld c = dot3(mx, my, mz, mx, my, mz) - r * r;
        if (c <= EPS) { tout = 0; return true; }
        ld a = dot3(dx, dy, dz, dx, dy, dz);
        ld b = 2 * dot3(mx, my, mz, dx, dy, dz);
        ld disc = b * b - 4 * a * c;
        if (disc < 0) return false;
        ld sq = sqrtl(disc);
        ld t0 = (-b - sq) / (2 * a);
        if (t0 >= -EPS) { tout = t0 > 0 ? t0 : 0; return true; }
        return false;
    }
    if (tp == 1) {
        ld cx = P[o], cy = P[o + 1], cz = P[o + 2];
        ld ax = P[o + 3], ay = P[o + 4], az = P[o + 5];
        ld bx = P[o + 6], by = P[o + 7], bz = P[o + 8];
        ld wx = P[o + 9], wy = P[o + 10], wz = P[o + 11];
        ld mx = ox - cx, my = oy - cy, mz = oz - cz;
        ld tmin = -1e300L, tmax = 1e300L;
        bool inside = true;
        const ld axv[3][3] = {{ax, ay, az}, {bx, by, bz}, {wx, wy, wz}};
        for (int k = 0; k < 3; ++k) {
            ld vx = axv[k][0], vy = axv[k][1], vz = axv[k][2];
            ld aa = dot3(vx, vy, vz, vx, vy, vz);
            ld om = dot3(mx, my, mz, vx, vy, vz);
            ld dm = dot3(dx, dy, dz, vx, vy, vz);
            if (fabsl(om) > aa + EPS) inside = false;
            if (fabsl(dm) <= EPS) { if (om < -aa - EPS || om > aa + EPS) return false; }
            else {
                ld t1 = (-aa - om) / dm, t2 = (aa - om) / dm; if (t1 > t2) swap(t1, t2);
                if (t1 > tmin) tmin = t1; if (t2 < tmax) tmax = t2;
                if (tmin > tmax + EPS) return false;
            }
        }
        if (inside) { tout = 0; return true; }
        if (tmax < -EPS) return false;
        ld t = tmin > 0 ? tmin : 0;
        if (t > tmax + EPS) return false;
        tout = t; return true;
    }
    if (tp == 2) {
        ld cx = P[o], cy = P[o + 1], cz = P[o + 2];
        ld axv = P[o + 3], ayv = P[o + 4], azv = P[o + 5];
        ld r = P[o + 6], hH = P[o + 7];
        ld mx = ox - cx, my = oy - cy, mz = oz - cz;
        ld la = sqrtl(dot3(axv, ayv, azv, axv, ayv, azv));
        ld hx = axv / la, hy = ayv / la, hz = azv / la;
        ld oa = dot3(mx, my, mz, hx, hy, hz);
        ld da = dot3(dx, dy, dz, hx, hy, hz);
        ld opx = mx - oa * hx, opy = my - oa * hy, opz = mz - oa * hz;
        ld dpx = dx - da * hx, dpy = dy - da * hy, dpz = dz - da * hz;
        ld op2 = dot3(opx, opy, opz, opx, opy, opz);
        if (fabsl(oa) <= hH + EPS && op2 <= r * r + EPS) { tout = 0; return true; }
        ld best = 1e300L;
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
                        ld sAxis = oa + root * da;
                        if (sAxis >= -hH - EPS && sAxis <= hH + EPS) { ld rr = root > 0 ? root : 0; if (rr < best) best = rr; }
                    }
                }
            }
        }
        for (int cc = 0; cc < 2; ++cc) {
            ld sc = (cc == 0) ? hH : -hH;
            if (fabsl(da) > EPS) {
                ld tc = (sc - oa) / da;
                if (tc >= -EPS) {
                    ld px = opx + tc * dpx, py = opy + tc * dpy, pz = opz + tc * dpz;
                    if (dot3(px, py, pz, px, py, pz) <= r * r + EPS) { ld rr = tc > 0 ? tc : 0; if (rr < best) best = rr; }
                }
            }
        }
        if (best < 1e299L) { tout = best; return true; }
        return false;
    }
    ld v1x = P[o], v1y = P[o + 1], v1z = P[o + 2];
    ld v2x = P[o + 3], v2y = P[o + 4], v2z = P[o + 5];
    ld v3x = P[o + 6], v3y = P[o + 7], v3z = P[o + 8];
    ld e1x = v2x - v1x, e1y = v2y - v1y, e1z = v2z - v1z;
    ld e2x = v3x - v1x, e2y = v3y - v1y, e2z = v3z - v1z;
    ld px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z, pz = dx * e2y - dy * e2x;
    ld det = e1x * px + e1y * py + e1z * pz;
    if (fabsl(det) <= EPS) return false;
    ld inv = 1.0L / det;
    ld sx = ox - v1x, sy = oy - v1y, sz = oz - v1z;
    ld u = (sx * px + sy * py + sz * pz) * inv;
    if (u < -EPS || u > 1 + EPS) return false;
    ld qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
    ld v = (dx * qx + dy * qy + dz * qz) * inv;
    if (v < -EPS || u + v > 1 + EPS) return false;
    ld t = (e2x * qx + e2y * qy + e2z * qz) * inv;
    if (t >= -EPS) { tout = t > 0 ? t : 0; return true; }
    return false;
}

// ----------------------- 物体 AABB -----------------------
void objAABB(int i, double lo[3], double hi[3]) {
    long long o = objOff[i];
    int tp = objType[i];
    if (tp == 0) {
        double cx = P[o], cy = P[o + 1], cz = P[o + 2], r = P[o + 3];
        lo[0] = cx - r; hi[0] = cx + r; lo[1] = cy - r; hi[1] = cy + r; lo[2] = cz - r; hi[2] = cz + r;
    } else if (tp == 1) {
        double cx = P[o], cy = P[o + 1], cz = P[o + 2];
        double ex = fabs(P[o + 3]) + fabs(P[o + 6]) + fabs(P[o + 9]);
        double ey = fabs(P[o + 4]) + fabs(P[o + 7]) + fabs(P[o + 10]);
        double ez = fabs(P[o + 5]) + fabs(P[o + 8]) + fabs(P[o + 11]);
        lo[0] = cx - ex; hi[0] = cx + ex; lo[1] = cy - ey; hi[1] = cy + ey; lo[2] = cz - ez; hi[2] = cz + ez;
    } else if (tp == 2) {
        double cx = P[o], cy = P[o + 1], cz = P[o + 2];
        double ax = P[o + 3], ay = P[o + 4], az = P[o + 5], r = P[o + 6], hH = P[o + 7];
        double la = sqrt(ax * ax + ay * ay + az * az);
        double hx = ax / la, hy = ay / la, hz = az / la;
        double ex = hH * fabs(hx) + r * sqrt(max(0.0, 1.0 - hx * hx));
        double ey = hH * fabs(hy) + r * sqrt(max(0.0, 1.0 - hy * hy));
        double ez = hH * fabs(hz) + r * sqrt(max(0.0, 1.0 - hz * hz));
        lo[0] = cx - ex; hi[0] = cx + ex; lo[1] = cy - ey; hi[1] = cy + ey; lo[2] = cz - ez; hi[2] = cz + ez;
    } else {
        for (int a = 0; a < 3; ++a) {
            double v0 = P[o + a], v1 = P[o + 3 + a], v2 = P[o + 6 + a];
            lo[a] = min({v0, v1, v2}); hi[a] = max({v0, v1, v2});
        }
    }
    // 给保守容差，规避浮点边界
    for (int a = 0; a < 3; ++a) { lo[a] -= 1e-6; hi[a] += 1e-6; }
}

// ----------------------- BVH -----------------------
struct Node { double lo[3], hi[3]; int left, right, parent, obj; };
vector<Node> nodes;
vector<int> leafOf;   // objIdx -> node id
vector<int> prim;     // 物体索引列表(建树时按区间划分)

int buildBVH(int l, int r, int parent) {
    int id = (int)nodes.size();
    nodes.push_back(Node());
    nodes[id].parent = parent;
    double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    for (int k = l; k < r; ++k) {
        double blo[3], bhi[3]; objAABB(prim[k], blo, bhi);
        for (int a = 0; a < 3; ++a) { lo[a] = min(lo[a], blo[a]); hi[a] = max(hi[a], bhi[a]); }
    }
    for (int a = 0; a < 3; ++a) { nodes[id].lo[a] = lo[a]; nodes[id].hi[a] = hi[a]; }
    if (r - l == 1) {
        nodes[id].left = nodes[id].right = -1;
        nodes[id].obj = prim[l];
        leafOf[prim[l]] = id;
        return id;
    }
    // 选最长轴, 按质心中位数划分
    double cen[3] = {1e300, 1e300, 1e300}, cex[3] = {-1e300, -1e300, -1e300};
    for (int k = l; k < r; ++k) {
        double blo[3], bhi[3]; objAABB(prim[k], blo, bhi);
        for (int a = 0; a < 3; ++a) { double c = (blo[a] + bhi[a]) * 0.5; cen[a] = min(cen[a], c); cex[a] = max(cex[a], c); }
    }
    int axis = 0; double best = cex[0] - cen[0];
    for (int a = 1; a < 3; ++a) if (cex[a] - cen[a] > best) { best = cex[a] - cen[a]; axis = a; }
    int mid = (l + r) / 2;
    if (best < 1e-12) {
        // 质心几乎相同 -> 按下标对半分, 保证深度有界
    } else {
        nth_element(prim.begin() + l, prim.begin() + mid, prim.begin() + r,
                    [&](int x, int y) {
                        double xl[3], xh[3], yl[3], yh[3]; objAABB(x, xl, xh); objAABB(y, yl, yh);
                        return (xl[axis] + xh[axis]) < (yl[axis] + yh[axis]);
                    });
    }
    nodes[id].obj = -1;
    int lc = buildBVH(l, mid, id);
    int rc = buildBVH(mid, r, id);
    nodes[id].left = lc; nodes[id].right = rc;
    return id;
}

void refit(int objIdx) {
    int id = leafOf[objIdx];
    double lo[3], hi[3]; objAABB(objIdx, lo, hi);
    for (int a = 0; a < 3; ++a) { nodes[id].lo[a] = lo[a]; nodes[id].hi[a] = hi[a]; }
    id = nodes[id].parent;
    while (id != -1) {
        int lc = nodes[id].left, rc = nodes[id].right;
        bool changed = false;
        for (int a = 0; a < 3; ++a) {
            double nlo = min(nodes[lc].lo[a], nodes[rc].lo[a]);
            double nhi = max(nodes[lc].hi[a], nodes[rc].hi[a]);
            if (nlo != nodes[id].lo[a] || nhi != nodes[id].hi[a]) changed = true;
            nodes[id].lo[a] = nlo; nodes[id].hi[a] = nhi;
        }
        if (!changed) break;
        id = nodes[id].parent;
    }
}

// 射线-AABB: 返回是否相交(t>=0), tEnter 为入射参数(起点在盒内则 0)
inline bool rayAABB(const Node &nd, ld ox, ld oy, ld oz, ld dx, ld dy, ld dz, ld &tEnter) {
    ld O[3] = {ox, oy, oz}, D[3] = {dx, dy, dz};
    ld tmin = 0, tmax = 1e300L;
    for (int a = 0; a < 3; ++a) {
        if (fabsl(D[a]) <= EPS) {
            if (O[a] < (ld)nd.lo[a] - EPS || O[a] > (ld)nd.hi[a] + EPS) return false;
        } else {
            ld inv = 1.0L / D[a];
            ld ta = ((ld)nd.lo[a] - O[a]) * inv;
            ld tb = ((ld)nd.hi[a] - O[a]) * inv;
            if (ta > tb) swap(ta, tb);
            if (ta > tmin) tmin = ta;
            if (tb < tmax) tmax = tb;
            if (tmin > tmax + EPS) return false;
        }
    }
    if (tmax < -EPS) return false;
    tEnter = tmin;
    return true;
}

int rootNode;

void query(ld ox, ld oy, ld oz, ld dx, ld dy, ld dz, ld &bestT, int &bestIdx) {
    bestT = 1e300L; bestIdx = -1;
    static vector<int> stk; stk.clear();
    ld rootEnter;
    if (!rayAABB(nodes[rootNode], ox, oy, oz, dx, dy, dz, rootEnter)) return;
    stk.push_back(rootNode);
    while (!stk.empty()) {
        int id = stk.back(); stk.pop_back();
        const Node &nd = nodes[id];
        ld tol = 1e-9L * fmaxl((ld)1.0L, fabsl(bestT));
        if (bestIdx != -1) {
            ld e;
            if (!rayAABB(nd, ox, oy, oz, dx, dy, dz, e)) continue;
            if (e > bestT + tol) continue;  // 整个盒都比当前最优更远 -> 剪枝
        }
        if (nd.obj >= 0) {  // 叶子
            ld t;
            if (hitObj(nd.obj, ox, oy, oz, dx, dy, dz, t)) {
                ld tt = 1e-9L * fmaxl((ld)1.0L, fmaxl(fabsl(t), fabsl(bestT)));
                if (bestIdx == -1 || t < bestT - tt) { bestT = t; bestIdx = nd.obj; }
                else if (fabsl(t - bestT) <= tt && nd.obj < bestIdx) { bestIdx = nd.obj; }
            }
            continue;
        }
        // 先入栈较远者, 后处理较近者(后进先出)
        int lc = nd.left, rc = nd.right;
        ld el, er; bool hl = rayAABB(nodes[lc], ox, oy, oz, dx, dy, dz, el);
        bool hr = rayAABB(nodes[rc], ox, oy, oz, dx, dy, dz, er);
        if (hl && hr) {
            if (el <= er) { stk.push_back(rc); stk.push_back(lc); }
            else { stk.push_back(lc); stk.push_back(rc); }
        } else if (hl) stk.push_back(lc);
        else if (hr) stk.push_back(rc);
    }
}

int main() {
    long long n, q;
    if (!in.readLL(n)) return 0;
    in.readLL(q);
    objType.resize(n); objOff.resize(n); P.reserve((size_t)n * 6 + 16);
    for (int i = 0; i < n; ++i) {
        long long tp; in.readLL(tp);
        objType[i] = (int)tp; objOff[i] = (long long)P.size();
        int cnt = (tp == 0) ? 4 : (tp == 1) ? 12 : (tp == 2) ? 8 : 9;
        for (int k = 0; k < cnt; ++k) { long long val; in.readLL(val); P.push_back((double)val); }
    }

    leafOf.assign(n, -1);
    prim.resize(n);
    for (int i = 0; i < n; ++i) prim[i] = i;
    nodes.reserve(2 * n + 1);
    rootNode = buildBVH(0, (int)n, -1);

    string out; out.reserve((size_t)q * 4);
    char numbuf[16];
    for (int qi = 0; qi < q; ++qi) {
        long long qtype, ox, oy, oz, dx, dy, dz, d;
        in.readLL(qtype);
        in.readLL(ox); in.readLL(oy); in.readLL(oz);
        in.readLL(dx); in.readLL(dy); in.readLL(dz); in.readLL(d);
        ld bestT; int bestIdx;
        query(ox, oy, oz, dx, dy, dz, bestT, bestIdx);
        int len = snprintf(numbuf, sizeof(numbuf), "%d\n", bestIdx);
        out.append(numbuf, len);
        if (bestIdx >= 0 && d > 0) {
            ld dl = sqrtl((ld)dx * dx + (ld)dy * dy + (ld)dz * dz);
            ld scale = (ld)d / dl;
            ld sx = (ld)dx * scale, sy = (ld)dy * scale, sz = (ld)dz * scale;
            long long o = objOff[bestIdx];
            int tp = objType[bestIdx];
            if (tp == 3) {
                for (int v = 0; v < 3; ++v) { P[o + v * 3] += (double)sx; P[o + v * 3 + 1] += (double)sy; P[o + v * 3 + 2] += (double)sz; }
            } else { P[o] += (double)sx; P[o + 1] += (double)sy; P[o + 2] += (double)sz; }
            refit(bestIdx);
        }
    }
    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
