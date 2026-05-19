#include "llm_mimo/sentence_splitter.h"

#include <codecvt>
#include <locale>
namespace llm_mimo {
namespace {

bool contains_filtered_tag(const std::string& s)
{
    return s.find("<think>") != std::string::npos ||
           s.find("</think>") != std::string::npos;
}

}  // namespace

SentenceSplitter::SentenceSplitter(SentenceEmitCallback emit,
                                   SentenceSplitterOptions options)
    : emit_(std::move(emit)), opt_(options)
{
}

void SentenceSplitter::reset()
{
    buffer_.clear();
    skipped_punct_ = 0;
    first_sentence_emitted_ = false;
}

void SentenceSplitter::feed(std::string_view utf8_delta)
{
    if (utf8_delta.empty() || !emit_) {
        return;
    }
    const std::string chunk(utf8_delta);
    if (contains_filtered_tag(chunk)) {
        return;
    }

    const std::wstring wide = utf8_to_wide(chunk);
    for (wchar_t c : wide) {
        buffer_ += c;

        if (buffer_.size() >= opt_.max_chars) {
            try_emit(true, true);
            continue;
        }

        if (is_strong_punct(c)) {
            if (skipped_punct_ < opt_.skip_leading_punctuation) {
                ++skipped_punct_;
                continue;
            }
            try_emit(false, true);
            continue;
        }

        if (is_weak_punct(c)) {
            const size_t min_need =
                first_sentence_emitted_ ? opt_.min_chars : opt_.min_chars_first;
            if (buffer_.size() >= min_need) {
                if (skipped_punct_ < opt_.skip_leading_punctuation) {
                    ++skipped_punct_;
                    continue;
                }
                try_emit(false, true);
            }
        }
    }
}

void SentenceSplitter::flush(bool force_end)
{
    if (!buffer_.empty()) {
        try_emit(true, true);
    }
    if (force_end && emit_) {
        emit_("", true);
    }
}

bool SentenceSplitter::is_strong_punct(wchar_t c) const
{
    return c == L'。' || c == L'！' || c == L'？' || c == L'；' || c == L'\n';
}

bool SentenceSplitter::is_weak_punct(wchar_t c) const
{
    return c == L'，' || c == L'、' || c == L'：' || c == L',';
}

void SentenceSplitter::try_emit(bool force, bool allow_weak)
{
    (void)allow_weak;
    if (buffer_.empty()) {
        return;
    }

    std::wstring out = buffer_;
    // 去掉首尾空白
    while (!out.empty() && (out.front() == L' ' || out.front() == L'\t' ||
                            out.front() == L'\r' || out.front() == L'\n')) {
        out.erase(out.begin());
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'\t' ||
                            out.back() == L'\r' || out.back() == L'\n')) {
        out.pop_back();
    }

    if (!force) {
        const size_t min_need =
            first_sentence_emitted_ ? opt_.min_chars : opt_.min_chars_first;
        if (out.size() < min_need) {
            return;
        }
    }

    if (out.empty()) {
        buffer_.clear();
        return;
    }

    const std::string utf8 = wide_to_utf8(out);
    if (contains_filtered_tag(utf8)) {
        buffer_.clear();
        return;
    }

    emit_(utf8, false);
    first_sentence_emitted_ = true;
    buffer_.clear();
}

std::wstring SentenceSplitter::utf8_to_wide(const std::string& utf8)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.from_bytes(utf8);
}

std::string SentenceSplitter::wide_to_utf8(const std::wstring& wide)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(wide);
}

}  // namespace llm_mimo
