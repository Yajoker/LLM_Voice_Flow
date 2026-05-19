#include "llm_mimo/sse_parser.h"
#include "llm_mimo/response_extract.h"

#include <nlohmann/json.hpp>

namespace llm_mimo {
namespace {

constexpr std::string_view kDataPrefix = "data:";

std::string_view trim_view(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

std::string_view strip_cr(std::string_view line)
{
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

}  // namespace

SseParser::SseParser(EventHandler handler) : handler_(std::move(handler)) {}

void SseParser::feed(const char* data, size_t len)
{
    if (data == nullptr || len == 0) {
        return;
    }
    line_buffer_.append(data, len);

    size_t pos = 0;
    while ((pos = line_buffer_.find('\n')) != std::string::npos) {
        std::string line = line_buffer_.substr(0, pos);
        line_buffer_.erase(0, pos + 1);
        process_line(line);
    }
}

void SseParser::flush()
{
    if (!line_buffer_.empty()) {
        process_line(line_buffer_);
        line_buffer_.clear();
    }
}

void SseParser::process_line(std::string_view raw_line)
{
    raw_line = strip_cr(trim_view(raw_line));
    if (raw_line.empty()) {
        return;
    }

    if (raw_line.size() >= kDataPrefix.size() &&
        raw_line.substr(0, kDataPrefix.size()) == kDataPrefix) {
        std::string_view payload = raw_line.substr(kDataPrefix.size());
        if (!payload.empty() && payload.front() == ' ') {
            payload.remove_prefix(1);
        }
        handle_data_payload(payload);
        return;
    }

    // 部分网关直接返回 JSON 行（非标准 SSE 前缀）
    if (raw_line.front() == '{') {
        handle_data_payload(raw_line);
    }
}

void SseParser::handle_data_payload(std::string_view payload)
{
    if (payload == "[DONE]") {
        if (handler_) {
            handler_(SseEvent{SseEventKind::Done, "", ""});
        }
        return;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(payload);
    } catch (const nlohmann::json::exception& e) {
        if (handler_) {
            SseEvent ev;
            ev.kind = SseEventKind::Error;
            ev.content = std::string("SSE JSON 解析失败: ") + e.what();
            ev.error_type = "parse_error";
            handler_(ev);
        }
        return;
    }

    if (j.contains("error") && j["error"].is_object()) {
        SseEvent ev;
        ev.kind = SseEventKind::Error;
        const auto& err = j["error"];
        ev.content = err.value("message", std::string("未知 API 错误"));
        ev.error_type = err.value("type", std::string("api_error"));
        if (handler_) {
            handler_(ev);
        }
        return;
    }

    if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
        return;
    }

    const auto& choice0 = j["choices"][0];
    const std::string text = extract_text_from_choice(choice0);
    if (!text.empty() && handler_) {
        handler_(SseEvent{SseEventKind::Delta, text, ""});
    }

    if (choice0.contains("finish_reason") && !choice0["finish_reason"].is_null()) {
        const std::string reason = choice0["finish_reason"].get<std::string>();
        if (!reason.empty() && handler_) {
            handler_(SseEvent{SseEventKind::Done, "", ""});
        }
    }
}

}  // namespace llm_mimo
