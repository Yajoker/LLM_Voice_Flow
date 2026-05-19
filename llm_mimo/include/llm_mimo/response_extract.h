#pragma once

#include <cstdlib>
#include <nlohmann/json.hpp>
#include <string>

namespace llm_mimo {

/** 默认不向终端/TTS 推送思考链；设 MIMO_INCLUDE_REASONING=1 可打印 reasoning */
inline bool include_reasoning_in_output()
{
    const char* v = std::getenv("MIMO_INCLUDE_REASONING");
    return v != nullptr && *v != '\0' && std::string(v) != "0";
}


/** 从 JSON 对象安全读取非空字符串字段 */
inline std::string json_string_field(const nlohmann::json& obj, const char* key)
{
    if (!obj.contains(key) || obj[key].is_null()) {
        return {};
    }
    if (obj[key].is_string()) {
        return obj[key].get<std::string>();
    }
    return {};
}

/** 仅提取面向用户的正文（content / text） */
inline std::string extract_visible_text_from_part(const nlohmann::json& part)
{
    const std::string content = json_string_field(part, "content");
    if (!content.empty()) {
        return content;
    }
    return json_string_field(part, "text");
}

/** 含思考链；用于调试或 MIMO_INCLUDE_REASONING=1 */
inline std::string extract_text_from_part(const nlohmann::json& part)
{
    const std::string visible = extract_visible_text_from_part(part);
    if (!visible.empty()) {
        return visible;
    }
    if (!include_reasoning_in_output()) {
        return {};
    }
    const std::string reasoning = json_string_field(part, "reasoning_content");
    if (!reasoning.empty()) {
        return reasoning;
    }
    return json_string_field(part, "reasoning");
}

/** 从单条 choice 或完整 completion JSON 提取助手回复 */
inline std::string extract_text_from_choice(const nlohmann::json& choice)
{
    if (choice.contains("delta") && choice["delta"].is_object()) {
        const std::string t = extract_text_from_part(choice["delta"]);
        if (!t.empty()) {
            return t;
        }
    }
    if (choice.contains("message") && choice["message"].is_object()) {
        const std::string t = extract_text_from_part(choice["message"]);
        if (!t.empty()) {
            return t;
        }
    }
    return json_string_field(choice, "text");
}

/** 解析整段 HTTP 响应体；优先可见正文，无正文且允许时才用思考链 */
inline std::string extract_text_from_completion_body(const std::string& body)
{
    if (body.empty()) {
        return {};
    }

    auto merge_choice = [](const nlohmann::json& choice) -> std::string {
        if (choice.contains("message") && choice["message"].is_object()) {
            const std::string v =
                extract_visible_text_from_part(choice["message"]);
            if (!v.empty()) {
                return v;
            }
        }
        if (include_reasoning_in_output()) {
            return extract_text_from_choice(choice);
        }
        return {};
    };

    // 先尝试整段 JSON
    try {
        const auto j = nlohmann::json::parse(body);
        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
            const std::string t = merge_choice(j["choices"][0]);
            if (!t.empty()) {
                return t;
            }
        }
    } catch (...) {
    }

    // 再扫描 SSE data: 行拼接
    std::string merged;
    size_t pos = 0;
    while (pos < body.size()) {
        const size_t data_pos = body.find("data:", pos);
        if (data_pos == std::string::npos) {
            break;
        }
        size_t line_end = body.find('\n', data_pos);
        if (line_end == std::string::npos) {
            line_end = body.size();
        }
        std::string_view line(body.data() + data_pos, line_end - data_pos);
        if (line.size() > 5) {
            std::string_view payload = line.substr(5);
            while (!payload.empty() && payload.front() == ' ') {
                payload.remove_prefix(1);
            }
            if (payload != "[DONE]" && !payload.empty() && payload.front() == '{') {
                try {
                    const auto j = nlohmann::json::parse(payload);
                    if (j.contains("choices") && j["choices"].is_array() &&
                        !j["choices"].empty()) {
                        merged += merge_choice(j["choices"][0]);
                    }
                } catch (...) {
                }
            }
        }
        pos = line_end + 1;
    }
    return merged;
}

}  // namespace llm_mimo
