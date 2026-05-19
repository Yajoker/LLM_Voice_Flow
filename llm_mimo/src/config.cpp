#include "llm_mimo/config.h"
#include "llm_mimo/supported_models.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace llm_mimo {
namespace {

std::string trim(std::string_view s)
{
    size_t begin = 0;
    while (begin < s.size() &&
           (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
        --end;
    }
    return std::string(s.substr(begin, end - begin));
}

/** 规范化 API Key：去空白、去掉误粘贴的首尾引号 */
std::string normalize_api_key(std::string key)
{
    key = trim(key);
    while (!key.empty() && (key.back() == '\n' || key.back() == '\r')) {
        key.pop_back();
    }
    if (key.size() >= 2) {
        const char a = key.front();
        const char b = key.back();
        if ((a == '"' && b == '"') || (a == '\'' && b == '\'')) {
            key = trim(std::string_view(key).substr(1, key.size() - 2));
        }
    }
    return key;
}

bool parse_long(const std::string& v, long* out)
{
    try {
        size_t pos = 0;
        long val = std::stol(v, &pos);
        if (pos != v.size()) {
            return false;
        }
        *out = val;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_double(const std::string& v, double* out)
{
    try {
        size_t pos = 0;
        double val = std::stod(v, &pos);
        if (pos != v.size()) {
            return false;
        }
        *out = val;
        return true;
    } catch (...) {
        return false;
    }
}

void apply_ini_kv(MimoConfig& cfg, const std::string& section,
                  const std::string& key, const std::string& value)
{
    if (section == "api" || section.empty()) {
        if (key == "base_url") {
            cfg.base_url = value;
        } else if (key == "model") {
            cfg.model = value;
        } else if (key == "api_key_env") {
            cfg.api_key_env = value;
        } else if (key == "temperature") {
            parse_double(value, &cfg.temperature);
        } else if (key == "max_tokens") {
            long v = 0;
            if (parse_long(value, &v)) {
                cfg.max_tokens = static_cast<int>(v);
            }
        }
    }
    if (section == "timeout" || section.empty()) {
        if (key == "connect_timeout_ms") {
            parse_long(value, &cfg.connect_timeout_ms);
        } else if (key == "read_idle_timeout_ms") {
            parse_long(value, &cfg.read_idle_timeout_ms);
        } else if (key == "turn_timeout_ms") {
            parse_long(value, &cfg.turn_timeout_ms);
        }
    }
}

bool load_ini_file(const std::string& path, MimoConfig& cfg, std::string* error_out)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        if (error_out != nullptr) {
            *error_out = "无法打开配置文件: " + path;
        }
        return false;
    }

    std::string section;
    std::string line;
    int line_no = 0;
    while (std::getline(ifs, line)) {
        ++line_no;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trim(trimmed.substr(1, trimmed.size() - 2));
            continue;
        }
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            if (error_out != nullptr) {
                *error_out = "配置文件格式错误（缺少 =）: " + path + ":" +
                             std::to_string(line_no);
            }
            return false;
        }
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));
        apply_ini_kv(cfg, section, key, value);
    }
    return true;
}

void apply_env_overrides(MimoConfig& cfg)
{
    if (const char* v = std::getenv("MIMO_BASE_URL"); v != nullptr && *v != '\0') {
        cfg.base_url = v;
    }
    if (const char* v = std::getenv("MIMO_MODEL"); v != nullptr && *v != '\0') {
        cfg.model = v;
    }
    if (const char* v = std::getenv("MIMO_API_KEY"); v != nullptr && *v != '\0') {
        cfg.api_key = normalize_api_key(v);
    }
    if (const char* v = std::getenv("MIMO_TEMPERATURE"); v != nullptr && *v != '\0') {
        parse_double(v, &cfg.temperature);
    }
    if (const char* v = std::getenv("MIMO_MAX_TOKENS"); v != nullptr && *v != '\0') {
        long n = 0;
        if (parse_long(v, &n)) {
            cfg.max_tokens = static_cast<int>(n);
        }
    }
}

bool resolve_api_key(MimoConfig& cfg, std::string* error_out)
{
    if (!cfg.api_key.empty()) {
        return true;
    }
    if (cfg.api_key_env.empty()) {
        if (error_out != nullptr) {
            *error_out = "未配置 api_key_env";
        }
        return false;
    }
    const char* v = std::getenv(cfg.api_key_env.c_str());
    if (v == nullptr || *v == '\0') {
        if (error_out != nullptr) {
            *error_out = "环境变量 " + cfg.api_key_env +
                         " 未设置或为空，请 export 后重试";
        }
        return false;
    }
    cfg.api_key = normalize_api_key(v);
    if (cfg.api_key.empty()) {
        if (error_out != nullptr) {
            *error_out = "环境变量 " + cfg.api_key_env + " 内容为空（仅空白或引号）";
        }
        return false;
    }
    return true;
}

std::string default_ini_search_path()
{
    return "config/mimo.ini";
}

}  // namespace

std::optional<MimoConfig> load_config(std::string* error_out,
                                      const std::string& ini_path)
{
    MimoConfig cfg;
    std::string ini_err;

    std::string path = ini_path;
    if (path.empty()) {
        if (const char* env_path = std::getenv("MIMO_CONFIG_PATH");
            env_path != nullptr && *env_path != '\0') {
            path = env_path;
        } else {
            path = default_ini_search_path();
        }
    }

    if (load_ini_file(path, cfg, &ini_err)) {
        // ini 加载成功
    } else {
        // 允许无 ini，仅使用默认值 + 环境变量（便于 CI / 仅 env 部署）
        if (!ini_path.empty()) {
            if (error_out != nullptr) {
                *error_out = ini_err;
            }
            return std::nullopt;
        }
    }

    apply_env_overrides(cfg);

    if (!resolve_api_key(cfg, error_out)) {
        return std::nullopt;
    }

    if (cfg.base_url.empty()) {
        if (error_out != nullptr) {
            *error_out = "base_url 不能为空";
        }
        return std::nullopt;
    }

    if (!is_supported_chat_model(cfg.model)) {
        if (error_out != nullptr) {
            *error_out =
                "模型 \"" + cfg.model +
                "\" 不在套餐对话模型列表中（MiMo-V2.5-Pro / MiMo-V2.5 / "
                "MiMo-V2-Pro / MiMo-V2-Omni）。"
                " 可用 API ID: " +
                supported_chat_models_hint() +
                "。TTS 系列模型请仅用于 TTS 模块，勿用于 llm_mimo。";
        }
        return std::nullopt;
    }

    while (!cfg.base_url.empty() && cfg.base_url.back() == '/') {
        cfg.base_url.pop_back();
    }

    return cfg;
}

std::string chat_completions_url(const MimoConfig& cfg)
{
    return cfg.base_url + "/chat/completions";
}

std::string config_summary_redacted(const MimoConfig& cfg)
{
    std::ostringstream oss;
    oss << "base_url=" << cfg.base_url << ", model=" << cfg.model
        << ", temperature=" << cfg.temperature
        << ", max_tokens=" << cfg.max_tokens
        << ", connect_timeout_ms=" << cfg.connect_timeout_ms
        << ", read_idle_timeout_ms=" << cfg.read_idle_timeout_ms
        << ", turn_timeout_ms=" << cfg.turn_timeout_ms
        << ", api_key_env=" << cfg.api_key_env
        << ", api_key=***";
    return oss.str();
}

}  // namespace llm_mimo
