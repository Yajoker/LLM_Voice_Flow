// 太空探测与动态几何体查询 —— 加速版 (BVH + 命中后刷新包围盒)
// ---------------------------------------------------------------------------
// 判定逻辑与暴力版完全一致, 用 BVH 做空间剪枝, 并针对超时做了系统性优化:
//   1. 全程 double + 预计算射线方向倒数, 降低常数;
//   2. 叶子分桶 (一个叶子多个物体), 减少内部节点与射线-盒测试量;
//   3. 建树后将物体参数按叶子(position)顺序重排为【连续内存】, 叶子内逐物体读取
//      变为顺序访问, 大幅减少 cache miss —— 这是稠密/重叠场景的主要加速来源(以内存换时间);
//   4. 两阶段查询: 先找包含起点(t=0)的最小编号物体, 命中则直接返回; 否则求最近命中;
//   5. 球/圆柱二次判别式带相切容差, 既符合"闭集边界算命中", 又避免 sqrt 放大噪声.
//   6. 物体被推动后只重算其叶子 AABB 并自底向上刷新祖先(保守正确).
// 标准: C++17.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using ld = double;  // 题面建议 epsilon=1e-9 且精度歧义极小, 按 double 设计(已与 long double 对拍一致)

namespace {

constexpr ld kEps = 1e-9;
constexpr double kEpsB = 1e-6;
constexpr ld kInf = 1e300;
constexpr double kInfB = 1e300;
constexpr int kLeafSize = 16;  // 叶子分桶大小

// ----------------------------- 快速输入 -----------------------------
class FastInput {
public:
    bool readLL(long long &out) {
        int c = gc();
        while (c != -1 && c != '-' && (c < '0' || c > '9')) {
            c = gc();
        }
        if (c == -1) {
            return false;
        }
        bool neg = false;
        if (c == '-') {
            neg = true;
            c = gc();
        }
        long long x = 0;
        while (c >= '0' && c <= '9') {
            x = x * 10 + (c - '0');
            c = gc();
        }
        out = neg ? -x : x;
        return true;
    }

private:
    static constexpr int kBufSize = 1 << 16;
    char buf_[kBufSize];
    int len_ = 0;
    int pos_ = 0;

    int gc() {
        if (pos_ == len_) {
            len_ = static_cast<int>(std::fread(buf_, 1, kBufSize, stdin));
            pos_ = 0;
            if (len_ == 0) {
                return -1;
            }
        }
        return buf_[pos_++];
    }
};

inline int paramCount(int tp) {
    return (tp == 0) ? 4 : (tp == 1) ? 12 : (tp == 2) ? 8 : 9;
}

inline ld dot3(ld ax, ld ay, ld az, ld bx, ld by, ld bz) {
    return ax * bx + ay * by + az * bz;
}

// ---------------------- 射线 vs 单个物体 (p 指向该物体参数) ----------------------
// 命中返回 true 并写入 t(>=0). 与暴力版完全一致.
// tMax: 调用方当前可接受的最大 t(含平局余量). 仅用于提前否决明显更远的物体(跳过 sqrt),
//       不影响结果正确性(只否决超出余量的更远者).
bool hitObj(int tp, const double *p, ld ox, ld oy, ld oz, ld dx, ld dy, ld dz, ld tMax, ld &tout) {
    if (tp == 0) {  // ---------- 球体 ----------
        const ld cx = p[0], cy = p[1], cz = p[2], r = p[3];
        const ld mx = ox - cx, my = oy - cy, mz = oz - cz;
        const ld c = dot3(mx, my, mz, mx, my, mz) - r * r;
        if (c <= kEps) {
            tout = 0;
            return true;
        }
        const ld a = dot3(dx, dy, dz, dx, dy, dz);
        const ld b = 2 * dot3(mx, my, mz, dx, dy, dz);
        ld disc = b * b - 4 * a * c;
        const ld dlim = 1e-9 * (std::fabs(b * b) + std::fabs(4 * a * c) + 1);
        if (disc < -dlim) {
            return false;
        }
        if (disc < dlim) {  // |disc|<=容差 -> 相切, 避免 sqrt 放大噪声
            disc = 0;
        }
        // 提前否决: 近根 t0=(-b-√disc)/(2a) > tMax 时无需开方 (a=D·D>0)
        const ld L = -b - 2 * a * tMax;
        if (L > 0 && L * L > disc) {
            return false;
        }
        const ld t0 = (-b - std::sqrt(disc)) / (2 * a);
        if (t0 >= -kEps) {
            tout = t0 > 0 ? t0 : 0;
            return true;
        }
        return false;
    }

    if (tp == 1) {  // ---------- OBB 长方体 (slab 法, 非单位轴避免开方) ----------
        const ld cx = p[0], cy = p[1], cz = p[2];
        const ld mx = ox - cx, my = oy - cy, mz = oz - cz;
        const ld axis[3][3] = {
            {p[3], p[4], p[5]}, {p[6], p[7], p[8]}, {p[9], p[10], p[11]}};

        ld tmin = -kInf, tmax = kInf;
        bool inside = true;
        for (int k = 0; k < 3; ++k) {
            const ld vx = axis[k][0], vy = axis[k][1], vz = axis[k][2];
            const ld aa = dot3(vx, vy, vz, vx, vy, vz);
            const ld om = dot3(mx, my, mz, vx, vy, vz);
            const ld dm = dot3(dx, dy, dz, vx, vy, vz);
            if (std::fabs(om) > aa + kEps) {
                inside = false;
            }
            if (std::fabs(dm) <= kEps) {
                if (om < -aa - kEps || om > aa + kEps) {
                    return false;
                }
            } else {
                ld t1 = (-aa - om) / dm;
                ld t2 = (aa - om) / dm;
                if (t1 > t2) {
                    std::swap(t1, t2);
                }
                if (t1 > tmin) {
                    tmin = t1;
                }
                if (t2 < tmax) {
                    tmax = t2;
                }
                if (tmin > tmax + kEps) {
                    return false;
                }
            }
        }
        if (inside) {
            tout = 0;
            return true;
        }
        if (tmax < -kEps) {
            return false;
        }
        const ld t = tmin > 0 ? tmin : 0;
        if (t > tmax + kEps) {
            return false;
        }
        tout = t;
        return true;
    }

    if (tp == 2) {  // ---------- 圆柱体 (侧面 + 两端盖) ----------
        const ld cx = p[0], cy = p[1], cz = p[2];
        const ld axv = p[3], ayv = p[4], azv = p[5];
        const ld r = p[6], halfH = p[7];
        const ld mx = ox - cx, my = oy - cy, mz = oz - cz;

        const ld la = std::sqrt(dot3(axv, ayv, azv, axv, ayv, azv));
        const ld hx = axv / la, hy = ayv / la, hz = azv / la;
        const ld oa = dot3(mx, my, mz, hx, hy, hz);
        const ld da = dot3(dx, dy, dz, hx, hy, hz);
        const ld opx = mx - oa * hx, opy = my - oa * hy, opz = mz - oa * hz;
        const ld dpx = dx - da * hx, dpy = dy - da * hy, dpz = dz - da * hz;
        const ld op2 = dot3(opx, opy, opz, opx, opy, opz);

        if (std::fabs(oa) <= halfH + kEps && op2 <= r * r + kEps) {
            tout = 0;
            return true;
        }

        ld best = kInf;
        const ld a = dot3(dpx, dpy, dpz, dpx, dpy, dpz);
        const ld b = 2 * dot3(opx, opy, opz, dpx, dpy, dpz);
        const ld c0 = op2 - r * r;
        if (a > kEps) {
            ld disc = b * b - 4 * a * c0;
            const ld dlim = 1e-9 * (std::fabs(b * b) + std::fabs(4 * a * c0) + 1);
            if (disc >= -dlim) {
                if (disc < dlim) {  // 相切
                    disc = 0;
                }
                const ld sq = std::sqrt(disc);
                const ld roots[2] = {(-b - sq) / (2 * a), (-b + sq) / (2 * a)};
                for (int s = 0; s < 2; ++s) {
                    const ld root = roots[s];
                    if (root >= -kEps) {
                        const ld sAxis = oa + root * da;
                        if (sAxis >= -halfH - kEps && sAxis <= halfH + kEps) {
                            const ld rr = root > 0 ? root : 0;
                            if (rr < best) {
                                best = rr;
                            }
                        }
                    }
                }
            }
        }
        for (int cap = 0; cap < 2; ++cap) {
            const ld sc = (cap == 0) ? halfH : -halfH;
            if (std::fabs(da) > kEps) {
                const ld tc = (sc - oa) / da;
                if (tc >= -kEps) {
                    const ld px = opx + tc * dpx;
                    const ld py = opy + tc * dpy;
                    const ld pz = opz + tc * dpz;
                    if (dot3(px, py, pz, px, py, pz) <= r * r + kEps) {
                        const ld rr = tc > 0 ? tc : 0;
                        if (rr < best) {
                            best = rr;
                        }
                    }
                }
            }
        }
        if (best < kInf * 0.5) {
            tout = best;
            return true;
        }
        return false;
    }

    // ---------- 三角面片 (Möller–Trumbore) ----------
    const ld v1x = p[0], v1y = p[1], v1z = p[2];
    const ld v2x = p[3], v2y = p[4], v2z = p[5];
    const ld v3x = p[6], v3y = p[7], v3z = p[8];
    const ld e1x = v2x - v1x, e1y = v2y - v1y, e1z = v2z - v1z;
    const ld e2x = v3x - v1x, e2y = v3y - v1y, e2z = v3z - v1z;
    const ld px = dy * e2z - dz * e2y;
    const ld py = dz * e2x - dx * e2z;
    const ld pz = dx * e2y - dy * e2x;
    const ld det = e1x * px + e1y * py + e1z * pz;
    if (std::fabs(det) <= kEps) {
        return false;
    }
    const ld inv = 1.0 / det;
    const ld sx = ox - v1x, sy = oy - v1y, sz = oz - v1z;
    const ld u = (sx * px + sy * py + sz * pz) * inv;
    if (u < -kEps || u > 1 + kEps) {
        return false;
    }
    const ld qx = sy * e1z - sz * e1y;
    const ld qy = sz * e1x - sx * e1z;
    const ld qz = sx * e1y - sy * e1x;
    const ld v = (dx * qx + dy * qy + dz * qz) * inv;
    if (v < -kEps || u + v > 1 + kEps) {
        return false;
    }
    const ld t = (e2x * qx + e2y * qy + e2z * qz) * inv;
    if (t >= -kEps) {
        tout = t > 0 ? t : 0;
        return true;
    }
    return false;
}

// 点包含测试 (与 hitObj 的"起点在内部/边界"判定完全一致). 三角片不在此处理.
inline bool containsPoint(int tp, const double *p, ld px, ld py, ld pz) {
    const ld mx = px - p[0], my = py - p[1], mz = pz - p[2];
    if (tp == 0) {  // 球体
        return dot3(mx, my, mz, mx, my, mz) <= p[3] * p[3] + kEps;
    }
    if (tp == 1) {  // OBB
        for (int k = 0; k < 3; ++k) {
            const ld vx = p[3 + k * 3], vy = p[4 + k * 3], vz = p[5 + k * 3];
            const ld aa = dot3(vx, vy, vz, vx, vy, vz);
            if (std::fabs(dot3(mx, my, mz, vx, vy, vz)) > aa + kEps) {
                return false;
            }
        }
        return true;
    }
    // 圆柱体
    const ld axv = p[3], ayv = p[4], azv = p[5], r = p[6], halfH = p[7];
    const ld la = std::sqrt(dot3(axv, ayv, azv, axv, ayv, azv));
    const ld hx = axv / la, hy = ayv / la, hz = azv / la;
    const ld oa = dot3(mx, my, mz, hx, hy, hz);
    if (std::fabs(oa) > halfH + kEps) {
        return false;
    }
    const ld opx = mx - oa * hx, opy = my - oa * hy, opz = mz - oa * hz;
    return dot3(opx, opy, opz, opx, opy, opz) <= r * r + kEps;
}

// 物体 AABB (向外略扩, 保守)
void aabbOf(int tp, const double *p, double lo[3], double hi[3]) {
    if (tp == 0) {
        const double cx = p[0], cy = p[1], cz = p[2], r = p[3];
        lo[0] = cx - r; hi[0] = cx + r;
        lo[1] = cy - r; hi[1] = cy + r;
        lo[2] = cz - r; hi[2] = cz + r;
    } else if (tp == 1) {
        const double cx = p[0], cy = p[1], cz = p[2];
        const double ex = std::fabs(p[3]) + std::fabs(p[6]) + std::fabs(p[9]);
        const double ey = std::fabs(p[4]) + std::fabs(p[7]) + std::fabs(p[10]);
        const double ez = std::fabs(p[5]) + std::fabs(p[8]) + std::fabs(p[11]);
        lo[0] = cx - ex; hi[0] = cx + ex;
        lo[1] = cy - ey; hi[1] = cy + ey;
        lo[2] = cz - ez; hi[2] = cz + ez;
    } else if (tp == 2) {
        const double cx = p[0], cy = p[1], cz = p[2];
        const double ax = p[3], ay = p[4], az = p[5], r = p[6], halfH = p[7];
        const double la = std::sqrt(ax * ax + ay * ay + az * az);
        const double hx = ax / la, hy = ay / la, hz = az / la;
        const double ex = halfH * std::fabs(hx) + r * std::sqrt(std::max(0.0, 1.0 - hx * hx));
        const double ey = halfH * std::fabs(hy) + r * std::sqrt(std::max(0.0, 1.0 - hy * hy));
        const double ez = halfH * std::fabs(hz) + r * std::sqrt(std::max(0.0, 1.0 - hz * hz));
        lo[0] = cx - ex; hi[0] = cx + ex;
        lo[1] = cy - ey; hi[1] = cy + ey;
        lo[2] = cz - ez; hi[2] = cz + ez;
    } else {
        for (int a = 0; a < 3; ++a) {
            const double w0 = p[a], w1 = p[3 + a], w2 = p[6 + a];
            lo[a] = std::min({w0, w1, w2});
            hi[a] = std::max({w0, w1, w2});
        }
    }
    for (int a = 0; a < 3; ++a) {
        lo[a] -= kEpsB;
        hi[a] += kEpsB;
    }
}

// ----------------------------- 全局数据 -----------------------------
// 建树阶段(原始顺序):
std::vector<int> g_type0;
std::vector<long long> g_off0;
std::vector<double> g_param0;

// 重排后(position 顺序, 连续内存):
std::vector<double> P;      // 物体参数, 按 position 顺序连续存放
std::vector<int> pType;     // position -> 类型
std::vector<int> pOff;      // position -> P 中起点
std::vector<int> pOrig;     // position -> 原始编号(用于输出与平局)

struct Node {
    double lo[3];
    double hi[3];
    int left;    // 内部节点左孩子; 叶子为 -1
    int right;
    int parent;
    int start;   // 叶子: position 区间起点
    int count;   // 叶子: 物体数; 内部节点为 0
    int minIdx;  // 子树内最小原始编号
};

std::vector<Node> g_nodes;
std::vector<int> g_leafOf;  // position -> 所属叶子节点
std::vector<int> g_prim;    // 建树用: position -> 原始编号
int g_root = -1;

// 建树期 AABB (读 g_param0)
inline void aabbOrig(int origIdx, double lo[3], double hi[3]) {
    aabbOf(g_type0[origIdx], &g_param0[g_off0[origIdx]], lo, hi);
}

int buildBVH(int l, int r, int parent) {
    const int id = static_cast<int>(g_nodes.size());
    g_nodes.push_back(Node());
    g_nodes[id].parent = parent;

    double lo[3] = {kInfB, kInfB, kInfB};
    double hi[3] = {-kInfB, -kInfB, -kInfB};
    int minIdx = INT32_MAX;
    for (int k = l; k < r; ++k) {
        double blo[3], bhi[3];
        aabbOrig(g_prim[k], blo, bhi);
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], blo[a]);
            hi[a] = std::max(hi[a], bhi[a]);
        }
        minIdx = std::min(minIdx, g_prim[k]);
    }
    for (int a = 0; a < 3; ++a) {
        g_nodes[id].lo[a] = lo[a];
        g_nodes[id].hi[a] = hi[a];
    }
    g_nodes[id].minIdx = minIdx;

    if (r - l <= kLeafSize) {
        g_nodes[id].left = -1;
        g_nodes[id].right = -1;
        g_nodes[id].start = l;
        g_nodes[id].count = r - l;
        return id;
    }

    double cenLo[3] = {kInfB, kInfB, kInfB};
    double cenHi[3] = {-kInfB, -kInfB, -kInfB};
    for (int k = l; k < r; ++k) {
        double blo[3], bhi[3];
        aabbOrig(g_prim[k], blo, bhi);
        for (int a = 0; a < 3; ++a) {
            const double c = (blo[a] + bhi[a]) * 0.5;
            cenLo[a] = std::min(cenLo[a], c);
            cenHi[a] = std::max(cenHi[a], c);
        }
    }
    int axis = 0;
    double bestSpan = cenHi[0] - cenLo[0];
    for (int a = 1; a < 3; ++a) {
        if (cenHi[a] - cenLo[a] > bestSpan) {
            bestSpan = cenHi[a] - cenLo[a];
            axis = a;
        }
    }
    const int mid = (l + r) / 2;
    if (bestSpan >= 1e-12) {
        std::nth_element(g_prim.begin() + l, g_prim.begin() + mid, g_prim.begin() + r,
                         [&](int x, int y) {
                             double xl[3], xh[3], yl[3], yh[3];
                             aabbOrig(x, xl, xh);
                             aabbOrig(y, yl, yh);
                             return (xl[axis] + xh[axis]) < (yl[axis] + yh[axis]);
                         });
    }

    g_nodes[id].count = 0;
    const int lc = buildBVH(l, mid, id);
    const int rc = buildBVH(mid, r, id);
    g_nodes[id].left = lc;
    g_nodes[id].right = rc;
    return id;
}

// 推动后刷新叶子盒(读 P)并向上传播
void refit(int posIdx) {
    int id = g_leafOf[posIdx];
    double lo[3] = {kInfB, kInfB, kInfB};
    double hi[3] = {-kInfB, -kInfB, -kInfB};
    const int st = g_nodes[id].start;
    const int cnt = g_nodes[id].count;
    for (int k = st; k < st + cnt; ++k) {
        double blo[3], bhi[3];
        aabbOf(pType[k], &P[pOff[k]], blo, bhi);
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], blo[a]);
            hi[a] = std::max(hi[a], bhi[a]);
        }
    }
    for (int a = 0; a < 3; ++a) {
        g_nodes[id].lo[a] = lo[a];
        g_nodes[id].hi[a] = hi[a];
    }
    id = g_nodes[id].parent;
    while (id != -1) {
        const int lc = g_nodes[id].left;
        const int rc = g_nodes[id].right;
        bool changed = false;
        for (int a = 0; a < 3; ++a) {
            const double nlo = std::min(g_nodes[lc].lo[a], g_nodes[rc].lo[a]);
            const double nhi = std::max(g_nodes[lc].hi[a], g_nodes[rc].hi[a]);
            if (nlo != g_nodes[id].lo[a] || nhi != g_nodes[id].hi[a]) {
                changed = true;
            }
            g_nodes[id].lo[a] = nlo;
            g_nodes[id].hi[a] = nhi;
        }
        if (!changed) {
            break;
        }
        id = g_nodes[id].parent;
    }
}

// 预计算的射线数据
struct Ray {
    double o[3];
    double inv[3];
    bool parallel[3];
};

inline bool rayAABB(const Node &nd, const Ray &ray, double &enter) {
    double tmin = 0.0, tmax = kInfB;
    for (int a = 0; a < 3; ++a) {
        if (ray.parallel[a]) {
            if (ray.o[a] < nd.lo[a] - kEpsB || ray.o[a] > nd.hi[a] + kEpsB) {
                return false;
            }
        } else {
            double ta = (nd.lo[a] - ray.o[a]) * ray.inv[a];
            double tb = (nd.hi[a] - ray.o[a]) * ray.inv[a];
            if (ta > tb) {
                std::swap(ta, tb);
            }
            if (ta > tmin) {
                tmin = ta;
            }
            if (tb < tmax) {
                tmax = tb;
            }
            if (tmin > tmax + kEpsB) {
                return false;
            }
        }
    }
    if (tmax < -kEpsB) {
        return false;
    }
    enter = tmin;
    return true;
}

inline bool originInBox(const Node &nd, const Ray &ray) {
    for (int a = 0; a < 3; ++a) {
        if (ray.o[a] < nd.lo[a] - kEpsB || ray.o[a] > nd.hi[a] + kEpsB) {
            return false;
        }
    }
    return true;
}

std::vector<int> g_stkId;
std::vector<double> g_stkT;

// 阶段 B: 返回包含起点(t<=kEps)且原始编号最小的物体的 position; 无则 -1.
int minIndexContaining(const Ray &ray, ld ox, ld oy, ld oz, ld dx, ld dy, ld dz) {
    int bestOrig = INT32_MAX;
    int bestPos = -1;
    if (g_nodes[g_root].minIdx >= bestOrig || !originInBox(g_nodes[g_root], ray)) {
        return -1;
    }
    int sp = 0;
    g_stkId[sp++] = g_root;

    while (sp > 0) {
        const int id = g_stkId[--sp];
        const Node &nd = g_nodes[id];
        if (nd.minIdx >= bestOrig) {
            continue;
        }
        if (nd.left == -1) {  // 叶子
            const int st = nd.start, cnt = nd.count;
            for (int k = st; k < st + cnt; ++k) {
                const int oi = pOrig[k];
                if (oi >= bestOrig) {
                    continue;
                }
                bool in;
                if (pType[k] == 3) {  // 三角片: 用 hitObj 保持与基准完全一致
                    ld t;
                    in = hitObj(3, &P[pOff[k]], ox, oy, oz, dx, dy, dz, 1e-6, t) && t <= kEps;
                } else {  // 球/OBB/圆柱: 廉价点包含测试
                    in = containsPoint(pType[k], &P[pOff[k]], ox, oy, oz);
                }
                if (in) {
                    bestOrig = oi;
                    bestPos = k;
                }
            }
            continue;
        }
        int lc = nd.left, rc = nd.right;
        if (g_nodes[lc].minIdx > g_nodes[rc].minIdx) {
            std::swap(lc, rc);
        }
        const bool okL = g_nodes[lc].minIdx < bestOrig && originInBox(g_nodes[lc], ray);
        const bool okR = g_nodes[rc].minIdx < bestOrig && originInBox(g_nodes[rc], ray);
        if (okR) {
            g_stkId[sp++] = rc;
        }
        if (okL) {
            g_stkId[sp++] = lc;
        }
    }
    return bestPos;
}

// 阶段 A: 最近命中(t>0). DFS + 最近孩子优先 + 入射 t 剪枝. 返回 position; 无则 -1.
int queryNearest(const Ray &ray, ld ox, ld oy, ld oz, ld dx, ld dy, ld dz) {
    ld bestT = kInf;
    int bestOrig = INT32_MAX;
    int bestPos = -1;

    double rootEnter;
    if (!rayAABB(g_nodes[g_root], ray, rootEnter)) {
        return -1;
    }
    int sp = 0;
    g_stkId[sp] = g_root;
    g_stkT[sp] = rootEnter;
    ++sp;

    while (sp > 0) {
        --sp;
        const int id = g_stkId[sp];
        const double enter = g_stkT[sp];
        if (bestPos != -1 && enter > bestT + 1e-7 * (std::fabs(bestT) + 1)) {
            continue;
        }
        const Node &nd = g_nodes[id];

        if (nd.left == -1) {  // 叶子
            const int st = nd.start, cnt = nd.count;
            const ld tMax = (bestPos == -1) ? kInf : bestT + 1e-7 * (std::fabs(bestT) + 1);
            for (int k = st; k < st + cnt; ++k) {
                ld t;
                if (hitObj(pType[k], &P[pOff[k]], ox, oy, oz, dx, dy, dz, tMax, t)) {
                    const int oi = pOrig[k];
                    const ld tol = 1e-9 * std::fmax(1.0, std::fmax(std::fabs(t), std::fabs(bestT)));
                    if (bestPos == -1 || t < bestT - tol) {
                        bestT = t;
                        bestOrig = oi;
                        bestPos = k;
                    } else if (std::fabs(t - bestT) <= tol && oi < bestOrig) {
                        bestOrig = oi;
                        bestPos = k;
                    }
                }
            }
            continue;
        }

        const int lc = nd.left, rc = nd.right;
        double el = 0.0, er = 0.0;
        const bool hl = rayAABB(g_nodes[lc], ray, el);
        const bool hr = rayAABB(g_nodes[rc], ray, er);
        const double lim = bestPos == -1 ? kInfB : bestT + 1e-7 * (std::fabs(bestT) + 1);
        if (hl && hr) {  // 较远者先入栈, 较近者后入栈先弹出
            if (el <= er) {
                if (bestPos == -1 || er <= lim) { g_stkId[sp] = rc; g_stkT[sp] = er; ++sp; }
                g_stkId[sp] = lc; g_stkT[sp] = el; ++sp;
            } else {
                if (bestPos == -1 || el <= lim) { g_stkId[sp] = lc; g_stkT[sp] = el; ++sp; }
                g_stkId[sp] = rc; g_stkT[sp] = er; ++sp;
            }
        } else if (hl) {
            g_stkId[sp] = lc; g_stkT[sp] = el; ++sp;
        } else if (hr) {
            g_stkId[sp] = rc; g_stkT[sp] = er; ++sp;
        }
    }
    return bestPos;
}

}  // namespace

int main() {
    FastInput in;

    long long n = 0, q = 0;
    if (!in.readLL(n)) {
        return 0;
    }
    in.readLL(q);

    g_type0.resize(n);
    g_off0.resize(n);
    g_param0.reserve(static_cast<std::size_t>(n) * 6 + 16);
    for (int i = 0; i < n; ++i) {
        long long tp = 0;
        in.readLL(tp);
        g_type0[i] = static_cast<int>(tp);
        g_off0[i] = static_cast<long long>(g_param0.size());
        const int cnt = paramCount(static_cast<int>(tp));
        for (int k = 0; k < cnt; ++k) {
            long long val = 0;
            in.readLL(val);
            g_param0.push_back(static_cast<double>(val));
        }
    }

    g_prim.resize(n);
    for (int i = 0; i < n; ++i) {
        g_prim[i] = i;
    }
    g_nodes.reserve(static_cast<std::size_t>(2) * n + 1);
    g_root = buildBVH(0, static_cast<int>(n), -1);

    // 按 position(g_prim) 顺序重排为连续内存
    P.reserve(g_param0.size());
    pType.resize(n);
    pOff.resize(n);
    pOrig.resize(n);
    g_leafOf.assign(n, -1);
    for (int k = 0; k < n; ++k) {
        const int orig = g_prim[k];
        pType[k] = g_type0[orig];
        pOrig[k] = orig;
        pOff[k] = static_cast<int>(P.size());
        const int cnt = paramCount(pType[k]);
        const long long base = g_off0[orig];
        for (int j = 0; j < cnt; ++j) {
            P.push_back(g_param0[base + j]);
        }
    }
    // 标记每个 position 的叶子
    for (int id = 0; id < static_cast<int>(g_nodes.size()); ++id) {
        if (g_nodes[id].left == -1) {
            const int st = g_nodes[id].start, cnt = g_nodes[id].count;
            for (int k = st; k < st + cnt; ++k) {
                g_leafOf[k] = id;
            }
        }
    }
    // 释放建树用的原始数组
    std::vector<double>().swap(g_param0);
    std::vector<long long>().swap(g_off0);
    std::vector<int>().swap(g_type0);
    std::vector<int>().swap(g_prim);

    g_stkId.resize(g_nodes.size() + 16);
    g_stkT.resize(g_nodes.size() + 16);

    std::string out;
    out.reserve(static_cast<std::size_t>(q) * 4);
    char numbuf[16];

    for (int qi = 0; qi < q; ++qi) {
        long long qtype = 0, ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = 0, d = 0;
        in.readLL(qtype);
        in.readLL(ox);
        in.readLL(oy);
        in.readLL(oz);
        in.readLL(dx);
        in.readLL(dy);
        in.readLL(dz);
        in.readLL(d);

        Ray ray;
        ray.o[0] = static_cast<double>(ox);
        ray.o[1] = static_cast<double>(oy);
        ray.o[2] = static_cast<double>(oz);
        const double dd[3] = {static_cast<double>(dx), static_cast<double>(dy), static_cast<double>(dz)};
        for (int a = 0; a < 3; ++a) {
            if (dd[a] == 0.0) {
                ray.parallel[a] = true;
                ray.inv[a] = 0.0;
            } else {
                ray.parallel[a] = false;
                ray.inv[a] = 1.0 / dd[a];
            }
        }

        const ld lox = static_cast<ld>(ox), loy = static_cast<ld>(oy), loz = static_cast<ld>(oz);
        const ld ldx = static_cast<ld>(dx), ldy = static_cast<ld>(dy), ldz = static_cast<ld>(dz);

        int pos = minIndexContaining(ray, lox, loy, loz, ldx, ldy, ldz);  // 阶段 B
        if (pos == -1) {
            pos = queryNearest(ray, lox, loy, loz, ldx, ldy, ldz);        // 阶段 A
        }

        const int ans = (pos == -1) ? -1 : pOrig[pos];
        const int len = std::snprintf(numbuf, sizeof(numbuf), "%d\n", ans);
        out.append(numbuf, len);

        if (pos != -1 && d > 0) {  // 沿 normalize(D) 平移 d
            const ld dl = std::sqrt(static_cast<ld>(dx) * dx + static_cast<ld>(dy) * dy + static_cast<ld>(dz) * dz);
            const ld scale = static_cast<ld>(d) / dl;
            const ld sx = static_cast<ld>(dx) * scale;
            const ld sy = static_cast<ld>(dy) * scale;
            const ld sz = static_cast<ld>(dz) * scale;
            const int o = pOff[pos];
            if (pType[pos] == 3) {  // 三角面片: 三顶点同时平移
                for (int v = 0; v < 3; ++v) {
                    P[o + v * 3] += static_cast<double>(sx);
                    P[o + v * 3 + 1] += static_cast<double>(sy);
                    P[o + v * 3 + 2] += static_cast<double>(sz);
                }
            } else {
                P[o] += static_cast<double>(sx);
                P[o + 1] += static_cast<double>(sy);
                P[o + 2] += static_cast<double>(sz);
            }
            refit(pos);
        }
    }

    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
