#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace llm_mimo {

/**
 * 与小米 MiMo 控制台「套餐权益」中列出的模型 ID 对齐（OpenAI 兼容为小写连字符）。
 * 对话模块 llm_mimo 仅使用 chat_models；TTS 系列由 tts 模块调用，不在此发起 chat。
 */
struct PlanModels {
    /** 套餐内可用于 /v1/chat/completions 的模型 */
    static const std::vector<std::string>& chat_models()
    {
        static const std::vector<std::string> kModels = {
            "mimo-v2.5-pro",
            "mimo-v2.5",
            "mimo-v2-pro",
            "mimo-v2-omni",
        };
        return kModels;
    }

    /** 套餐内 TTS 相关模型（勿用于 llm_mimo 流式对话） */
    static const std::vector<std::string>& tts_models()
    {
        static const std::vector<std::string> kModels = {
            "mimo-v2.5-tts",
            "mimo-v2.5-tts-voiceclone",
            "mimo-v2.5-tts-voicedesign",
            "mimo-v2-tts",
        };
        return kModels;
    }

    /** 控制台展示名 → API model 字段 */
    static const std::vector<std::pair<std::string, std::string>>& display_names()
    {
        static const std::vector<std::pair<std::string, std::string>> kMap = {
            {"MiMo-V2.5-Pro", "mimo-v2.5-pro"},
            {"MiMo-V2.5", "mimo-v2.5"},
            {"MiMo-V2.5-TTS-VoiceClone", "mimo-v2.5-tts-voiceclone"},
            {"MiMo-V2.5-TTS-VoiceDesign", "mimo-v2.5-tts-voicedesign"},
            {"MiMo-V2.5-TTS", "mimo-v2.5-tts"},
            {"MiMo-V2-Pro", "mimo-v2-pro"},
            {"MiMo-V2-Omni", "mimo-v2-omni"},
            {"MiMo-V2-TTS", "mimo-v2-tts"},
        };
        return kMap;
    }
};

inline bool is_supported_chat_model(const std::string& model_id)
{
    const auto& list = PlanModels::chat_models();
    return std::find(list.begin(), list.end(), model_id) != list.end();
}

inline std::string supported_chat_models_hint()
{
    std::string s;
    for (const auto& m : PlanModels::chat_models()) {
        if (!s.empty()) {
            s += ", ";
        }
        s += m;
    }
    return s;
}

/** 语音对话推荐：均衡延迟与质量 */
inline constexpr const char* kDefaultVoiceChatModel = "mimo-v2.5";

}  // namespace llm_mimo
