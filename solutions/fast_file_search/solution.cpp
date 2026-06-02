// 快速文件搜索 (Fast File Search / IDE "Go to File")
// -----------------------------------------------------------------------------
// 题目要求：在百万级文件路径中实现核心搜索引擎类 FileSystemSearch，支持
//   1) 非前缀子串匹配；
//   2) 空格分隔的多关键字 AND 匹配（关键字顺序无关）；
//   3) 结果按字典序排列，仅返回最小的前 10 条；
//   4) 毫秒级响应。
//
// 匹配规则：大小写不敏感（如 "UTILS" 命中 "utils"）。索引与匹配均在小写化后的
//           文本上进行，但输出保留路径原始大小写，排序仍按原始字节字典序。
//
// 算法概述：
//   * 字典序预排序 + 去重：路径按逐字节字典序排序，其下标 rank 即字典序。
//     因为只需“字典序最小的前 10 条”，按 rank 升序枚举候选取前 10 即可。
//   * 3-gram 倒排索引（CSR 紧凑存储）：trigram -> 升序排列的文档 rank 列表，
//     建立在小写化文本上。若路径包含某关键字，则它必然包含该关键字的全部
//     trigram，因此“包含关键字的文档集合”一定是该关键字任意一个 trigram 倒排
//     表的子集。
//   * 查询时，在所有长度 >= 3 的关键字的全部 trigram 中挑选倒排表最短者作为驱动表
//     （候选超集），沿驱动表（天然 rank 升序）枚举候选，用 find() 校验是否真正包含
//     全部关键字（覆盖子串连续性以及长度 < 3 的短关键字），收集到 10 条即停。
//   * 边界：某 trigram 倒排表为空 => 无任何匹配；若全部关键字长度 < 3（无法用
//     trigram），退化为按 rank 升序线性扫描 + 子串校验取前 10。
//
// 复杂度：建索引 O(总字符数)；单次查询约 O(驱动表长度 * 路径长度)，区分度高的
//         查询近乎常数时间。
//
// 编译：g++ -O2 -std=c++17 solution.cpp -o solution
// -----------------------------------------------------------------------------

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

namespace fsearch {

// 单次查询最多返回的匹配数量。
constexpr std::size_t kMaxResults = 10;

// 核心搜索引擎：构建索引并支持多关键字子串查询。
class FileSystemSearch {
public:
    // 用给定的路径集合构建索引。paths 可包含重复项与任意顺序，内部会完成
    // 字典序排序与去重；构建完成后 paths 引用的外部缓冲即可释放。
    void Build(const std::vector<std::string_view>& paths) {
        BuildSortedUnique(paths);
        BuildTrigramIndex();
    }

    // 文档数量（去重后）。
    int DocCount() const { return doc_count_; }

    // 取得字典序第 rank 个路径。
    std::string_view PathOf(int rank) const {
        return std::string_view(data_.data() + offset_[rank],
                                offset_[rank + 1] - offset_[rank]);
    }

    // 查询：keywords 为一组关键字（调用方需保证已转为小写），要求路径同时包含
    // 全部关键字（顺序无关，大小写不敏感）。结果以字典序升序写入 out_ranks，
    // 最多 kMaxResults 条。
    void Search(const std::vector<std::string_view>& keywords,
                std::vector<int>& out_ranks) const {
        out_ranks.clear();
        if (keywords.empty()) return;

        // 在所有长关键字(>=3)的全部 trigram 中选取倒排表最短者作为驱动表。
        bool no_match = false;
        bool has_long = false;
        std::uint32_t best_begin = 0;
        std::uint32_t best_end = 0;
        std::uint64_t best_size = UINT64_MAX;

        for (const std::string_view& kw : keywords) {
            if (kw.size() < 3) continue;
            has_long = true;
            for (std::size_t i = 0; i + 3 <= kw.size(); ++i) {
                const std::uint32_t tri = Pack(kw.data() + i);
                const std::uint32_t size = start_[tri + 1] - start_[tri];
                if (size == 0) {  // 该 trigram 不存在 => 此关键字无处可寻
                    no_match = true;
                    break;
                }
                if (size < best_size) {
                    best_size = size;
                    best_begin = start_[tri];
                    best_end = start_[tri + 1];
                }
            }
            if (no_match) break;
        }

        if (no_match) return;

        if (has_long) {
            for (std::uint32_t p = best_begin;
                 p < best_end && out_ranks.size() < kMaxResults; ++p) {
                const int rank = static_cast<int>(postings_[p]);
                if (MatchesAll(rank, keywords)) out_ranks.push_back(rank);
            }
        } else {  // 所有关键字长度都 < 3，无法借助 trigram，按字典序线性扫描
            for (int rank = 0;
                 rank < doc_count_ && out_ranks.size() < kMaxResults; ++rank) {
                if (MatchesAll(rank, keywords)) out_ranks.push_back(rank);
            }
        }
    }

private:
    // ASCII 大写转小写，其余字节保持不变。
    static char ToLower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    // 将连续 3 字节打包为 24 位整数，作为 trigram 的键。
    static std::uint32_t Pack(const char* p) {
        return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[0])) << 16) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[1])) << 8) |
               static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[2]));
    }

    // 校验第 rank 个路径是否包含全部关键字（在小写化文本上比较）。
    bool MatchesAll(int rank, const std::vector<std::string_view>& keywords) const {
        const std::string_view path(lower_.data() + offset_[rank],
                                    offset_[rank + 1] - offset_[rank]);
        for (const std::string_view& kw : keywords) {
            if (path.find(kw) == std::string_view::npos) return false;
        }
        return true;
    }

    // 字典序排序 + 去重，并把路径重排进连续缓冲以提升查询时的缓存命中。
    void BuildSortedUnique(const std::vector<std::string_view>& paths) {
        const int n = static_cast<int>(paths.size());
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return paths[a] < paths[b]; });

        data_.clear();
        std::size_t total_bytes = 0;
        for (const std::string_view& p : paths) total_bytes += p.size();
        data_.reserve(total_bytes);
        offset_.clear();
        offset_.reserve(static_cast<std::size_t>(n) + 1);
        offset_.push_back(0);

        for (int i = 0; i < n; ++i) {
            const std::string_view cur = paths[order[i]];
            if (i > 0 && cur == paths[order[i - 1]]) continue;  // 去重
            data_.append(cur.data(), cur.size());
            offset_.push_back(static_cast<std::uint32_t>(data_.size()));
        }
        doc_count_ = static_cast<int>(offset_.size()) - 1;

        // 生成与 data_ 等长的小写化副本，供建索引与匹配使用。
        lower_.resize(data_.size());
        for (std::size_t i = 0; i < data_.size(); ++i) lower_[i] = ToLower(data_[i]);
    }

    // 构建 3-gram 倒排索引（CSR：start_ 为各 trigram 在 postings_ 中的起始位置）。
    void BuildTrigramIndex() {
        std::vector<std::uint32_t> count(kTrigramSpace, 0);
        std::vector<int> seen(kTrigramSpace, -1);  // 文档内去重，避免重复计数

        for (int rank = 0; rank < doc_count_; ++rank) {
            const char* p = lower_.data() + offset_[rank];
            const int len = static_cast<int>(offset_[rank + 1] - offset_[rank]);
            for (int i = 0; i + 3 <= len; ++i) {
                const std::uint32_t tri = Pack(p + i);
                if (seen[tri] != rank) {
                    seen[tri] = rank;
                    ++count[tri];
                }
            }
        }

        start_.assign(kTrigramSpace + 1, 0);
        for (std::uint32_t t = 0; t < kTrigramSpace; ++t) {
            start_[t + 1] = start_[t] + count[t];
        }
        postings_.assign(start_[kTrigramSpace], 0);

        for (std::uint32_t t = 0; t < kTrigramSpace; ++t) count[t] = start_[t];  // 复用为写游标
        std::fill(seen.begin(), seen.end(), -1);

        for (int rank = 0; rank < doc_count_; ++rank) {
            const char* p = lower_.data() + offset_[rank];
            const int len = static_cast<int>(offset_[rank + 1] - offset_[rank]);
            for (int i = 0; i + 3 <= len; ++i) {
                const std::uint32_t tri = Pack(p + i);
                if (seen[tri] != rank) {
                    seen[tri] = rank;
                    postings_[count[tri]++] = static_cast<std::uint32_t>(rank);
                }
            }
        }
    }

    // trigram 键空间：256^3。
    static constexpr std::uint32_t kTrigramSpace = 1u << 24;

    std::string data_;                    // 去重后按字典序连续存放的全部路径（原始大小写）
    std::string lower_;                    // 与 data_ 等长的小写化副本，用于建索引与匹配
    std::vector<std::uint32_t> offset_;   // 第 rank 个路径为 [offset_[rank], offset_[rank+1])
    std::vector<std::uint32_t> start_;     // CSR 起始偏移
    std::vector<std::uint32_t> postings_;  // 各 trigram 对应的升序 rank 列表
    int doc_count_ = 0;
};

// 一次性读入全部标准输入。
std::string ReadAllStdin() {
    std::string buffer;
    constexpr std::size_t kChunk = 1 << 20;
    static char chunk[kChunk];
    std::size_t got = 0;
    while ((got = std::fread(chunk, 1, kChunk, stdin)) > 0) {
        buffer.append(chunk, got);
    }
    return buffer;
}

// 按空格/制表符拆分关键字（忽略空片段），并将关键字小写化写入 scratch；
// out 中的 string_view 指向 scratch（在所有片段写入后统一构建，避免扩容失效）。
void SplitKeywordsLower(std::string_view line, std::string& scratch,
                        std::vector<std::pair<std::size_t, std::size_t>>& spans,
                        std::vector<std::string_view>& out) {
    scratch.clear();
    spans.clear();
    out.clear();

    std::size_t i = 0;
    const std::size_t n = line.size();
    while (i < n) {
        while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;
        std::size_t j = i;
        while (j < n && line[j] != ' ' && line[j] != '\t') ++j;
        if (j > i) {
            const std::size_t begin = scratch.size();
            for (std::size_t k = i; k < j; ++k) {
                const char c = line[k];
                scratch.push_back((c >= 'A' && c <= 'Z')
                                      ? static_cast<char>(c - 'A' + 'a')
                                      : c);
            }
            spans.emplace_back(begin, scratch.size());
        }
        i = j;
    }

    for (const auto& s : spans) {
        out.emplace_back(scratch.data() + s.first, s.second - s.first);
    }
}

}  // namespace fsearch

int main() {
    const std::string input = fsearch::ReadAllStdin();

    std::size_t pos = 0;
    const std::size_t len = input.size();

    auto skip_ws = [&]() {
        while (pos < len) {
            const char c = input[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos;
            else break;
        }
    };
    auto read_int = [&]() -> long {
        skip_ws();
        long value = 0;
        bool any = false;
        while (pos < len && input[pos] >= '0' && input[pos] <= '9') {
            value = value * 10 + (input[pos] - '0');
            ++pos;
            any = true;
        }
        return any ? value : 0;
    };
    auto skip_line = [&]() {
        while (pos < len && input[pos] != '\n') ++pos;
        if (pos < len) ++pos;
    };
    // 读取一行 [begin, end)（不含换行；去掉行尾 '\r'）。
    auto read_line = [&](std::size_t& begin, std::size_t& end) {
        begin = pos;
        while (pos < len && input[pos] != '\n') ++pos;
        end = pos;
        if (pos < len) ++pos;
        if (end > begin && input[end - 1] == '\r') --end;
    };

    const long file_count = read_int();
    skip_line();

    std::vector<std::string_view> paths;
    paths.reserve(static_cast<std::size_t>(file_count));
    for (long i = 0; i < file_count; ++i) {
        std::size_t begin = 0;
        std::size_t end = 0;
        read_line(begin, end);
        paths.emplace_back(input.data() + begin, end - begin);
    }

    const long query_count = read_int();
    skip_line();

    std::vector<std::pair<std::size_t, std::size_t>> query_spans;
    query_spans.reserve(static_cast<std::size_t>(query_count));
    for (long i = 0; i < query_count; ++i) {
        std::size_t begin = 0;
        std::size_t end = 0;
        read_line(begin, end);
        query_spans.emplace_back(begin, end);
    }

    fsearch::FileSystemSearch engine;
    engine.Build(paths);

    std::string output;
    output.reserve(1 << 20);

    std::vector<std::string_view> keywords;
    std::vector<int> ranks;
    std::string kw_scratch;
    std::vector<std::pair<std::size_t, std::size_t>> kw_spans;

    for (const auto& span : query_spans) {
        const std::string_view line(input.data() + span.first, span.second - span.first);
        fsearch::SplitKeywordsLower(line, kw_scratch, kw_spans, keywords);

        if (keywords.empty()) {
            output.push_back('\n');
            continue;
        }

        engine.Search(keywords, ranks);
        for (std::size_t i = 0; i < ranks.size(); ++i) {
            if (i) output.push_back(' ');
            const std::string_view path = engine.PathOf(ranks[i]);
            output.append(path.data(), path.size());
        }
        output.push_back('\n');
    }

    std::fwrite(output.data(), 1, output.size(), stdout);
    return 0;
}
