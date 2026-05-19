#pragma once

#include "llm_mimo/config.h"
#include "llm_mimo/mimo_client.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace llm_mimo {

struct ZmqEndpoints {
    std::string voice_rep_bind = "tcp://*:6666";
    std::string tts_push_connect = "tcp://localhost:7777";
};

/** VOICE(REP) ↔ MiMo 流式 ↔ TTS(PUSH) 网关 */
class VoiceGateway {
public:
    explicit VoiceGateway(MimoConfig config, ZmqEndpoints endpoints = {});

    /** 阻塞运行，接收 ASR 文本并驱动整条流水线 */
    void run();

private:
    void process_turn(const std::string& user_text, uint64_t session_id);

    MimoConfig config_;
    ZmqEndpoints endpoints_;
    std::mutex client_mutex_;
    std::unique_ptr<MimoStreamClient> active_client_;
    std::atomic<uint64_t> next_session_{0};
};

}  // namespace llm_mimo
