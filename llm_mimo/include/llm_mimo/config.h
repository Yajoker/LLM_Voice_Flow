#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace llm_mimo {

/** 小米 MiMo 开放平台连接与生成参数（OpenAI 兼容） */
struct MimoConfig {
    /**
     * OpenAI 兼容 Base URL。
     * 须与控制台「专属 Base URL」一致（常见为 token-plan-*.xiaomimimo.com/v1），
     * 勿与文档示例 api.mimo-v2.com / api.xiaomimimo.com 混用，否则 401。
     */
    std::string base_url = "https://token-plan-sgp.xiaomimimo.com/v1";
    /** 默认对话模型，见 supported_models.h（套餐含 MiMo-V2.5，不含 Flash） */
    std::string model = "mimo-v2.5";
    /** 环境变量名，用于读取 API Key */
    std::string api_key_env = "MIMO_API_KEY";
    /** 解析后的密钥（运行时填充，勿写入日志） */
    std::string api_key;

    double temperature = 0.7;
    int max_tokens = 256;

    long connect_timeout_ms = 5000;
    /** SSE 读空闲超时：超过该时间未收到数据则中断 */
    long read_idle_timeout_ms = 30000;
    /** 单轮对话总超时 */
    long turn_timeout_ms = 60000;
};

/**
 * 加载配置：默认值 <- ini 文件 <- 环境变量覆盖
 * @param ini_path  显式指定 ini；为空则依次尝试 MIMO_CONFIG_PATH、./config/mimo.ini
 * @return 成功时返回配置；失败时返回 nullopt 且 error_out 含原因
 */
std::optional<MimoConfig> load_config(std::string* error_out,
                                      const std::string& ini_path = "");

/** 返回 chat/completions 完整 URL */
std::string chat_completions_url(const MimoConfig& cfg);

/** 用于日志的脱敏配置摘要（不含 api_key） */
std::string config_summary_redacted(const MimoConfig& cfg);

}  // namespace llm_mimo
