// 快速文件搜索 (Fast File Search / "Go to File")
//
// 思路概述：
//   1) 读入全部路径，按字典序(逐字节, 即 memcmp 顺序)排序并去重，得到 rank=lex 序的文档列表。
//      因为结果要求“字典序最小的前 10 条”，所以只要按 rank 升序枚举候选并取前 10 即可。
//   2) 构建 3-gram 倒排索引(CSR 紧凑存储)：trigram -> 升序排列的文档 rank 列表。
//      若一个路径包含某关键字, 则它一定包含该关键字的所有 trigram, 因此“包含关键字的文档集合”
//      一定是该关键字任意一个 trigram 倒排表的子集。
//   3) 查询时, 在所有关键字(长度>=3)的全部 trigram 中挑选倒排表最短的那个作为“驱动表”(candidate
//      superset)。沿驱动表(已是 rank 升序)枚举候选, 用 find() 校验是否真正包含全部关键字(覆盖子串
//      连续性, 以及长度<3 的短关键字), 收集到 10 条即停, 从而做到毫秒级响应。
//   4) 若某关键字的某个 trigram 倒排表为空, 说明无任何文档包含它, 直接判定无匹配。
//      若全部关键字长度<3(无法用 trigram), 退化为按 rank 升序线性扫描+子串校验, 取前 10。
//
// 复杂度：建索引 O(总字符数); 单次查询 ~ O(驱动表长度 * 路径长度), 区分度高的查询近乎常数。

#include <bits/stdc++.h>
using namespace std;

int main() {
    // ---------- 一次性读入全部 stdin ----------
    string in;
    {
        const size_t CH = 1 << 20;
        static char buf[CH];
        size_t n;
        while ((n = fread(buf, 1, CH, stdin)) > 0) in.append(buf, n);
    }

    size_t pos = 0, L = in.size();
    auto skipWs = [&]() {
        while (pos < L) {
            char c = in[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') pos++;
            else break;
        }
    };
    auto readInt = [&]() -> long {
        skipWs();
        long v = 0; bool any = false;
        while (pos < L && in[pos] >= '0' && in[pos] <= '9') { v = v * 10 + (in[pos] - '0'); pos++; any = true; }
        return any ? v : -1;
    };
    auto skipLine = [&]() {
        while (pos < L && in[pos] != '\n') pos++;
        if (pos < L) pos++;
    };
    // 读取一行 [s, e)（不含换行, 去掉尾部 '\r'）
    auto readLine = [&](size_t &s, size_t &e) {
        s = pos;
        while (pos < L && in[pos] != '\n') pos++;
        e = pos;
        if (pos < L) pos++;
        if (e > s && in[e - 1] == '\r') e--;
    };

    long N = readInt();
    if (N < 0) N = 0;
    skipLine();

    // ---------- 读入 N 条路径到连续缓冲 ----------
    string data;
    data.reserve(in.size());
    vector<uint32_t> off;
    off.reserve(N + 1);
    off.push_back(0);
    for (long i = 0; i < N; i++) {
        size_t s, e; readLine(s, e);
        data.append(&in[s], e - s);
        off.push_back((uint32_t)data.size());
    }

    long M = readInt();
    if (M < 0) M = 0;
    skipLine();

    // ---------- 读入 M 条查询(并释放 in) ----------
    vector<string> queries;
    queries.reserve(M);
    for (long i = 0; i < M; i++) {
        size_t s, e; readLine(s, e);
        queries.emplace_back(&in[s], e - s);
    }
    { string().swap(in); }

    // ---------- 按字典序排序 + 去重 ----------
    auto svOf = [&](int idx) -> string_view {
        return string_view(&data[off[idx]], off[idx + 1] - off[idx]);
    };
    vector<int> ord((size_t)N);
    iota(ord.begin(), ord.end(), 0);
    sort(ord.begin(), ord.end(), [&](int a, int b) { return svOf(a) < svOf(b); });

    vector<int> uniq;
    uniq.reserve((size_t)N);
    for (long i = 0; i < N; i++) {
        if (i == 0 || svOf(ord[i]) != svOf(ord[i - 1])) uniq.push_back(ord[i]);
    }
    int D = (int)uniq.size();

    // ---------- 重排为字典序连续缓冲, 提升查询缓存命中 ----------
    string sdata;
    sdata.reserve(data.size());
    vector<uint32_t> soff(D + 1);
    soff[0] = 0;
    for (int r = 0; r < D; r++) {
        int oi = uniq[r];
        sdata.append(&data[off[oi]], off[oi + 1] - off[oi]);
        soff[r + 1] = (uint32_t)sdata.size();
    }
    { string().swap(data); }
    { vector<uint32_t>().swap(off); }
    { vector<int>().swap(ord); }
    { vector<int>().swap(uniq); }

    auto pack = [](const char *p) -> uint32_t {
        return ((uint32_t)(uint8_t)p[0] << 16) | ((uint32_t)(uint8_t)p[1] << 8) | (uint32_t)(uint8_t)p[2];
    };

    // ---------- 构建 3-gram 倒排索引 (CSR) ----------
    const uint32_t TS = 1u << 24; // 256^3
    vector<uint32_t> cnt(TS, 0);
    vector<int> seen(TS, -1);     // 每文档内去重, 避免同一 trigram 重复计数

    for (int r = 0; r < D; r++) {
        const char *p = &sdata[soff[r]];
        int len = (int)(soff[r + 1] - soff[r]);
        for (int i = 0; i + 3 <= len; i++) {
            uint32_t t = pack(p + i);
            if (seen[t] != r) { seen[t] = r; cnt[t]++; }
        }
    }

    vector<uint32_t> start(TS + 1);
    start[0] = 0;
    for (uint32_t t = 0; t < TS; t++) start[t + 1] = start[t] + cnt[t];
    uint64_t total = start[TS];

    vector<uint32_t> postings((size_t)total);
    for (uint32_t t = 0; t < TS; t++) cnt[t] = start[t]; // cnt 复用为写游标
    fill(seen.begin(), seen.end(), -1);

    for (int r = 0; r < D; r++) {
        const char *p = &sdata[soff[r]];
        int len = (int)(soff[r + 1] - soff[r]);
        for (int i = 0; i + 3 <= len; i++) {
            uint32_t t = pack(p + i);
            if (seen[t] != r) { seen[t] = r; postings[cnt[t]++] = (uint32_t)r; }
        }
    }
    { vector<uint32_t>().swap(cnt); }
    { vector<int>().swap(seen); }

    // ---------- 处理查询 ----------
    string out;
    out.reserve(1 << 20);

    vector<string_view> kw;
    vector<int> res;

    for (long qi = 0; qi < M; qi++) {
        const string &q = queries[qi];
        kw.clear();
        {
            size_t i = 0, n = q.size();
            while (i < n) {
                while (i < n && (q[i] == ' ' || q[i] == '\t')) i++;
                size_t j = i;
                while (j < n && q[j] != ' ' && q[j] != '\t') j++;
                if (j > i) kw.emplace_back(&q[i], j - i);
                i = j;
            }
        }

        if (kw.empty()) { out.push_back('\n'); continue; }

        // 选取所有长关键字(>=3)的全部 trigram 中倒排表最短者作为驱动表
        bool emptyRes = false, haveLong = false;
        uint32_t bestStart = 0, bestEnd = 0;
        uint64_t bestSize = UINT64_MAX;
        for (auto &k : kw) {
            if (k.size() >= 3) {
                haveLong = true;
                for (size_t i = 0; i + 3 <= k.size(); i++) {
                    uint32_t t = pack(k.data() + i);
                    uint32_t sz = start[t + 1] - start[t];
                    if (sz == 0) { emptyRes = true; break; }
                    if (sz < bestSize) { bestSize = sz; bestStart = start[t]; bestEnd = start[t + 1]; }
                }
                if (emptyRes) break;
            }
        }

        auto matchAll = [&](int r) -> bool {
            string_view path(&sdata[soff[r]], soff[r + 1] - soff[r]);
            for (auto &k : kw) if (path.find(k) == string_view::npos) return false;
            return true;
        };

        res.clear();
        if (emptyRes) {
            // 无匹配
        } else if (haveLong) {
            for (uint32_t p = bestStart; p < bestEnd && res.size() < 10; p++) {
                int r = (int)postings[p];
                if (matchAll(r)) res.push_back(r);
            }
        } else {
            for (int r = 0; r < D && res.size() < 10; r++) {
                if (matchAll(r)) res.push_back(r);
            }
        }

        for (size_t i = 0; i < res.size(); i++) {
            if (i) out.push_back(' ');
            int r = res[i];
            out.append(&sdata[soff[r]], soff[r + 1] - soff[r]);
        }
        out.push_back('\n');
    }

    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
