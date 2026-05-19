#include "llm_mimo/mimo_client.h"

#include "llm_mimo/response_extract.h"
#include "llm_mimo/sse_parser.h"

#include <curl/curl.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

#include <utility>

namespace llm_mimo {
namespace {

struct CurlStreamContext {
    MimoStreamClient* client = nullptr;
    SseParser* parser = nullptr;
    std::string response_accum;
    bool stream_mode = true;
};

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* ctx = static_cast<CurlStreamContext*>(userdata);
    const size_t total = size * nmemb;
    if (ctx->client != nullptr && ctx->client->is_cancelled()) {
        return 0;
    }

    if (ptr == nullptr || total == 0) {
        return 0;
    }

    ctx->response_accum.append(ptr, total);
    if (ctx->stream_mode && ctx->parser != nullptr) {
        ctx->parser->feed(ptr, total);
    }
    return total;
}

std::string build_request_body(const MimoConfig& cfg,
                               const std::vector<ChatMessage>& messages,
                               bool stream)
{
    nlohmann::json body;
    body["model"] = cfg.model;
    body["stream"] = stream;
    body["temperature"] = cfg.temperature;
    body["max_tokens"] = cfg.max_tokens;
    body["max_completion_tokens"] = cfg.max_tokens;

    nlohmann::json msg_array = nlohmann::json::array();
    for (const auto& m : messages) {
        msg_array.push_back({{"role", m.role}, {"content", m.content}});
    }
    body["messages"] = std::move(msg_array);
    return body.dump();
}

StreamResult parse_http_error_body(long http_code, const std::string& body)
{
    StreamResult r;
    r.ok = false;
    r.http_code = http_code;
    r.raw_body_snippet = body.size() > 512 ? body.substr(0, 512) : body;

    try {
        const auto j = nlohmann::json::parse(body);
        if (j.contains("error") && j["error"].is_object()) {
            r.error_message = j["error"].value("message", std::string("HTTP 错误"));
            r.error_type = j["error"].value("type", std::string("http_error"));
            return r;
        }
    } catch (...) {
    }

    r.error_message = "HTTP " + std::to_string(http_code);
    r.error_type = "http_error";
    if (!body.empty()) {
        r.error_message += ": " + r.raw_body_snippet;
    }
    return r;
}

bool debug_enabled()
{
    const char* v = std::getenv("MIMO_DEBUG");
    return v != nullptr && *v != '\0' && std::string(v) != "0";
}

void debug_dump_response(const std::string& tag, const std::string& body)
{
    if (!debug_enabled()) {
        return;
    }
    constexpr size_t kMax = 4096;
    const std::string snippet =
        body.size() > kMax ? body.substr(0, kMax) + "..." : body;
    std::cerr << "[llm_mimo][debug] " << tag << " bytes=" << body.size() << "\n"
              << snippet << std::endl;
}

StreamResult perform_chat_request(MimoStreamClient* client,
                                  MimoConfig& cfg,
                                  const std::vector<ChatMessage>& messages,
                                  bool stream,
                                  TokenCallback* callback,
                                  int* non_empty_delta_count,
                                  std::string* raw_body_out)
{
    StreamResult result;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error_message = "curl_easy_init 失败";
        result.error_type = "curl_error";
        return result;
    }

    const std::string url = chat_completions_url(cfg);
    const std::string body = build_request_body(cfg, messages, stream);
    const std::string api_key_header = "api-key: " + cfg.api_key;
    const std::string bearer_header = "Authorization: Bearer " + cfg.api_key;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, api_key_header.c_str());
    headers = curl_slist_append(headers, bearer_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (stream) {
        headers = curl_slist_append(headers, "Accept: text/event-stream");
    }

    int nonempty_deltas = 0;
    bool stream_finished = false;
    bool done_notified = false;
    StreamResult stream_error;

    std::unique_ptr<SseParser> parser;
    SseParser* parser_ptr = nullptr;
    if (stream && callback != nullptr) {
        parser = std::make_unique<SseParser>([&](const SseEvent& ev) {
            if (client != nullptr && client->is_cancelled()) {
                return;
            }
            switch (ev.kind) {
                case SseEventKind::Delta:
                    if (!ev.content.empty()) {
                        ++nonempty_deltas;
                        (*callback)(ev.content, false, nullptr);
                    }
                    break;
                case SseEventKind::Done:
                    stream_finished = true;
                    if (!done_notified) {
                        done_notified = true;
                        (*callback)("", true, nullptr);
                    }
                    break;
                case SseEventKind::Error:
                    stream_error.ok = false;
                    stream_error.error_message = ev.content;
                    stream_error.error_type = ev.error_type;
                    stream_finished = true;
                    if (!done_notified) {
                        done_notified = true;
                        (*callback)("", true, &stream_error);
                    }
                    break;
            }
        });
        parser_ptr = parser.get();
    }

    CurlStreamContext ctx;
    ctx.client = client;
    ctx.parser = parser_ptr;
    ctx.stream_mode = stream;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, cfg.connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, cfg.turn_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    if (cfg.read_idle_timeout_ms > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,
                         static_cast<long>(cfg.read_idle_timeout_ms / 1000));
    }
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode code = curl_easy_perform(curl);
    if (parser_ptr != nullptr) {
        parser_ptr->flush();
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    result.http_code = http_code;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (raw_body_out != nullptr) {
        *raw_body_out = std::move(ctx.response_accum);
    }
    if (non_empty_delta_count != nullptr) {
        *non_empty_delta_count = nonempty_deltas;
    }

    if (client != nullptr && client->is_cancelled()) {
        result.error_message = "请求已取消";
        result.error_type = "cancelled";
        return result;
    }

    if (code != CURLE_OK) {
        result.error_message = curl_easy_strerror(code);
        result.error_type = "curl_error";
        if (code == CURLE_OPERATION_TIMEDOUT) {
            result.error_type = "timeout";
        }
        return result;
    }

    if (http_code < 200 || http_code >= 300) {
        return parse_http_error_body(http_code, ctx.response_accum);
    }

    if (!stream_error.error_message.empty()) {
        return stream_error;
    }

    if (!stream && callback != nullptr) {
        const std::string text =
            extract_text_from_completion_body(ctx.response_accum);
        if (!text.empty()) {
            (*callback)(text, true, nullptr);
        }
    } else if (stream && callback != nullptr && !stream_finished) {
        (*callback)("", true, nullptr);
    }

    result.ok = true;
    return result;
}

}  // namespace

MimoStreamClient::MimoStreamClient(MimoConfig config)
    : config_(std::move(config))
{
}

void MimoStreamClient::cancel() { cancelled_.store(true); }

StreamResult MimoStreamClient::chat_stream(const std::vector<ChatMessage>& messages,
                                           TokenCallback callback)
{
    cancelled_.store(false);

    if (messages.empty()) {
        StreamResult r;
        r.error_message = "messages 不能为空";
        r.error_type = "invalid_argument";
        return r;
    }

    int delta_count = 0;
    std::string raw_body;
    StreamResult result = perform_chat_request(
        this, config_, messages, true, &callback, &delta_count, &raw_body);

    debug_dump_response("stream", raw_body);

    if (!result.ok) {
        return result;
    }

    if (delta_count == 0) {
        const std::string merged = extract_text_from_completion_body(raw_body);
        if (!merged.empty() && callback) {
            callback(merged, false, nullptr);
            callback("", true, nullptr);
            return result;
        }

        if (debug_enabled()) {
            std::cerr << "[llm_mimo] 流式未解析到文本，尝试非流式回退" << std::endl;
        } else {
            std::cerr << "[llm_mimo] 流式无输出，正在非流式回退…" << std::endl;
        }

        int dummy = 0;
        std::string raw2;
        StreamResult fallback = perform_chat_request(
            this, config_, messages, false, &callback, &dummy, &raw2);
        debug_dump_response("fallback", raw2);

        if (fallback.ok && dummy == 0) {
            const std::string text = extract_text_from_completion_body(raw2);
            if (!text.empty() && callback) {
                callback(text, true, nullptr);
            } else if (debug_enabled()) {
                std::cerr << "[llm_mimo] 非流式仍无文本，请检查 MIMO_DEBUG=1 原始响应"
                          << std::endl;
            }
        }
        return fallback.ok ? fallback : result;
    }

    return result;
}

StreamResult MimoStreamClient::verify_api_key()
{
    cancelled_.store(false);
    std::vector<ChatMessage> messages;
    messages.push_back({"user", "ping"});

    int dummy = 0;
    std::string raw;
    return perform_chat_request(this, config_, messages, false, nullptr, &dummy,
                                &raw);
}

}  // namespace llm_mimo
