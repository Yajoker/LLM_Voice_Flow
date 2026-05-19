/**
 * llm_mimo 服务入口
 *
 * 用法:
 *   llm_mimo_server                         # T1 依赖自检
 *   llm_mimo_server --show-config           # 加载并打印脱敏配置
 *   llm_mimo_server serve                    # T4-T6 语音网关（VOICE↔MiMo↔TTS）
 *   llm_mimo_server chat "你好"              # T3 流式对话测试
 *   llm_mimo_server --config path chat "…"
 */
#include <curl/curl.h>
#include <zmq.h>

#include "llm_mimo/config.h"
#include "llm_mimo/zmq_gateway.h"
#include "llm_mimo/mimo_client.h"
#include "llm_mimo/response_extract.h"
#include "llm_mimo/sse_parser.h"
#include "llm_mimo/supported_models.h"
#include "llm_mimo/version.h"

#include <fstream>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* prog)
{
    std::cerr << "用法:\n"
              << "  " << prog << " [--config <mimo.ini>] [--show-config]\n"
              << "  " << prog << " [--config <mimo.ini>] --verify-key\n"
              << "  " << prog << " [--config <mimo.ini>] --list-models\n"
              << "  " << prog << " [--config <mimo.ini>] serve\n"
              << "  " << prog << " [--config <mimo.ini>] chat \"用户输入\"\n"
              << "  " << prog << " --test-sse [<sample.sse>]\n";
}

bool probe_zmq_context()
{
    void* ctx = zmq_ctx_new();
    if (ctx == nullptr) {
        return false;
    }
    const int rc = zmq_ctx_term(ctx);
    return rc == 0;
}

int run_self_check()
{
    std::cout << "llm_mimo_server v" << LLM_MIMO_VERSION_STRING
              << "（小米 MiMo 流式 LLM）" << std::endl;

    curl_version_info_data* curl_ver = curl_version_info(CURLVERSION_NOW);
    if (curl_ver != nullptr && curl_ver->version != nullptr) {
        std::cout << "[llm_mimo] libcurl: " << curl_ver->version << std::endl;
    }

    int major = 0;
    int minor = 0;
    int patch = 0;
    zmq_version(&major, &minor, &patch);
    std::cout << "[llm_mimo] libzmq: " << major << "." << minor << "." << patch
              << std::endl;

    if (!probe_zmq_context()) {
        std::cerr << "[llm_mimo] ZeroMQ 上下文探测失败" << std::endl;
        return EXIT_FAILURE;
    }

#ifdef HAS_ZMQ_COMPONENT
    std::cout << "[llm_mimo] zmq-comm-kit: 已链接" << std::endl;
#endif

    std::cout << "[llm_mimo] 自检通过" << std::endl;
    return EXIT_SUCCESS;
}

int run_verify_key(const std::string& ini_path)
{
    std::string err;
    auto cfg_opt = llm_mimo::load_config(&err, ini_path);
    if (!cfg_opt.has_value()) {
        std::cerr << "[llm_mimo] 配置加载失败: " << err << std::endl;
        return EXIT_FAILURE;
    }

    const auto& cfg = *cfg_opt;
    std::cout << "[llm_mimo] Key 长度: " << cfg.api_key.size();
    if (cfg.api_key.size() >= 4) {
        std::cout << "，前缀: " << cfg.api_key.substr(0, 4) << "...";
    }
    std::cout << std::endl;
    std::cout << "[llm_mimo] 探测: " << llm_mimo::chat_completions_url(cfg)
              << std::endl;

    llm_mimo::MimoStreamClient client(cfg);
    const auto result = client.verify_api_key();
    if (result.ok) {
        std::cout << "[llm_mimo] API Key 有效，http=" << result.http_code
                  << std::endl;
        return EXIT_SUCCESS;
    }

    std::cerr << "[llm_mimo] API Key 校验失败 http=" << result.http_code
              << " type=" << result.error_type << " msg=" << result.error_message
              << std::endl;
    if (result.http_code == 400 &&
        result.error_message.find("Not supported model") != std::string::npos) {
        std::cerr << "[llm_mimo] 当前 model 不在套餐权益内，对话请使用:\n"
                  << "  " << llm_mimo::supported_chat_models_hint() << "\n"
                  << "  运行 ./llm_mimo_server --list-models 查看对照表\n";
    }
    if (result.http_code == 401) {
        std::cerr << "[llm_mimo] 请确认:\n"
                  << "  1) API Key 与 base_url 必须来自同一控制台页面的「专属 Base URL」\n"
                  << "  2) export MIMO_BASE_URL=\"控制台显示的 OpenAI 兼容地址\"\n"
                  << "     例如 https://token-plan-sgp.xiaomimimo.com/v1\n"
                  << "  3) export MIMO_API_KEY=\"你的 tp- 密钥\"（无多余空格）\n"
                  << "  4) 不要用 api.xiaomimimo.com，除非控制台里写的就是它\n";
    }
    return EXIT_FAILURE;
}

int run_list_models()
{
    std::cout << "【套餐权益 · 对话模型】用于 llm_mimo chat / verify-key\n";
    for (const auto& m : llm_mimo::PlanModels::chat_models()) {
        std::cout << "  - " << m << std::endl;
    }
    std::cout << "默认推荐（语音交互）: " << llm_mimo::kDefaultVoiceChatModel
              << std::endl;

    std::cout << "\n【套餐权益 · TTS 模型】由 tts 模块使用，勿用于本程序\n";
    for (const auto& m : llm_mimo::PlanModels::tts_models()) {
        std::cout << "  - " << m << std::endl;
    }

    std::cout << "\n控制台展示名 → API model 字段:\n";
    for (const auto& [display, api_id] : llm_mimo::PlanModels::display_names()) {
        std::cout << "  " << display << "  →  " << api_id << std::endl;
    }
    return EXIT_SUCCESS;
}

int run_show_config(const std::string& ini_path)
{
    std::string err;
    auto cfg = llm_mimo::load_config(&err, ini_path);
    if (!cfg.has_value()) {
        std::cerr << "[llm_mimo] 配置加载失败: " << err << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << llm_mimo::config_summary_redacted(*cfg) << std::endl;
    std::cout << "[llm_mimo] endpoint: "
              << llm_mimo::chat_completions_url(*cfg) << std::endl;
    return EXIT_SUCCESS;
}

int run_test_sse(const std::string& sample_path)
{
    std::ifstream ifs(sample_path, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[llm_mimo] 无法打开 SSE 样例: " << sample_path << std::endl;
        return EXIT_FAILURE;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    const std::string raw = oss.str();

    bool done_once = false;
    llm_mimo::SseParser parser([&done_once](const llm_mimo::SseEvent& ev) {
        switch (ev.kind) {
            case llm_mimo::SseEventKind::Delta:
                std::cout << ev.content << std::flush;
                break;
            case llm_mimo::SseEventKind::Done:
                if (!done_once) {
                    done_once = true;
                    std::cout << "\n[llm_mimo] SSE 样例解析完成 [DONE]" << std::endl;
                }
                break;
            case llm_mimo::SseEventKind::Error:
                std::cerr << "\n[解析错误] " << ev.error_type << ": " << ev.content
                          << std::endl;
                break;
        }
    });

    std::cout << "[助手] " << std::flush;
    parser.feed(raw.data(), raw.size());
    parser.flush();
    return EXIT_SUCCESS;
}

int run_serve(const std::string& ini_path)
{
    std::string err;
    auto cfg_opt = llm_mimo::load_config(&err, ini_path);
    if (!cfg_opt.has_value()) {
        std::cerr << "[llm_mimo] 配置加载失败: " << err << std::endl;
        return EXIT_FAILURE;
    }

    llm_mimo::ZmqEndpoints ep;
    if (const char* v = std::getenv("MIMO_VOICE_BIND"); v != nullptr && *v) {
        ep.voice_rep_bind = v;
    }
    if (const char* v = std::getenv("MIMO_TTS_PUSH"); v != nullptr && *v) {
        ep.tts_push_connect = v;
    }

    llm_mimo::VoiceGateway gateway(*cfg_opt, ep);
    gateway.run();
    return EXIT_SUCCESS;
}

int run_chat_stream(const std::string& ini_path, const std::string& user_text)
{
    std::string err;
    auto cfg_opt = llm_mimo::load_config(&err, ini_path);
    if (!cfg_opt.has_value()) {
        std::cerr << "[llm_mimo] 配置加载失败: " << err << std::endl;
        return EXIT_FAILURE;
    }

    llm_mimo::MimoStreamClient client(*cfg_opt);

    std::vector<llm_mimo::ChatMessage> messages;
    messages.push_back(
        {"system", "你是简洁的语音助手，用口语化中文回答，尽量控制在两句话以内。"});
    messages.push_back({"user", user_text});

    std::cout << "[用户] " << user_text << "\n[助手] " << std::flush;
    if (!llm_mimo::include_reasoning_in_output()) {
        std::cerr << "[llm_mimo] 仅输出正文（思考链已隐藏，调试可设 MIMO_INCLUDE_REASONING=1）\n";
    }

    const auto result = client.chat_stream(
        messages,
        [](std::string_view delta, bool is_done,
           const llm_mimo::StreamResult* stream_err) {
            if (stream_err != nullptr && !stream_err->error_message.empty()) {
                std::cerr << "\n[流错误] " << stream_err->error_type << ": "
                          << stream_err->error_message << std::endl;
                return;
            }
            if (!delta.empty()) {
                std::cout << delta << std::flush;
            }
            if (is_done) {
                std::cout << std::endl;
            }
        });

    if (!result.ok) {
        std::cerr << "[llm_mimo] 请求失败"
                  << " http=" << result.http_code << " type=" << result.error_type
                  << " msg=" << result.error_message;
        if (!result.raw_body_snippet.empty()) {
            std::cerr << " body=" << result.raw_body_snippet;
        }
        std::cerr << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "\n[llm_mimo] 完成, http=" << result.http_code;
    if (std::getenv("MIMO_DEBUG")) {
        std::cout << "（调试: MIMO_DEBUG=1 可查看原始响应）";
    }
    std::cout << std::endl;
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv)
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        std::cerr << "[llm_mimo] curl_global_init 失败" << std::endl;
        return EXIT_FAILURE;
    }

    std::string ini_path;
    std::string mode;
    std::string chat_text;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            ini_path = argv[++i];
        } else if (arg == "--show-config") {
            mode = "show-config";
        } else if (arg == "--verify-key") {
            mode = "verify-key";
        } else if (arg == "--list-models") {
            mode = "list-models";
        } else if (arg == "chat" && i + 1 < argc) {
            mode = "chat";
            chat_text = argv[++i];
        } else if (arg == "serve") {
            mode = "serve";
        } else if (arg == "--test-sse") {
            mode = "test-sse";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                chat_text = argv[++i];
            }
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            curl_global_cleanup();
            return EXIT_SUCCESS;
        } else {
            std::cerr << "未知参数: " << arg << std::endl;
            print_usage(argv[0]);
            curl_global_cleanup();
            return EXIT_FAILURE;
        }
    }

    int rc = EXIT_SUCCESS;
    if (mode == "show-config") {
        rc = run_show_config(ini_path);
    } else if (mode == "verify-key") {
        rc = run_verify_key(ini_path);
    } else if (mode == "list-models") {
        rc = run_list_models();
    } else if (mode == "serve") {
        rc = run_serve(ini_path);
    } else if (mode == "chat") {
        if (chat_text.empty()) {
            std::cerr << "chat 模式需要提供用户文本" << std::endl;
            print_usage(argv[0]);
            rc = EXIT_FAILURE;
        } else {
            rc = run_chat_stream(ini_path, chat_text);
        }
    } else if (mode == "test-sse") {
        const std::string sample = chat_text.empty()
                                       ? "../testdata/sample_stream.sse"
                                       : chat_text;
        rc = run_test_sse(sample);
    } else {
        rc = run_self_check();
    }

    curl_global_cleanup();
    return rc;
}
