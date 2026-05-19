#include "llm_mimo/zmq_gateway.h"

#include "llm_mimo/mimo_client.h"
#include "llm_mimo/sentence_splitter.h"

#include "ZmqPusher.h"
#include "ZmqServer.h"

#include <iostream>
#include <utility>

namespace llm_mimo {

VoiceGateway::VoiceGateway(MimoConfig config, ZmqEndpoints endpoints)
    : config_(std::move(config)), endpoints_(std::move(endpoints))
{
}

void VoiceGateway::process_turn(const std::string& user_text, uint64_t session_id)
{
    zmq_component::ZmqPusher tts_push(endpoints_.tts_push_connect);

    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (active_client_) {
            active_client_->cancel();
        }
        active_client_ = std::make_unique<MimoStreamClient>(config_);
    }

    MimoStreamClient* client_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_ptr = active_client_.get();
    }
    if (client_ptr == nullptr) {
        return;
    }

    SentenceSplitter splitter(
        [&](const std::string& segment, bool is_end) {
            try {
                if (is_end) {
                    tts_push.push("END");
                    std::cout << "[llm_mimo -> tts] session=" << session_id
                              << " END" << std::endl;
                } else if (!segment.empty()) {
                    tts_push.push(segment);
                    std::cout << "[llm_mimo -> tts] session=" << session_id
                              << " \"" << segment << "\"" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "[llm_mimo] 推送 TTS 失败: " << e.what() << std::endl;
            }
        });

    std::vector<ChatMessage> messages;
    messages.push_back(
        {"system",
         "你是简洁的语音助手，用口语化中文回答，控制在两句话以内，不要使用表情符号。"});
    messages.push_back({"user", user_text});

    std::cout << "[llm_mimo] 开始 MiMo 流式 session=" << session_id
              << " 用户: " << user_text << std::endl;

    const auto result = client_ptr->chat_stream(
        messages,
        [&splitter](std::string_view delta, bool is_done,
                    const StreamResult* stream_err) {
            if (stream_err != nullptr && !stream_err->error_message.empty()) {
                std::cerr << "[llm_mimo] 流错误: " << stream_err->error_message
                          << std::endl;
                return;
            }
            if (!delta.empty()) {
                splitter.feed(delta);
            }
            if (is_done) {
                splitter.flush(true);
            }
        });

    if (!result.ok) {
        std::cerr << "[llm_mimo] MiMo 失败 session=" << session_id << " http="
                  << result.http_code << " " << result.error_message << std::endl;
        try {
            tts_push.push("END");
        } catch (...) {
        }
    } else {
        std::cout << "[llm_mimo] MiMo 完成 session=" << session_id
                  << " http=" << result.http_code << std::endl;
    }
}

void VoiceGateway::run()
{
    zmq_component::ZmqServer voice_server(endpoints_.voice_rep_bind);

    std::cout << "[llm_mimo] 语音网关启动" << std::endl;
    std::cout << "  VOICE REP: " << endpoints_.voice_rep_bind << std::endl;
    std::cout << "  TTS PUSH → " << endpoints_.tts_push_connect << std::endl;
    std::cout << "  模型: " << config_.model << std::endl;

    while (true) {
        std::string asr_text = voice_server.receive();
        const uint64_t session_id = ++next_session_;

        std::cout << "[voice -> llm_mimo] session=" << session_id
                  << " \"" << asr_text << "\"" << std::endl;

        // 与旧版 llm 兼容：尽快 REP，VOICE 才能继续握手 TTS
        voice_server.send("llm success reply !!!");

        process_turn(asr_text, session_id);
    }
}

}  // namespace llm_mimo
