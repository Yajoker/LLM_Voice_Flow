#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace llm_mimo {

/** SSE 解析后的事件类型 */
enum class SseEventKind {
    Delta,   ///< 增量文本 choices[0].delta.content
    Done,    ///< data: [DONE] 或 finish_reason 结束
    Error,   ///< 流内 error 对象或非法 JSON
};

struct SseEvent {
    SseEventKind kind = SseEventKind::Delta;
    std::string content;
    std::string error_type;
};

/**
 * 按 SSE 规范解析 data: 行，处理半行/粘包
 */
class SseParser {
public:
    using EventHandler = std::function<void(const SseEvent&)>;

    explicit SseParser(EventHandler handler);

    void feed(const char* data, size_t len);
    void flush();

private:
    void process_line(std::string_view line);
    void handle_data_payload(std::string_view payload);

    EventHandler handler_;
    std::string line_buffer_;
};

}  // namespace llm_mimo
