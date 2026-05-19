#pragma once

#include "llm_mimo/config.h"

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace llm_mimo {

/** 单条对话消息（OpenAI messages 格式） */
struct ChatMessage {
    std::string role;
    std::string content;
};

/** 流式调用结果状态 */
struct StreamResult {
    bool ok = false;
    long http_code = 0;
    std::string error_message;
    std::string error_type;
    /** 非流式错误响应体或调试信息 */
    std::string raw_body_snippet;
};

/**
 * 流式 token 回调
 * @param delta     增量 UTF-8 文本（可能为空，可忽略）
 * @param is_done   本轮流结束
 * @param error     若 is_done 且出错，携带错误信息
 */
using TokenCallback = std::function<void(std::string_view delta, bool is_done,
                                         const StreamResult* error)>;

/** 基于 libcurl easy 的小米 MiMo SSE 客户端 */
class MimoStreamClient {
public:
    explicit MimoStreamClient(MimoConfig config);

    /**
     * 发起流式 chat/completions
     * 阻塞直到流结束、超时或 cancel()
     */
    StreamResult chat_stream(const std::vector<ChatMessage>& messages,
                             TokenCallback callback);

    /** 非流式短请求，用于校验 API Key 与端点是否可用 */
    StreamResult verify_api_key();

    void cancel();

    /** 供 libcurl 写回调检测是否中止传输 */
    bool is_cancelled() const { return cancelled_.load(); }

private:
    MimoConfig config_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace llm_mimo
