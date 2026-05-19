#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace llm_mimo {

/** 切句后回调：segment 为可送 TTS 的文本，is_end 表示本轮结束哨兵 */
using SentenceEmitCallback =
    std::function<void(const std::string& segment, bool is_end)>;

struct SentenceSplitterOptions {
    /** 首句达到该字数后，遇弱标点也可切 */
    size_t min_chars_first = 4;
    /** 普通句最小字数 */
    size_t min_chars = 6;
    /** 超过该长度强制切 */
    size_t max_chars = 48;
    /** 跳过开头前 N 个标点（过滤语气词后首部噪声） */
    int skip_leading_punctuation = 2;
};

/**
 * 流式 UTF-8 增量切句（宽字符标点），供 TTS 流水线使用
 */
class SentenceSplitter {
public:
    explicit SentenceSplitter(SentenceEmitCallback emit,
                              SentenceSplitterOptions options = {});

    void reset();
    void feed(std::string_view utf8_delta);
    /** 冲刷缓冲区；若 force_end 则发送结束哨兵 */
    void flush(bool force_end = true);

private:
    bool is_strong_punct(wchar_t c) const;
    bool is_weak_punct(wchar_t c) const;
    void try_emit(bool force, bool allow_weak);
    std::wstring utf8_to_wide(const std::string& utf8);
    std::string wide_to_utf8(const std::wstring& wide);

    SentenceEmitCallback emit_;
    SentenceSplitterOptions opt_;
    std::wstring buffer_;
    int skipped_punct_ = 0;
    bool first_sentence_emitted_ = false;
};

}  // namespace llm_mimo
