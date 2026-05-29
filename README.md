# LLM_Voice_Flow —— 模块化低延迟语音交互系统（云端 LLM）

## 项目概述

本项目是一套**模块化、松耦合**的智能语音交互系统，数据流为：

```
[麦克风] → voice(流式 ASR) →(ZMQ)→ llm_mimo(云端 LLM 网关) →(ZMQ)→ tts(语音合成+双缓冲播放)
```

- **voice**：基于 sherpa-onnx 的流式 ASR，麦克风/音频 → 文本；
- **llm_mimo**：云端 LLM 网关，通过 ZeroMQ(REQ/REP) 接收文本，调用云端大模型（OpenAI 兼容、SSE 流式），做流式 SSE 解析与分句，再通过 ZeroMQ(PUSH/PULL) 推给 TTS；
- **tts**：语音合成 + 双缓冲队列播放；
- **zmq-comm-kit**：封装 ZeroMQ 的通信底座（Client/Server/Pusher/Puller）。

> 说明：项目已**全面转向云端 LLM API**，原端侧大模型部分（RKLLM / RK3576 NPU / 端侧量化部署）已移除，LLM 一律走云端（OpenAI 兼容 SSE）。

## 技术栈

Linux、C++17、sherpa-onnx(ASR)、TTS、ZeroMQ、CMake、多线程、OpenAI 兼容 SSE 流式。

## 核心特性

- 🔧 **模块化架构**：ASR / LLM 网关 / TTS 通过 ZeroMQ 松耦合通信；
- ⚡ **低延迟**：流式 ASR + 流式分句 + 双缓冲 TTS 队列，首句尽早出声；
- ⏹️ **可打断（barge-in）**：LLM 网关支持 `cancel()`，用户开口即可打断当前播报。

## 业务化升级：车载语音 Agent

本仓库正在把"语音问答管道"升级为**面向真实业务场景的车载语音 Agent**（保留并复用现有 C++ 主链路，"思考层"抽为独立 Python Agent 编排服务，二者以 OpenAI 兼容 SSE 对接）。

完整设计（业务场景、系统架构、关键技术方案、接口契约、落地路线、简历表达、可信度与风险）见：

**[`docs/voice_agent_design.md`](docs/voice_agent_design.md)**
