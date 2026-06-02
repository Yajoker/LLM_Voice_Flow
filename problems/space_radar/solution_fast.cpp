// 太空探测与动态几何体查询 —— 加速版 (BVH + 命中后刷新包围盒)
// ---------------------------------------------------------------------------
// 判定逻辑与暴力版完全一致, 用 BVH 做空间剪枝, 并针对超时做了重点优化:
//   1. 遍历用 double + 预计算方向倒数, 大幅降低常数 (精确命中仍用 long double);
//   2. 每个节点记录子树最小物体编号 minIdx;
//      命中 t=0 后, 用 "包围盒入射 t" 与 "minIdx >= 当前最优编号" 双重剪枝,
//      彻底解决 "大量物体堆叠且射线起点在其内部(全部 t=0)" 的最坏情况;
//   3. 物体被推动后只重算其叶子 AABB 并自底向上刷新祖先 (始终保守正确).
// 平局规则: t 相等(容差内)取编号最小者; 剪枝阈值带容差, 不会漏掉同 t 小编号.
// 标准: C++17.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using ld = long double;

namespace {

constexpr ld kEps = 1e-9L;     // 精确判定容差
constexpr double kEpsB = 1e-6; // 包围盒(double)容差
constexpr ld kInf = 1e300L;
constexpr double kInfB = 1e300;

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

// ----------------------------- 物体存储 -----------------------------
// 用一个扁平数组保存全部参数, g_off[i] 指向第 i 个物体参数起点.
// 参数布局:
//   球体 (type 0): cx cy cz r                              (4)
//   长方体(type 1): cx cy cz ux uy uz vx vy vz wx wy wz     (12)
//   圆柱体(type 2): cx cy cz ax ay az r halfH               (8)
//   三角片(type 3): v1x v1y v1z v2x v2y v2z v3x v3y v3z     (9)
std::vector<int> g_type;
std::vector<long long> g_off;
std::vector<double> g_param;

inline ld dot3(ld ax, ld ay, ld az, ld bx, ld by, ld bz) {
    return ax * bx + ay * by + az * bz;
}

// 命中: 若射线命中第 i 个物体则返回 true, 并将命中参数 t(>=0) 写入 tout.
bool hitObj(int i, ld ox, ld oy, ld oz, ld dx, ld dy, ld dz, ld &tout) {
    const long long o = g_off[i];
    const int tp = g_type[i];

    if (tp == 0) {  // ---------- 球体 ----------
        const ld cx = g_param[o], cy = g_param[o + 1], cz = g_param[o + 2];
        const ld r = g_param[o + 3];
        const ld mx = ox - cx, my = oy - cy, mz = oz - cz;
        const ld c = dot3(mx, my, mz, mx, my, mz) - r * r;
        if (c <= kEps) {  // 起点在内部/边界
            tout = 0;
            return true;
        }
        const ld a = dot3(dx, dy, dz, dx, dy, dz);
        const ld b = 2 * dot3(mx, my, mz, dx, dy, dz);
        const ld disc = b * b - 4 * a * c;
        if (disc < 0) {
            return false;
        }
        const ld t0 = (-b - std::sqrt(disc)) / (2 * a);  // 近交点
        if (t0 >= -kEps) {
            tout = t0 > 0 ? t0 : 0;
            return true;
        }
        return false;  // 两根均为负 -> 射线背向
    }

    if (tp == 1) {  // ---------- OBB 长方体 (slab 法, 用非单位轴避免开方) ----------
        const ld cx = g_param[o], cy = g_param[o + 1], cz = g_param[o + 2];
        const ld mx = ox - cx, my = oy - cy, mz = oz - cz;

        const ld axis[3][3] = {
            {g_param[o + 3], g_param[o + 4], g_param[o + 5]},
            {g_param[o + 6], g_param[o + 7], g_param[o + 8]},
            {g_param[o + 9], g_param[o + 10], g_param[o + 11]},
        };

        ld tmin = -kInf;
        ld tmax = kInf;
        bool inside = true;
        for (int k = 0; k < 3; ++k) {
            const ld vx = axis[k][0], vy = axis[k][1], vz = axis[k][2];
            const ld aa = dot3(vx, vy, vz, vx, vy, vz);  // |u|^2
            const ld om = dot3(mx, my, mz, vx, vy, vz);  // (O-C)·u
            const ld dm = dot3(dx, dy, dz, vx, vy, vz);  // D·u
            if (std::fabs(om) > aa + kEps) {
                inside = false;
            }
            if (std::fabs(dm) <= kEps) {  // 射线平行于该 slab
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

    if (tp == 2) {  // ---------- 圆柱体 (侧面 + 两个端盖) ----------
        const ld cx = g_param[o], cy = g_param[o + 1], cz = g_param[o + 2];
        const ld ax = g_param[o + 3], ay = g_param[o + 4], az = g_param[o + 5];
        const ld r = g_param[o + 6], halfH = g_param[o + 7];
        const ld mx = ox - cx, my = oy - cy, mz = oz - cz;

        const ld la = std::sqrt(dot3(ax, ay, az, ax, ay, az));  // 轴长 (保证非零)
        const ld hx = ax / la, hy = ay / la, hz = az / la;      // 单位轴
        const ld oa = dot3(mx, my, mz, hx, hy, hz);             // 沿轴坐标(起点)
        const ld da = dot3(dx, dy, dz, hx, hy, hz);             // 沿轴方向分量
        const ld opx = mx - oa * hx, opy = my - oa * hy, opz = mz - oa * hz;  // 起点径向
        const ld dpx = dx - da * hx, dpy = dy - da * hy, dpz = dz - da * hz;  // 方向径向
        const ld op2 = dot3(opx, opy, opz, opx, opy, opz);

        if (std::fabs(oa) <= halfH + kEps && op2 <= r * r + kEps) {
            tout = 0;
            return true;
        }

        ld best = kInf;

        // 侧面: |径向(t)|^2 = r^2
        const ld a = dot3(dpx, dpy, dpz, dpx, dpy, dpz);
        const ld b = 2 * dot3(opx, opy, opz, dpx, dpy, dpz);
        const ld c0 = op2 - r * r;
        if (a > kEps) {
            const ld disc = b * b - 4 * a * c0;
            if (disc >= 0) {
                const ld sq = std::sqrt(disc);
                const ld roots[2] = {(-b - sq) / (2 * a), (-b + sq) / (2 * a)};
                for (int s = 0; s < 2; ++s) {
                    const ld root = roots[s];
                    if (root >= -kEps) {
                        const ld sAxis = oa + root * da;  // 命中点沿轴坐标
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

        // 端盖: 平面 沿轴坐标 = ±halfH, 命中点径向距离 <= r
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

        if (best < kInf * 0.5L) {
            tout = best;
            return true;
        }
        return false;
    }

    // ---------- 三角面片 (Möller–Trumbore) ----------
    const ld v1x = g_param[o], v1y = g_param[o + 1], v1z = g_param[o + 2];
    const ld v2x = g_param[o + 3], v2y = g_param[o + 4], v2z = g_param[o + 5];
    const ld v3x = g_param[o + 6], v3y = g_param[o + 7], v3z = g_param[o + 8];
    const ld e1x = v2x - v1x, e1y = v2y - v1y, e1z = v2z - v1z;
    const ld e2x = v3x - v1x, e2y = v3y - v1y, e2z = v3z - v1z;

    const ld px = dy * e2z - dz * e2y;  // pvec = D × e2
    const ld py = dz * e2x - dx * e2z;
    const ld pz = dx * e2y - dy * e2x;
    const ld det = e1x * px + e1y * py + e1z * pz;
    if (std::fabs(det) <= kEps) {  // 射线与三角形共面 -> 视为不相交
        return false;
    }
    const ld inv = 1.0L / det;

    const ld sx = ox - v1x, sy = oy - v1y, sz = oz - v1z;
    const ld u = (sx * px + sy * py + sz * pz) * inv;
    if (u < -kEps || u > 1 + kEps) {
        return false;
    }

    const ld qx = sy * e1z - sz * e1y;  // qvec = s × e1
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

// ----------------------------- 物体 AABB (double, 向外略扩保守) -----------------------------
void objAABB(int i, double lo[3], double hi[3]) {
    const long long o = g_off[i];
    const int tp = g_type[i];
    if (tp == 0) {
        const double cx = g_param[o], cy = g_param[o + 1], cz = g_param[o + 2];
        const double r = g_param[o + 3];
        lo[0] = cx - r; hi[0] = cx + r;
        lo[1] = cy - r; hi[1] = cy + r;
        lo[2] = cz - r; hi[2] = cz + r;
    } else if (tp == 1) {
        const double cx = g_param[o], cy = g_param[o + 1], cz = g_param[o + 2];
        const double ex = std::fabs(g_param[o + 3]) + std::fabs(g_param[o + 6]) + std::fabs(g_param[o + 9]);
        const double ey = std::fabs(g_param[o + 4]) + std::fabs(g_param[o + 7]) + std::fabs(g_param[o + 10]);
        const double ez = std::fabs(g_param[o + 5]) + std::fabs(g_param[o + 8]) + std::fabs(g_param[o + 11]);
        lo[0] = cx - ex; hi[0] = cx + ex;
        lo[1] = cy - ey; hi[1] = cy + ey;
        lo[2] = cz - ez; hi[2] = cz + ez;
    } else if (tp == 2) {
        const double cx = g_param[o], cy = g_param[o + 1], cz = g_param[o + 2];
        const double ax = g_param[o + 3], ay = g_param[o + 4], az = g_param[o + 5];
        const double r = g_param[o + 6], halfH = g_param[o + 7];
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
            const double w0 = g_param[o + a];
            const double w1 = g_param[o + 3 + a];
            const double w2 = g_param[o + 6 + a];
            lo[a] = std::min({w0, w1, w2});
            hi[a] = std::max({w0, w1, w2});
        }
    }
    for (int a = 0; a < 3; ++a) {  // 向外略扩, 规避浮点边界(保证保守)
        lo[a] -= kEpsB;
        hi[a] += kEpsB;
    }
}

// ----------------------------- BVH -----------------------------
struct Node {
    double lo[3];
    double hi[3];
    int left;
    int right;
    int parent;
    int obj;     // 叶子为物体下标, 内部节点为 -1
    int minIdx;  // 子树内最小物体编号
};

std::vector<Node> g_nodes;
std::vector<int> g_leafOf;  // objIdx -> node id
std::vector<int> g_prim;    // 物体索引列表(建树时按区间划分)
int g_root = -1;

int buildBVH(int l, int r, int parent) {
    const int id = static_cast<int>(g_nodes.size());
    g_nodes.push_back(Node());
    g_nodes[id].parent = parent;

    double lo[3] = {kInfB, kInfB, kInfB};
    double hi[3] = {-kInfB, -kInfB, -kInfB};
    int minIdx = INT32_MAX;
    for (int k = l; k < r; ++k) {
        double blo[3], bhi[3];
        objAABB(g_prim[k], blo, bhi);
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

    if (r - l == 1) {
        g_nodes[id].left = -1;
        g_nodes[id].right = -1;
        g_nodes[id].obj = g_prim[l];
        g_leafOf[g_prim[l]] = id;
        return id;
    }

    // 选最长轴, 按质心中位数划分
    double cenLo[3] = {kInfB, kInfB, kInfB};
    double cenHi[3] = {-kInfB, -kInfB, -kInfB};
    for (int k = l; k < r; ++k) {
        double blo[3], bhi[3];
        objAABB(g_prim[k], blo, bhi);
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
    if (bestSpan >= 1e-12) {  // 质心几乎相同时直接按下标对半分, 保证深度有界
        std::nth_element(g_prim.begin() + l, g_prim.begin() + mid, g_prim.begin() + r,
                         [&](int x, int y) {
                             double xl[3], xh[3], yl[3], yh[3];
                             objAABB(x, xl, xh);
                             objAABB(y, yl, yh);
                             return (xl[axis] + xh[axis]) < (yl[axis] + yh[axis]);
                         });
    }

    g_nodes[id].obj = -1;
    const int lc = buildBVH(l, mid, id);
    const int rc = buildBVH(mid, r, id);
    g_nodes[id].left = lc;
    g_nodes[id].right = rc;
    return id;
}

void refit(int objIdx) {
    int id = g_leafOf[objIdx];
    double lo[3], hi[3];
    objAABB(objIdx, lo, hi);
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

// 预计算的射线数据 (double, 加速 BVH 遍历)
struct Ray {
    double o[3];
    double inv[3];
    bool parallel[3];  // 方向分量近 0
};

// 射线-AABB: 返回是否相交(t>=0), enter 为入射参数(起点在盒内则 0)
inline bool rayAABB(const Node &nd, const Ray &ray, double &enter) {
    double tmin = 0.0;
    double tmax = kInfB;
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

std::vector<int> g_stk;  // 复用的遍历栈

// 阶段 B: 找出包含射线起点(t<=kEps, 即起点落在闭集内/边界)的最小编号物体; 不存在返回 -1.
// 关键剪枝: 子树最小编号 >= 当前最优时无法更优; 起点不在节点包围盒内则整棵子树无包含.
// 按 minIdx 较小的子节点优先遍历, 重叠堆叠场景下可迅速收敛到最小编号.
int minIndexContaining(const Ray &ray, ld ox, ld oy, ld oz, ld dx, ld dy, ld dz) {
    int best = INT32_MAX;
    g_stk.clear();
    g_stk.push_back(g_root);

    while (!g_stk.empty()) {
        const int id = g_stk.back();
        g_stk.pop_back();
        const Node &nd = g_nodes[id];

        if (nd.minIdx >= best) {
            continue;  // 子树编号都不更小 -> 剪枝
        }
        bool inBox = true;  // 起点必须在包围盒内, 否则子树内无物体能包含它
        for (int a = 0; a < 3; ++a) {
            if (ray.o[a] < nd.lo[a] - kEpsB || ray.o[a] > nd.hi[a] + kEpsB) {
                inBox = false;
                break;
            }
        }
        if (!inBox) {
            continue;
        }

        if (nd.obj >= 0) {  // 叶子
            ld t;
            if (hitObj(nd.obj, ox, oy, oz, dx, dy, dz, t) && t <= kEps && nd.obj < best) {
                best = nd.obj;
            }
            continue;
        }

        const int lc = nd.left;
        const int rc = nd.right;
        if (g_nodes[lc].minIdx <= g_nodes[rc].minIdx) {  // 较小 minIdx 后进先出 -> 先处理
            g_stk.push_back(rc);
            g_stk.push_back(lc);
        } else {
            g_stk.push_back(lc);
            g_stk.push_back(rc);
        }
    }
    return best == INT32_MAX ? -1 : best;
}

// 阶段 A: 起点不在任何物体内时, 求最近命中(t>0). 最近优先遍历 + 入射 t 剪枝.
void queryNearest(const Ray &ray, ld ox, ld oy, ld oz, ld dx, ld dy, ld dz, ld &bestT, int &bestIdx) {
    bestT = kInf;
    bestIdx = -1;

    g_stk.clear();
    double rootEnter;
    if (!rayAABB(g_nodes[g_root], ray, rootEnter)) {
        return;
    }
    g_stk.push_back(g_root);

    while (!g_stk.empty()) {
        const int id = g_stk.back();
        g_stk.pop_back();
        const Node &nd = g_nodes[id];

        if (bestIdx != -1) {
            double enter;
            if (!rayAABB(nd, ray, enter)) {
                continue;
            }
            const double tol = 1e-9 * std::max(1.0, std::fabs(static_cast<double>(bestT)));
            if (enter > static_cast<double>(bestT) + tol) {  // 整盒都比当前最优更远
                continue;
            }
        }

        if (nd.obj >= 0) {  // 叶子
            ld t;
            if (hitObj(nd.obj, ox, oy, oz, dx, dy, dz, t)) {
                const ld tol = 1e-9L * std::fmax(static_cast<ld>(1.0L), std::fmax(std::fabs(t), std::fabs(bestT)));
                if (bestIdx == -1 || t < bestT - tol) {
                    bestT = t;
                    bestIdx = nd.obj;
                } else if (std::fabs(t - bestT) <= tol && nd.obj < bestIdx) {
                    bestIdx = nd.obj;
                }
            }
            continue;
        }

        // 先入栈较远者, 后处理较近者(后进先出, 近处优先以尽快收紧 bestT)
        const int lc = nd.left;
        const int rc = nd.right;
        double el, er;
        const bool hl = rayAABB(g_nodes[lc], ray, el);
        const bool hr = rayAABB(g_nodes[rc], ray, er);
        if (hl && hr) {
            if (el <= er) {
                g_stk.push_back(rc);
                g_stk.push_back(lc);
            } else {
                g_stk.push_back(lc);
                g_stk.push_back(rc);
            }
        } else if (hl) {
            g_stk.push_back(lc);
        } else if (hr) {
            g_stk.push_back(rc);
        }
    }
}

}  // namespace

int main() {
    FastInput in;

    long long n = 0, q = 0;
    if (!in.readLL(n)) {
        return 0;
    }
    in.readLL(q);

    g_type.resize(n);
    g_off.resize(n);
    g_param.reserve(static_cast<std::size_t>(n) * 6 + 16);

    for (int i = 0; i < n; ++i) {
        long long tp = 0;
        in.readLL(tp);
        g_type[i] = static_cast<int>(tp);
        g_off[i] = static_cast<long long>(g_param.size());
        const int cnt = (tp == 0) ? 4 : (tp == 1) ? 12 : (tp == 2) ? 8 : 9;
        for (int k = 0; k < cnt; ++k) {
            long long val = 0;
            in.readLL(val);
            g_param.push_back(static_cast<double>(val));
        }
    }

    g_leafOf.assign(n, -1);
    g_prim.resize(n);
    for (int i = 0; i < n; ++i) {
        g_prim[i] = i;
    }
    g_nodes.reserve(static_cast<std::size_t>(2) * n + 1);
    g_root = buildBVH(0, static_cast<int>(n), -1);

    std::string out;
    out.reserve(static_cast<std::size_t>(q) * 4);
    char numbuf[16];

    for (int qi = 0; qi < q; ++qi) {
        long long qtype = 0, ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = 0, d = 0;
        in.readLL(qtype);  // 射线脉冲类型恒为 2
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

        // 阶段 B: 起点是否落在某物体内(t=0). 若有, t=0 必胜, 取其中最小编号.
        int bestIdx = minIndexContaining(ray, lox, loy, loz, ldx, ldy, ldz);
        if (bestIdx == -1) {  // 阶段 A: 否则求最近命中
            ld bestT;
            queryNearest(ray, lox, loy, loz, ldx, ldy, ldz, bestT, bestIdx);
        }

        const int len = std::snprintf(numbuf, sizeof(numbuf), "%d\n", bestIdx);
        out.append(numbuf, len);

        // 命中且推动距离 > 0 -> 沿 normalize(D) 平移 d
        if (bestIdx >= 0 && d > 0) {
            const ld dl = std::sqrt(static_cast<ld>(dx) * dx + static_cast<ld>(dy) * dy + static_cast<ld>(dz) * dz);
            const ld scale = static_cast<ld>(d) / dl;
            const ld sx = static_cast<ld>(dx) * scale;
            const ld sy = static_cast<ld>(dy) * scale;
            const ld sz = static_cast<ld>(dz) * scale;
            const long long o = g_off[bestIdx];
            if (g_type[bestIdx] == 3) {  // 三角面片: 三个顶点同时平移
                for (int v = 0; v < 3; ++v) {
                    g_param[o + v * 3] += static_cast<double>(sx);
                    g_param[o + v * 3 + 1] += static_cast<double>(sy);
                    g_param[o + v * 3 + 2] += static_cast<double>(sz);
                }
            } else {  // 其余: 中心点平移
                g_param[o] += static_cast<double>(sx);
                g_param[o + 1] += static_cast<double>(sy);
                g_param[o + 2] += static_cast<double>(sz);
            }
            refit(bestIdx);
        }
    }

    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
