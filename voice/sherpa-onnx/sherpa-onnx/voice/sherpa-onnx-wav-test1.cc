// 从 WAV 文件做流式 ASR，并通过 ZMQ 走完整 LLM/TTS 流水线（供 SSH 远程：笔记本录音上传后测试）

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <clocale>
#include <cwctype>
#include <iostream>
#include <string>
#include <vector>

#include "ZmqClient.h"
#include "sherpa-onnx/csrc/online-recognizer.h"
#include "sherpa-onnx/csrc/parse-options.h"
#include "sherpa-onnx/csrc/wave-reader.h"

static bool stop = false;

static void Handler(int32_t /*sig*/) {
  stop = true;
  fprintf(stderr, "\n已中断\n");
}

static std::string tolowerUnicode(const std::string &input_str) {
  std::setlocale(LC_ALL, "");
  std::wstring input_wstr(input_str.size() + 1, L'\0');
  std::mbstowcs(&input_wstr[0], input_str.c_str(), input_str.size());
  std::wstring lowercase_wstr;
  for (wchar_t wc : input_wstr) {
    if (std::iswupper(wc)) {
      lowercase_wstr += std::towlower(wc);
    } else {
      lowercase_wstr += wc;
    }
  }
  std::string lowercase_str(input_str.size() + 1, '\0');
  std::wcstombs(&lowercase_str[0], lowercase_wstr.c_str(),
                lowercase_wstr.size());
  return lowercase_str;
}

/** 将整段音频分块送入流式识别器（模拟麦克风流） */
static void FeedWaveformChunked(sherpa_onnx::OnlineStream *stream,
                                int32_t sample_rate,
                                const std::vector<float> &samples) {
  const int32_t chunk =
      std::max<int32_t>(1, static_cast<int32_t>(0.1f * sample_rate));
  for (size_t offset = 0; offset < samples.size(); offset += chunk) {
    const size_t n = std::min(static_cast<size_t>(chunk), samples.size() - offset);
    stream->AcceptWaveform(sample_rate, samples.data() + offset,
                           static_cast<int32_t>(n));
  }
}

static bool RecognizeOneFile(sherpa_onnx::OnlineRecognizer *recognizer,
                             sherpa_onnx::OnlineRecognizerConfig *config,
                             const std::string &wav_path,
                             zmq_component::ZmqClient *llm_client,
                             zmq_component::ZmqClient *block_client) {
  int32_t sample_rate = -1;
  bool is_ok = false;
  const std::vector<float> samples =
      sherpa_onnx::ReadWave(wav_path, &sample_rate, &is_ok);
  if (!is_ok) {
    fprintf(stderr, "无法读取 WAV: %s\n", wav_path.c_str());
    return false;
  }

  fprintf(stderr, "[wav] %s  采样率=%d  时长=%.2fs\n", wav_path.c_str(),
          sample_rate, samples.size() / static_cast<float>(sample_rate));

  auto stream = recognizer->CreateStream();
  FeedWaveformChunked(stream.get(), sample_rate, samples);

  std::vector<float> tail_paddings(
      static_cast<size_t>(0.8f * sample_rate));
  stream->AcceptWaveform(sample_rate, tail_paddings.data(),
                         static_cast<int32_t>(tail_paddings.size()));
  stream->InputFinished();

  std::string last_text;
  while (!stop) {
    while (recognizer->IsReady(stream.get())) {
      recognizer->DecodeStream(stream.get());
    }

    auto text = recognizer->GetResult(stream.get()).text;
    bool is_endpoint = recognizer->IsEndpoint(stream.get());

    if (is_endpoint &&
        !config->model_config.paraformer.encoder.empty()) {
      std::vector<float> extra(static_cast<size_t>(1.0f * sample_rate));
      stream->AcceptWaveform(sample_rate, extra.data(),
                             static_cast<int32_t>(extra.size()));
      while (recognizer->IsReady(stream.get())) {
        recognizer->DecodeStream(stream.get());
      }
      text = recognizer->GetResult(stream.get()).text;
    }

    if (!text.empty() && last_text != text) {
      last_text = text;
      fprintf(stderr, "[asr] %s\n", text.c_str());
    }

    if (is_endpoint) {
      if (!text.empty()) {
        auto response = llm_client->request(text);
        std::cout << "[llm -> voice] received: " << response << std::endl;

        auto block_response = block_client->request("block");
        std::cout << "[tts -> voice] received: " << block_response
                  << std::endl;
      } else {
        fprintf(stderr, "[wav] 端点触发但识别为空，请检查 WAV 是否有人声\n");
      }
      return true;
    }
  }
  return false;
}

int32_t main(int32_t argc, char *argv[]) {
  signal(SIGINT, Handler);
  zmq_component::ZmqClient llm_client;
  zmq_component::ZmqClient block_client("tcp://localhost:6677");

  const char *kUsageMessage = R"usage(
从 WAV 文件做流式识别，识别结束后经 ZMQ 请求 LLM(6666) 与 TTS 播放握手(6677)。
适用于 SSH 远程：在 Windows 上录音为 WAV，上传到服务器 test_wavs 后运行。

用法:
  ./bin/sherpa-onnx-wav-test1 \
    --tokens=... --encoder=... --decoder=... --joiner=... \
    /path/to/your.wav [/path/to/another.wav ...]

WAV 要求: 单声道 16-bit PCM；采样率任意（内部会重采样）。
建议笔记本录音: 16kHz 单声道，可用 ffmpeg 转换。
)usage";

  sherpa_onnx::ParseOptions po(kUsageMessage);
  sherpa_onnx::OnlineRecognizerConfig config;
  config.Register(&po);
  po.Read(argc, argv);

  if (po.NumArgs() < 1) {
    po.PrintUsage();
    fprintf(stderr, "请至少提供一个 .wav 文件路径\n");
    return EXIT_FAILURE;
  }

  fprintf(stderr, "%s\n", config.ToString().c_str());
  if (!config.Validate()) {
    fprintf(stderr, "配置无效\n");
    return EXIT_FAILURE;
  }

  sherpa_onnx::OnlineRecognizer recognizer(config);

  for (int32_t i = 1; i <= po.NumArgs() && !stop; ++i) {
    RecognizeOneFile(&recognizer, &config, po.GetArg(i), &llm_client,
                     &block_client);
  }

  return 0;
}
