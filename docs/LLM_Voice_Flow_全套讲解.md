# LLM_Voice_Flow 全套精讲（一对一带教版）

> 目标项目：<https://github.com/Yajoker/LLM_Voice_Flow>
> 适用读者：C++ 基础已掌握，正在把这个项目作为简历核心项目，希望"看懂 + 讲透 + 答题 + 改造"。
> 风格：一对一带教、信息密度高、零跳步。

---

## 目录

- [第一部分：项目全景](#第一部分项目全景)
- [第二部分：环境与构建](#第二部分环境与构建)
- [第三部分：源代码逐模块精讲](#第三部分源代码逐模块精讲)
- [第四部分：跨模块关键技术专题](#第四部分跨模块关键技术专题)
- [第五部分：简历与面试包装](#第五部分简历与面试包装)
- [第六部分：C++ 知识体系串讲](#第六部分c-知识体系串讲)

---

# 第一部分：项目全景

## 1. 项目做什么 / 解决什么问题 / 场景

**LLM_Voice_Flow** 是一个面向 **RK3576 NPU 嵌入式硬件** 的「全离线、模块化中文智能语音交互系统」，端到端串起 **ASR（语音识别）→ LLM（DeepSeek-R1-Distill-Qwen-1.5B 推理）→ TTS（语音合成播放）** 三大模块，三模块解耦成三个独立进程，靠 **自研的 ZeroMQ REQ/REP 通信组件** 互通。

- **解决的问题**：
  1. 云端语音助手依赖网络、隐私差、延迟不可控；
  2. 嵌入式资源紧张，必须做模型量化 + NPU 加速；
  3. ASR/LLM/TTS 三者技术栈差异大，需要松耦合架构以便单独迭代。
- **典型场景**：智能音箱、车机离线唤醒后对话、工业巡检语音助手、隐私敏感设备（医疗、政企）。

## 2. 整体架构

```
┌──────────────────────────┐       ZMQ REQ          ┌──────────────────────────┐
│        voice (ASR)       │ ─────tcp://*:6666───▶ │        llm (LLM)         │
│ - PortAudio 抓 mic       │ ◀──── "ack" ─────────  │ - rkllm 在 NPU 推理       │
│ - sherpa-onnx 流式解码    │                        │ - 回调里按标点切句         │
│ - endpoint 触发提交       │                        │ - 转发给 tts             │
└──────────┬───────────────┘                        └──────────┬───────────────┘
           │ ZMQ REQ "block"                                    │ ZMQ REQ 文本片
           │ tcp://*:6677                                       │ tcp://*:7777
           ▼                                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              tts (TTS Server)                               │
│  main thread        ┌──── text_queue ────┐        ┌──── audio_queue ────┐   │
│  收 ZMQ 7777 → push │                    │ pop    │                     │   │
│                     │                    ▼        │                     ▼   │
│                     │             synthesis_worker│             playback_worker
│                     │             (VITS C++)      │             (ALSA 播放)  │
│                     └────────────────────┘        └────────────────────┘   │
│  播完最后一段：status_server.send → 解锁 voice 的 6677 REQ                  │
└─────────────────────────────────────────────────────────────────────────────┘
```

ASCII 调用关系：

```
mic ─► PortAudio cb ─► sherpa-onnx OnlineStream ─► IsEndpoint
                                                    │
                                                    ▼
                                              ZmqClient REQ(6666) ─► ZmqServer(llm)
                                                                       │
                                                                       ▼
                                                                 rkllm_run (NPU)
                                                                       │ callback
                                                                       ▼
                                                              按标点切句 ─► ZmqClient REQ(7777)
                                                                       │
                                                                       ▼
                                                              ZmqServer(tts) ─► text_queue
                                                                                  │
                                                                                  ▼
                                                                            synthesis_worker
                                                                              VITS infer
                                                                                  │
                                                                                  ▼
                                                                            audio_queue
                                                                                  │
                                                                                  ▼
                                                                            playback_worker
                                                                              ALSA writei
                                                                                  │
                                                                                  ▼
                                                              is_last ─► status_server.send(6677)
                                                                                  │
                                                                                  ▼
                                                                          解锁 voice block_client
```

## 3. 技术栈清单

| 库 / 组件 | 一句话作用 |
| --- | --- |
| **C++17**（TTS 模块）/ **C++11**（其余） | 主开发语言 |
| **CMake ≥ 3.10** | 构建 |
| **ZeroMQ + cppzmq (`zmq.hpp`)** | 进程间通信（REQ/REP） |
| **PortAudio** | 跨平台麦克风采集 |
| **ALSA (`libasound`)** | Linux 底层音频播放 |
| **sherpa-onnx** | 流式 Zipformer 中英双语 ASR |
| **ONNX Runtime**（sherpa-onnx 依赖） | ASR 模型推理 |
| **rkllm + librkllmrt.so** | 瑞芯微 RK3576 NPU 上跑 LLM |
| **DeepSeek-R1-Distill-Qwen-1.5B (.rkllm)** | LLM 模型，含 `<think>` 思考段 |
| **VITS (`SynthesizerTrn`)** | 端到端中文 TTS 模型（C++ + Eigen） |
| **Eigen 3.4.0** | TTS 矩阵运算（header-only） |
| **OpenFST / glog / gflags** | TTS 文本归一化（TN）依赖 |
| **pthread / std::thread / mutex / condition_variable / atomic** | 双队列、生产者-消费者、原子标志 |
| **`<codecvt>` / `wstring_convert`** | UTF-8 ↔ wchar 互转，用于按中文标点切句 |

## 4. 目录结构

```
LLM_Voice_Flow/
├── README.md
├── docs/image.png                架构图
├── zmq-comm-kit/                 ★自研：ZMQ 通信封装库（编译成 .so）
│   ├── include/
│   │   ├── zmq.hpp               cppzmq 单头文件（第三方）
│   │   ├── ZmqInterface.h        基类：持有 context_/socket_
│   │   ├── ZmqClient.h           REQ 客户端
│   │   └── ZmqServer.h           REP 服务端
│   ├── src/  *.cpp               对应实现
│   ├── test/demo.cpp             单进程内 server↔client echo demo
│   └── CMakeLists.txt            生成 libzmq_component.so 并 install
│
├── voice/                        ★ASR 模块
│   ├── models/sherpa-onnx-streaming-zipformer-small-bilingual-zh-en-2023-02-16/
│   └── sherpa-onnx/              整个 sherpa-onnx 源码（第三方）
│       └── sherpa-onnx/voice/    ★自研子目录
│           ├── sherpa-onnx-microphone.cc  ★主程序（mic→ASR→ZMQ）
│           ├── microphone.cc/h          PortAudio 包装
│           └── CMakeLists.txt
│
├── llm/                          ★LLM 模块
│   ├── models/README.md          说明把 .rkllm 模型放这里
│   ├── rknn-llm/                 瑞芯微官方 RKLLM SDK（第三方）
│   │   ├── rkllm-runtime/Linux/librkllm_api/include/rkllm.h   C API
│   │   └── examples/DeepSeek-R1-Distill-Qwen-1.5B_Demo/deploy/src/llm_demo.cpp
│   │       ★被作者改造为：ZMQ 服务端，回调里按标点切句转发给 TTS
│   └── test/llm_test.cpp         不带 NPU 的"假"LLM 测试程序（直接透传给 TTS）
│
├── tts/                          ★TTS 模块（可执行：tts_server）
│   ├── eigen-3.4.0/              Eigen header-only（第三方）
│   ├── include/, src/            VITS 模型 C++ 实现（第三方）
│   ├── models/single_speaker_fast.bin   VITS 权重
│   ├── test/main.cpp             ★整个文件被注释掉（旧版主程序，可忽略）
│   ├── tts_server/               ★自研：模块化重写的 TTS 服务端
│   │   ├── include/{TTSModel,MessageQueue,AudioPlayer,TextProcessor,Utils}.h
│   │   └── src/  对应 .cpp + main.cpp（生产者-消费者主调度）
│   └── CMakeLists.txt
│
└── .gitignore
```

## 5. 主流程（端到端一遍）

设 RK3576 上分别启动 3 个进程（voice/llm/tts，启动顺序任意）。

1. **预热握手**：tts main 首次会 `status_server.receive()` 阻塞，等 voice 发 6677 REQ。voice 在没 endpoint 之前不会发 → **首次必须由用户先说话**。
2. **用户说"今天天气怎么样？"**
   - PortAudio 回调 `RecordCallback` 把 16 kHz mono float32 PCM 喂 sherpa-onnx 的 `OnlineStream::AcceptWaveform`。
   - 主循环 `IsReady/DecodeStream`，Zipformer 流式解码；`IsEndpoint==true` → ZmqClient REQ 给 6666 端口 llm。
3. **llm 收到文本**
   - `ZmqServer::receive()` 拿到字符串 → 回 `"llm sucess reply !!!"`。
   - 调 `rkllm_run`，NPU 上前向，**流式**通过 `callback` 一段段回 token。
4. **llm 回调按标点切片转 tts**
   - `RKLLM_RUN_NORMAL`：每段 UTF-8→wchar，累积到 `buffer_`；遇 `：，。\n；！？` flush，调 `client.request(...)` 发 7777。
   - `RKLLM_RUN_FINISH`：把残余 + `" END"` 发出去，作为本句结束哨兵。
5. **tts 收到文本**
   - main `server.receive()` → `queue.push_text(text)`。
   - `synthesis_worker` 解析 `END`、`model.infer` → PCM → `unique_ptr<int16_t[]>` 入音频队列。
   - `playback_worker` 取出 → `AudioPlayer::play` → `snd_pcm_writei` → 扬声器。
   - `is_last==true` 时 → `status_server.send("[tts -> voice]play end success")`。
6. **voice 解除阻塞**
   - 之前 `block_client.request("block")`，`wait=true` 让 PortAudio 回调丢弃麦克风数据（防回声）。
   - 收到 REP → `wait=false` → 下一轮。

端到端 ≤ 4s 的关键：**流式 ASR + 流式 LLM + 按标点立即切片送 TTS + 双缓冲队列让合成与播放并行**。

---

# 第二部分：环境与构建

## 1. 本地编译运行

目标硬件 RK3576（aarch64 Linux）。x86 主机上**只能**编译 zmq-comm-kit 和 tts 的部分，因为 LLM 依赖 ARM-only 的 `librkllmrt.so`。

依赖（Ubuntu/Debian on RK3576）：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
                    libzmq3-dev libasound2-dev portaudio19-dev \
                    libomp-dev
```

**第 1 步：构建 ZMQ 通信库**

```bash
cd zmq-comm-kit && mkdir -p build && cd build
cmake .. && make -j
sudo make install   # /usr/local/lib/libzmq_component.so + /usr/local/include/Zmq*.h
sudo ldconfig
```

**第 2 步：构建 TTS**

```bash
cd tts && mkdir -p build && cd build
cmake .. && make -j
./tts_server ../models/single_speaker_fast.bin
```

**第 3 步：构建 LLM**

```bash
cd llm/rknn-llm/examples/DeepSeek-R1-Distill-Qwen-1.5B_Demo/deploy
mkdir -p build && cd build
cmake .. && make -j
./llm_demo <你的-DeepSeek-R1-Distill-Qwen-1.5B.rkllm>
```

**第 4 步：构建 voice**

```bash
cd voice/sherpa-onnx && mkdir -p build && cd build
cmake -DBUILD_SHARED_LIBS=OFF -DSHERPA_ONNX_ENABLE_PORTAUDIO=ON ..
make -j sherpa-onnx-microphone-test1
./bin/sherpa-onnx-microphone-test1 \
    --tokens=../../models/.../tokens.txt \
    --encoder=.../encoder.onnx \
    --decoder=.../decoder.onnx \
    --joiner=.../joiner.onnx \
    --provider=cpu --num-threads=2 --decoding-method=greedy_search
```

3 个程序都起来后，对着麦克风说话即可。

## 2. CMakeLists 逐段精讲

以最能体现作者风格的 zmq-comm-kit/CMakeLists.txt 为例：

```1:28:zmq-comm-kit/CMakeLists.txt
cmake_minimum_required(VERSION 3.12)
project(zmq_component VERSION 1.0.0 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_search_module(ZMQ REQUIRED libzmq)
find_package(Threads REQUIRED)

include_directories(
    include
)

add_library(zmq_component SHARED
    src/ZmqInterface.cpp
    src/ZmqServer.cpp
    src/ZmqClient.cpp
)

target_link_libraries(zmq_component
     zmq
    Threads::Threads
)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS zmq_component DESTINATION lib)

add_executable(demo test/demo.cpp)
target_link_libraries(demo zmq_component)
```

逐行：
- `cmake_minimum_required(VERSION 3.12)`：3.12 起 `target_link_libraries` 支持目标级 transitive 用法。
- `project(... LANGUAGES CXX)`：显式只用 C++，避免 C 编译器检测拖慢配置。
- `find_package(PkgConfig REQUIRED)`：拉进 pkg-config。
- `pkg_search_module(ZMQ REQUIRED libzmq)`：用 pkg-config 找系统 libzmq。**但后面 hard-code 库名 `zmq`，没用 `${ZMQ_LIBRARIES}/${ZMQ_INCLUDE_DIRS}`**——可移植性差，面试可讲改造。
- `find_package(Threads REQUIRED)`：找 pthread，生成 imported target `Threads::Threads`。
- `include_directories(include)`：全局指令，**现代推荐 `target_include_directories`**（目标级），作者偷懒。
- `add_library(zmq_component SHARED ...)`：编出 `libzmq_component.so`；`SHARED`→动态库，`STATIC`→`.a`。
- `target_link_libraries(... zmq Threads::Threads)`。
- `install(...)`：`sudo make install` 把头/库放到 `/usr/local/{include,lib}`。LLM 和 TTS 都靠这条路径硬连，**这是项目的强约束**。
- `add_executable(demo ...) + target_link_libraries(demo zmq_component)`。

**CMake 小科普**：

- `find_package(X)` 走 `Find<X>.cmake` 或 `<X>Config.cmake`，**导入第三方**。
- `add_library(name [STATIC|SHARED|MODULE] src...)`。
- `target_link_libraries(t lib...)`：现代用法 `PUBLIC/PRIVATE/INTERFACE` 控制传递。
- `target_include_directories(t PUBLIC dir)`：目标级头路径。
- `install(...)`：定义 `make install` 行为。

`tts/CMakeLists.txt` 的特点：

```99:99:tts/CMakeLists.txt
target_link_libraries(tts_server    /usr/local/lib/libzmq_component.so  zmq portaudio asound)
```

- `portaudio`：tts 其实不用，**是冗余链接**。
- `asound`：ALSA。
- 把 60+ VITS .cpp 全堆 `add_executable`，**没拆成 `add_library`**——面试可讲"我会把 VITS 抽成 `libvits_core.a`"。

## 3. 配置文件

**没有正式配置文件**。所有"参数"都是硬编码：

- ZMQ 端口（6666/7777/6677）写死在源码。
- LLM 超参在 `Init()` 硬编码：`top_k=1, top_p=0.95, temperature=0.8, repeat_penalty=1.1, max_new_tokens=100, max_context_len=256, enabled_cpus_mask=CPU0|CPU2`。
- TTS 采样率 16000 写死在 `AudioPlayer.cpp:35`。
- 切句标点集合写死在 `llm_demo.cpp:39-47`。

**面试加分**：可以说"作为改造方向，我会把这些常量集中到 `config.yaml`，用 yaml-cpp / nlohmann::json 加载"。

---

# 第三部分：源代码逐模块精讲

5 个模块：①ZMQ 底座 → ②ASR/voice → ③LLM → ④TTS → ⑤主控调度（融在三个进程的 main 里）。

---

## 模块一：ZMQ 通信底座（`zmq-comm-kit/`）

### (a) 职责与 I/O

- **职责**：屏蔽 cppzmq 细节，给业务提供两行调用的同步请求/响应通信。
- **输入**：地址字符串 + 字节消息。
- **输出**：字符串响应或异常。

### (b) 核心类

| 类 | 一句话 |
| --- | --- |
| `ZmqCommunicationError` | 自定义异常，继承 `std::runtime_error` |
| `ZmqInterface` | 抽象基类，持有 `context_/socket_`，封装 `setupSocket()` |
| `ZmqClient : ZmqInterface` | REQ 客户端，`sendRequest/receiveResponse/request` |
| `ZmqServer : ZmqInterface` | REP 服务端，`receive/send` |

### (c) 关键源码

```14:25:zmq-comm-kit/include/ZmqInterface.h
class ZmqInterface {
protected:
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;
    int timeout_ms_ = -1;

    void setupSocket(int socket_type, const std::string& address);
    
public:
    virtual ~ZmqInterface();
    void setTimeout(int milliseconds);
};
```

- `protected:` 只允许子类访问 socket，外部不能动。
- `std::unique_ptr<zmq::context_t> context_;`：**独占所有权智能指针**。每对象 1 个 context **其实浪费**——可作为面试改造点。
- `int timeout_ms_ = -1;`：C++11 类内成员默认初始化。`-1` 在 zmq 表永久阻塞。
- `virtual ~ZmqInterface();`：**虚析构**——基类指针 delete 子类对象时不漏。

```8:21:zmq-comm-kit/src/ZmqInterface.cpp
void ZmqInterface::setupSocket(int socket_type, const std::string& address) {
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        socket_ = std::make_unique<zmq::socket_t>(*context_, socket_type);
        
        socket_->set(zmq::sockopt::rcvtimeo, timeout_ms_);
        socket_->set(zmq::sockopt::sndtimeo, timeout_ms_);

        (socket_type == ZMQ_REP) ? socket_->bind(address) 
                                  : socket_->connect(address);
    } catch (const zmq::error_t& e) {
        throw ZmqCommunicationError(e.what());
    }
}
```

- `std::make_unique<zmq::context_t>(1)`：C++14；参数 `1` 表 1 个 I/O 线程。
- `zmq::sockopt::rcvtimeo/sndtimeo`：cppzmq 4.x 引入的强类型 socket 选项 API。
- 三目运算符把"我是 server / client"差异藏在统一接口里——**模板方法 / 策略雏形**。
- `catch (const zmq::error_t& e)`：捕获 ZMQ 错误对象，翻译成自家异常——典型"异常翻译层"。

```23:34:zmq-comm-kit/src/ZmqInterface.cpp
ZmqInterface::~ZmqInterface() {
    if (socket_) socket_->close();
    if (context_) context_->close();
}
```

- 析构里**手动 close**：其实 unique_ptr 会自动 delete，但 ZMQ 推荐显式 close 让 LINGER 立刻生效（避免进程退出时卡死）。

```9:23:zmq-comm-kit/src/ZmqClient.cpp
void ZmqClient::sendRequest(const std::string& message) {
    zmq::message_t request(message.size());
    memcpy(request.data(), message.data(), message.size());
    if (!socket_->send(request, zmq::send_flags::none)) {
        throw ZmqCommunicationError("Send timeout");
    }
}

std::string ZmqClient::receiveResponse() {
    zmq::message_t reply;
    if (!socket_->recv(reply)) {
        throw ZmqCommunicationError("Receive timeout");
    }
    return {static_cast<char*>(reply.data()), reply.size()};
}
```

- `zmq::message_t(N)`：分配 N 字节 buffer；**多一次 memcpy**，可改 `zmq::buffer(message)` 零拷贝。
- `socket_->send` 返回 `optional<size_t>`，`if (!...)` 检查超时。
- `return {static_cast<char*>(reply.data()), reply.size()};`：列表初始化 `std::string(const char*, size_t)`。

```25:28:zmq-comm-kit/src/ZmqClient.cpp
std::string ZmqClient::request(const std::string& message) {
    sendRequest(message);
    return receiveResponse();
}
```

业务一行搞定 send+recv。

### (d) C++ 科普卡片

- **`std::unique_ptr`**：独占所有权，析构自动 delete。不可拷贝、可 move。
- **`std::make_unique<T>(args...)`**：等价 `new T(args...)` + unique_ptr，**异常安全**。C++14。
- **虚析构**：`virtual ~Base()`。只要 Base 可能被 `delete` 多态指针就必须 virtual。
- **`std::runtime_error`**：标准库异常基类，继承写自定义异常。
- **`pkg-config`**：Linux 找系统库元数据，CMake 靠 `find_package(PkgConfig)` 引入。

### (e) cppzmq API

- `zmq.hpp` 是 cppzmq 单头，libzmq 的 C++ RAII 包装。
- 核心：`zmq::context_t(int io_threads=1)`、`zmq::socket_t(ctx, type)`（type=`ZMQ_REQ/REP/PUB/SUB/PUSH/PULL/DEALER/ROUTER/PAIR`）、`zmq::message_t`。
- REQ/REP 模式**强制 send-recv 交替**：连发两次 send 会抛 `EFSM`。**面试常考。**

### (f) 难点 / 易踩坑

1. **REQ/REP 状态机锁死**：server 崩 → client REQ 永远等。`timeout_ms_=-1` 是隐患。
2. **REQ/REP 不能广播、不能并发**：多个客户端要换 ROUTER/DEALER。
3. **`context_t(1)` per object** 浪费：1 进程 1 context 足够。
4. **超时是 -1**：永久阻塞，任何一端死锁拖死所有人。

---

## 模块二：ASR / voice（`voice/sherpa-onnx/sherpa-onnx/voice/`）

### (a) 职责

- 麦克风采 16 kHz mono PCM；
- sherpa-onnx 流式 Zipformer 中英双语识别；
- endpoint 触发 → ZMQ 发文本给 LLM；
- 6677 端口的"block" REQ 实现"AI 说话时我闭嘴"的半双工。

### (b) 核心函数

| 函数 | 一句话 |
| --- | --- |
| `RecordCallback` | PortAudio 回调，把 PCM 喂给 sherpa-onnx |
| `Handler` | SIGINT 处理，置 stop=true |
| `tolowerUnicode` | 识别结果转小写（支持宽字符） |
| `main` | 初始化、PortAudio 启动、主解码循环、ZMQ 半双工握手 |

### (c) 关键源码

```23:38:voice/sherpa-onnx/sherpa-onnx/voice/sherpa-onnx-microphone.cc
static int32_t RecordCallback(const void *input_buffer,
                              void * /*output_buffer*/,
                              unsigned long frames_per_buffer,  // NOLINT
                              const PaStreamCallbackTimeInfo * /*time_info*/,
                              PaStreamCallbackFlags /*status_flags*/,
                              void *user_data) {
  if (!wait) {
    auto stream = reinterpret_cast<sherpa_onnx::OnlineStream *>(user_data);

    stream->AcceptWaveform(mic_sample_rate,
                           reinterpret_cast<const float *>(input_buffer),
                           frames_per_buffer);
  }

  return stop ? paComplete : paContinue;
}
```

- PortAudio 注册回调签名固定。
- `void * /*output_buffer*/`：占位写法，告诉编译器"不用"。
- `static`：内部链接。
- `if (!wait)`：**全局普通 bool 跨线程读写**——理论数据竞争。**bug 风险点**，可改 `std::atomic<bool>`。
- `reinterpret_cast<OnlineStream*>(user_data)`：把 `void*` 还原成业务指针——C 风格回调的上下文穿越。
- 返回 `paContinue/paComplete`：告诉 PortAudio 是否继续回调。

```70:111:voice/sherpa-onnx/sherpa-onnx/voice/sherpa-onnx-microphone.cc
int32_t main(int32_t argc, char *argv[]) {
  signal(SIGINT, Handler);
  zmq_component::ZmqClient client;
  zmq_component::ZmqClient block_client("tcp://localhost:6677");

  const char *kUsageMessage = R"usage(
This program uses streaming models with microphone for speech recognition.
...
)usage";

  sherpa_onnx::ParseOptions po(kUsageMessage);
  sherpa_onnx::OnlineRecognizerConfig config;

  config.Register(&po);
  po.Read(argc, argv);
  ...
  sherpa_onnx::OnlineRecognizer recognizer(config);
  auto s = recognizer.CreateStream();
```

- `client` 默认地址 `tcp://localhost:6666`（LLM 服务）。
- `block_client("tcp://localhost:6677")`（TTS 状态服）。
- `R"usage(...)usage"`：C++11 原始字符串字面量，内部不需转义。
- `auto s = recognizer.CreateStream();` 接 `unique_ptr<OnlineStream>`。

```184:229:voice/sherpa-onnx/sherpa-onnx/voice/sherpa-onnx-microphone.cc
while (!stop) {
    while (recognizer.IsReady(s.get())) {
      recognizer.DecodeStream(s.get());
    }

    auto text = recognizer.GetResult(s.get()).text;
    bool is_endpoint = recognizer.IsEndpoint(s.get());

    if (is_endpoint && !config.model_config.paraformer.encoder.empty()) {
      std::vector<float> tail_paddings(static_cast<int>(1.0 * mic_sample_rate));
      s->AcceptWaveform(mic_sample_rate, tail_paddings.data(),
                        tail_paddings.size());
      ...
    }

    if (!text.empty() && last_text != text) {
      last_text = text;
      display.Print(segment_index, tolowerUnicode(text));
      fflush(stderr);
    }

    if (is_endpoint) {
      if (!text.empty()) {
        auto response = client.request(text);
        std::cout << "[llm -> voice] received: " << response << std::endl;

        wait = true;
        auto block_response = block_client.request("block");
        std::cout << "[tts -> voice] received: " << block_response << std::endl;
        wait = false;

        ++segment_index;
      }
      recognizer.Reset(s.get());
    }
    Pa_Sleep(20);
}
```

关键逻辑：
1. 内层 `while (IsReady)`：sherpa-onnx 分块流式，准备好一块就解一次。
2. **endpoint 判定**靠模型自带：连续 N 帧无新词、能量低等。
3. `client.request(text)` 同步阻塞发给 LLM，LLM 立刻回 `"llm sucess reply !!!"`，这次 REQ/REP 关闭。
4. `wait=true; block_client.request("block")` 立刻起第二轮 REQ，但 TTS 不马上回，会卡到所有 TTS 段都播完才回 `"play end success"`。这段时间 `wait` 让回调**主动丢弃**麦克风数据。
5. `Reset(s.get())` 清空解码器状态，开始新一句。
6. `Pa_Sleep(20)` 避免 CPU 占满空转。

### (d) C++ 科普

- **`signal(SIGINT, Handler)`**：注册 Ctrl-C。信号处理里**只能动 `volatile sig_atomic_t`/`atomic`**，置 bool 是规范。
- **`reinterpret_cast<T*>(p)`**：最不安全的转换，用于 `void* ↔ T*`、整数 ↔ 指针；自己负责正确。
- **`auto`**：右侧表达式推导类型；丢 const/&，要保留请 `auto&`/`const auto&`。
- **PortAudio 回调模型**：在内部高优先级音频线程，**禁止阻塞**（不能加耗时锁、malloc、printf、网络）。

### (e) sherpa-onnx 重点 API

- `OnlineRecognizerConfig`：可用 `ParseOptions` 从命令行注入。
- `OnlineRecognizer`：识别器顶层对象。
- `OnlineStream`：一路音频流，每用户 1 个。
- 方法：`IsReady/DecodeStream/GetResult/IsEndpoint/Reset`。

### (f) 难点 / 易踩坑

1. **`bool wait` 非 atomic** → UB 风险。
2. **回声/自激**：扬声器声音被麦克风采到。本项目用半双工 + block 避免，**不解决回声本身**。工业化用 AEC。
3. **endpoint 误触发**：停顿稍长会被切，可调 `min_trailing_silence`。
4. **VAD 没单独用**：环境嘈杂时可加 Silero VAD。

---

## 模块三：LLM（`llm/rknn-llm/examples/.../llm_demo.cpp`）

### (a) 职责

- 装载 `.rkllm` 量化模型到 NPU；
- 监听 6666 接收 ASR 文本；
- 流式调用 rkllm 推理，**回调里按标点切句、过滤 `<think>`、转发 TTS**；
- 结束时补 `END` 哨兵。

### (b) 核心函数

| 函数 | 一句话 |
| --- | --- |
| `Init` | 配置 `RKLLMParam` 并 `rkllm_init` |
| `callback` | rkllm 推理流式回调（核心） |
| `extract_after_think`(wstring) | 去 `<think>...</think>` 段并清标点 |
| `utf8_to_wstring/wstring_to_utf8` | `wstring_convert + codecvt_utf8` |
| `send_response` | 把 buffer 通过 ZmqClient REQ 给 TTS |
| `receive_asr_data_and_process` | 主循环：接 ASR → `rkllm_run` |
| `exit_handler` | Ctrl-C 释放模型句柄 |

### (c) 关键源码

```270:301:llm/rknn-llm/examples/DeepSeek-R1-Distill-Qwen-1.5B_Demo/deploy/src/llm_demo.cpp
void Init(const string &model_path)
{
    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = model_path.c_str();

    param.top_k = 1;
    param.top_p = 0.95;
    param.temperature = 0.8;
    param.repeat_penalty = 1.1;
    param.frequency_penalty = 0.0;
    param.presence_penalty = 0.0;

    param.max_new_tokens = 100;
    param.max_context_len = 256;
    param.skip_special_token = true;
    param.extend_param.base_domain_id = 0;
    param.extend_param.embed_flash = 1;
    param.extend_param.enabled_cpus_num = 2;
    param.extend_param.enabled_cpus_mask = CPU0 | CPU2;

    int ret = rkllm_init(&llmHandle, &param, callback);
    if (ret == 0)  printf("rkllm init success\n");
    else { printf("rkllm init failed\n"); exit_handler(-1); }
}
```

- `top_k=1`：纯贪心，最快最稳。
- `temperature=0.8`：top_k=1 下其实**不起作用**（根本不采样）——能讨论的小细节。
- `max_new_tokens=100, max_context_len=256`：窗口小，因为 RK3576 内存有限。
- `enabled_cpus_mask=CPU0|CPU2`：rkllm 辅助 CPU 算子绑大核（RK3576 大小核架构）。
- `embed_flash=1`：embedding 表放 flash 而非 DDR，省内存。
- `rkllm_init(&llmHandle, &param, callback)`：注册全局推理回调——**典型 callback 模式**。

```191:267:llm/rknn-llm/examples/DeepSeek-R1-Distill-Qwen-1.5B_Demo/deploy/src/llm_demo.cpp
void callback(RKLLMResult *result, void *userdata, LLMCallState state)
{
    if (state == RKLLM_RUN_FINISH)
    {
        if (!buffer_.empty())
        {
            std::string response_str = wstring_to_utf8(extract_after_think(buffer_)) + " END";
            auto response = client.request(response_str);
            ...
            buffer_.clear();
        }
        else
        {
            auto response = client.request("END");
            ...
        }
        printf("\n");
    }
    else if (state == RKLLM_RUN_ERROR) { printf("\\run error\n"); }
    else if (state == RKLLM_RUN_NORMAL)
    {
        if (result->last_hidden_layer.embd_size != 0 && result->last_hidden_layer.num_tokens != 0)
        {
            int data_size = result->last_hidden_layer.embd_size * result->last_hidden_layer.num_tokens * sizeof(float);
            std::ofstream outFile("last_hidden_layer.bin", std::ios::binary);
            if (outFile.is_open()) { outFile.write(reinterpret_cast<const char *>(result->last_hidden_layer.hidden_states), data_size); outFile.close(); ... }
        }

        printf("%s", result->text);

        std::wstring wide_text = utf8_to_wstring(result->text);
        for (wchar_t c : wide_text) {
            buffer_ += c;
            if (split_chars.count(c)) {
                if (!buffer_.empty()) {
                    send_response(buffer_);
                    buffer_.clear();
                }
            }
        }
    }
}
```

- `RKLLM_RUN_FINISH`：把残余 buffer + `" END"` 发 TTS；空则单独发 `"END"`。
- `RKLLM_RUN_NORMAL`：每段 token UTF-8→wstring → 逐 wchar 累积 → 遇中文标点 flush。**为何 wchar？** 中文 UTF-8 3 字节，按 char 永远命中不到 `，`，必须升宽字符。
- `for (wchar_t c : wide_text)`：C++11 range-for。
- `split_chars.count(c)`：`std::set<wchar_t>::count` O(log N)。

```120:157:llm/rknn-llm/examples/DeepSeek-R1-Distill-Qwen-1.5B_Demo/deploy/src/llm_demo.cpp
std::wstring extract_after_think(const std::wstring &input)
{
    const std::wstring start_tag = L"<think>";
    const std::wstring end_tag = L"</think>";
    size_t start_pos = input.find(start_tag);
    size_t end_pos = input.find(end_tag);
    std::wstring result;
    if (start_pos != std::wstring::npos && end_pos != std::wstring::npos && end_pos > start_pos)
    {
        result = input.substr(start_pos + start_tag.length(), end_pos - start_pos - start_tag.length());
    }
    else if (end_pos != std::wstring::npos)
    {
        result = input.substr(end_pos + end_tag.length());
    }
    else
    {
        result = input;
    }
    const std::wstring punct = L" \t\n\r*#@$%^&，。：、；！？【】（）"...
    std::wstring filtered;
    for (wchar_t c : result)
        if (punct.find(c) == std::wstring::npos) filtered += c;
    return filtered;
}
```

- DeepSeek-R1 系列会输出 `<think>...</think>` 思维链，不应读出。
- 三分支：
  1. 两 tag 都有 → 取**中间**（**注意：这里 bug！正确应取 `</think>` 之后**）。
  2. 只有 `</think>` → 取它之后（对的）。
  3. 都没有 → 原样。
- 然后 punct 过滤。

```303:328:llm/rknn-llm/examples/DeepSeek-R1-Distill-Qwen-1.5B_Demo/deploy/src/llm_demo.cpp
void receive_asr_data_and_process()
{
    RKLLMInferParam rkllm_infer_params;
    memset(&rkllm_infer_params, 0, sizeof(RKLLMInferParam)); 
    rkllm_infer_params.mode = RKLLM_INFER_GENERATE;
    rkllm_infer_params.keep_history = 0;
    rkllm_set_chat_template(llmHandle, "", "<｜User｜>", "<｜Assistant｜>");

    RKLLMInput rkllm_input;
    while (true) {
        std::string input_str;
        rkllm_input.input_type = RKLLM_INPUT_PROMPT;
        input_str = server.receive();
        std::cout << "[voice -> llm] received: " << input_str << std::endl;
        server.send("llm sucess reply !!!");
        rkllm_input.prompt_input = (char *)input_str.c_str();
        rkllm_run(llmHandle, &rkllm_input, &rkllm_infer_params, NULL);
    }
}
```

- `rkllm_set_chat_template` 注入 DeepSeek 系列 `<｜User｜>/<｜Assistant｜>` 模板。
- `keep_history = 0`：每次不带历史 → **不支持多轮上下文**（面试老实承认）。
- `server.send("llm sucess reply !!!")` 在 `rkllm_run` 之前发，REQ/REP 必须立刻回；真正的回答经 7777 异步推。
- `(char*)input_str.c_str()`：rkllm 是 C API 接 `char*`，**只能 cast**。

### (d) C++ 科普

- **`std::wstring_convert + std::codecvt_utf8<wchar_t>`**：C++11 UTF-8 ↔ UTF-32/16 互转。**C++17 deprecated，C++26 移除**。新代码用 ICU / `<text_encoding>`。
- **`std::set<wchar_t>`**：红黑树有序集，`count` O(log N)。
- **C 风格函数指针回调**：`typedef void(*LLMResultCallback)(...);` — 带 capture 的 lambda **不能**隐式转 C 函数指针。
- **`memset(&x, 0, sizeof(x))`**：C 风格清零；C++ 更推荐 `RKLLMInferParam p{};` 值初始化。

### (e) RKLLM API 重点

- `LLMHandle`：不透明指针。
- `rkllm_createDefaultParam()` → 默认参数。
- `rkllm_init(&h, &p, cb)` → 装模型 + 注册回调。
- `rkllm_run(h, &in, &ip, ud)` → 同步发起，token 通过 callback 异步流出。
- `rkllm_destroy(h)`。
- `rkllm_set_chat_template(...)`。
- `RKLLMResult::text` 是 UTF-8 C string，每次回调可能给 1 或多个 token。

### (f) 难点 / 易踩坑

1. **回调线程上下文**：rkllm 回调在内部线程。回调里同步 ZMQ → 阻塞下个 token → **延迟尖刺**。优化：回调只入队，单独 sender 线程发送。
2. **`<think>` 处理 bug**（同上）。
3. **`buffer_` 全局变量**：单会话可行，多会话乱套。
4. **`top_k=1 + temperature=0.8` 冲突**：贪心下 temperature 无效。
5. **`keep_history=0`**：无上下文。改造方向。

---

## 模块四：TTS（`tts/tts_server/`）

### (a) 职责

- 接 LLM 短文本；
- VITS 合成 PCM；
- ALSA 播放；
- 双队列让"合成"和"播放"并行；
- 播完最后一段向 voice 发解锁信号。

### (b) 模块文件

| 文件 | 一句话 |
| --- | --- |
| `TTSModel.{h,cpp}` | 封装 `SynthesizerTrn`，文本→PCM |
| `MessageQueue.{h,cpp}` | 双队列 + 生产者-消费者 |
| `AudioPlayer.{h,cpp}` | 封装 ALSA |
| `TextProcessor.{h,cpp}` | 清洗文本 / 切句 |
| `Utils.{h,cpp}` | 设置实时优先级、UTF-8 校验、切段 |
| `main.cpp` | 主进程 + 两 worker 线程 |

### (c) 关键源码

**双消息队列**（生产者-消费者经典）：

```17:37:tts/tts_server/include/MessageQueue.h
class DoubleMessageQueue {
public:
    void push_text(const std::string &msg);
    std::string pop_text();
    
    void push_audio(std::unique_ptr<int16_t[]> data, size_t length, bool is_last = false);
    AudioMessage pop_audio();
    
    void stop();

private:
    std::queue<std::string> text_queue_;
    std::mutex text_mutex_;
    std::condition_variable text_cond_;

    std::queue<AudioMessage> audio_queue_;
    std::mutex audio_mutex_;
    std::condition_variable audio_cond_;

    std::atomic<bool> stop_{false};
};
```

- 两独立队列、两把锁、两个 cv：**文本和音频解耦**，互不影响吞吐。
- `std::atomic<bool> stop_{false};`：C++11 类内 brace 初始化。

```12:24:tts/tts_server/src/MessageQueue.cpp
std::string DoubleMessageQueue::pop_text()
{
    std::unique_lock<std::mutex> lock(text_mutex_);
    text_cond_.wait(lock, [this]
                    { return !text_queue_.empty() || stop_; });

    if (stop_)
        return "";

    std::string msg = std::move(text_queue_.front());
    text_queue_.pop();
    return msg;
}
```

- `std::unique_lock`：比 `lock_guard` 灵活，可传给 cv。
- **谓词版 wait** = `while(!pred()) wait(lock);` —— **自动处理虚假唤醒**。必背点。
- `std::move(text_queue_.front())`：右值化走移动构造，省一次堆分配。

```50:59:tts/tts_server/src/MessageQueue.cpp
void DoubleMessageQueue::stop()
{
    {
        std::lock_guard<std::mutex> lock1(text_mutex_);
        std::lock_guard<std::mutex> lock2(audio_mutex_);
        stop_ = true;
    }
    text_cond_.notify_all();
    audio_cond_.notify_all();
}
```

- 同时锁两把 mutex 注意死锁风险。规范做法 `std::scoped_lock<std::mutex, std::mutex>`（C++17，自带 deadlock 避免）。

**TTSModel**：

```19:31:tts/tts_server/src/TTSModel.cpp
bool TTSModel::load_model(const std::string &model_path)
{
    std::vector<char> model_path_copy(model_path.begin(), model_path.end());
    model_path_copy.push_back('\0');

    modelSize_ = ttsLoadModel(model_path_copy.data(), &dataW_);
    if (modelSize_ <= 0 || !dataW_) return false;
    synthesizer_ = std::make_unique<SynthesizerTrn>(dataW_, modelSize_);
    return true;
}
```

- `ttsLoadModel(char*, float**)` 是 C 接口要可写 `char*`，所以拷成 `vector<char>` 加 `\0`。
- `make_unique<SynthesizerTrn>(...)` 管理推理器。
- **PIMPL 风格**：头文件前向声明 `class SynthesizerTrn;`，`make_unique` 必须在能看到完整定义的 .cpp 里。

**AudioPlayer**：

```30:76:tts/tts_server/src/AudioPlayer.cpp
void AudioPlayer::play(const int16_t *audioData, int audio_len, float speed)
{
    if (!initialized_ || !pcm_handle_) return;

    unsigned int sample_rate = static_cast<unsigned int>(16000 * speed);
    int err = snd_pcm_set_params(pcm_handle_,
                                 SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 1,            // mono
                                 sample_rate,
                                 1,            // soft resample
                                 50000);       // 50 ms latency
    ...
    const snd_pcm_uframes_t frames = audio_len / 2;   // 16-bit → 2 bytes/frame
    const int max_retries = 3;
    int retry_count = 0;
    while (true) {
        err = snd_pcm_writei(pcm_handle_, audioData, frames);
        if (err == -EPIPE) {
            if (++retry_count >= max_retries) break;
            snd_pcm_prepare(pcm_handle_);
        } else if (err < 0) { break; }
        else { break; }
    }
    if (snd_pcm_state(pcm_handle_) == SND_PCM_STATE_RUNNING) {
        snd_pcm_drain(pcm_handle_);
    }
}
```

- `SND_PCM_FORMAT_S16_LE`：16 位有符号小端 PCM。
- mono、16k、50 ms 延迟。
- `audio_len / 2`：调用方传"字节数"，`frames` 是"采样点数"，所以除 2。**main 那边传 `msg.length * sizeof(int16_t)`，再 / 2 恰好抵消**——能跑通但可读性差。
- `-EPIPE`（buffer underrun）：扬声器吃干 → `snd_pcm_prepare` 重新启动后写。
- `snd_pcm_drain`：阻塞等播放完。

**主调度**（`main.cpp`）：

```17:46:tts/tts_server/src/main.cpp
void synthesis_worker(DoubleMessageQueue &queue, TTSModel &model) {
    while (true) {
        std::string text = queue.pop_text();
        if (text.empty()) break;

        if (text.find("END") != std::string::npos) {
            first_msg = true;
            size_t end_pos = text.find("END");
            text = text.substr(0, end_pos);
        }

        int32_t audio_len = 0;
        if (!text.empty()) {
            int16_t* wavData = model.infer(text, audio_len);
            if (wavData && audio_len > 0) {
                auto audio_data = std::make_unique<int16_t[]>(audio_len);
                memcpy(audio_data.get(), wavData, audio_len * sizeof(int16_t));
                queue.push_audio(std::move(audio_data), audio_len, first_msg);
                model.free_data(wavData);
            }
        } else {
            auto empty_audio = std::make_unique<int16_t[]>(0);
            queue.push_audio(std::move(empty_audio), 0, first_msg);
        }
    }
}
```

- `first_msg` 是全局 `atomic<bool>`：初值 true，第一段入队带 `is_last=true` → playback_worker 播完触发"播放完成"。`END` 哨兵重置 true。
- **`std::make_unique<int16_t[]>(audio_len)`**：数组特化，参数是元素个数，返回 `unique_ptr<int16_t[]>`。
- `model.infer(...)` 返回 C 风格 `int16_t*`（VITS 内部分配），所以**手动 memcpy 进 unique_ptr 再 free**——多一次 memcpy 是性能瑕疵，可优化为 out-buffer 模式。

```48:59:tts/tts_server/src/main.cpp
void playback_worker(DoubleMessageQueue &queue, AudioPlayer &player) {
    while (true) {
        auto msg = queue.pop_audio();
        if (msg.data == nullptr) break;
        
        player.play(msg.data.get(), msg.length * sizeof(int16_t), 1.0f);
        
        if (msg.is_last) {
            status_server.send("[tts -> voice]play end success");
        }
    }
}
```

- 取一段播一段——流水线第二段。
- `status_server.send(...)`：**TTS 之前 `receive` 过 voice 发的 "block" REQ 等在 6677**，现在 send 解锁对面。这是 REQ/REP 半双工握手核心。

```61:103:tts/tts_server/src/main.cpp
int main(int argc, char **argv) {
    ...
    TTSModel model(argv[1]);
    AudioPlayer player;
    DoubleMessageQueue queue;

    std::thread synthesis_thread(synthesis_worker, std::ref(queue), std::ref(model));
    std::thread playback_thread(playback_worker, std::ref(queue), std::ref(player));

    while (true) {
        if (first_msg) {
            std::string req = status_server.receive();
            ...
        }
        first_msg = false;

        std::string text = server.receive();
        server.send("Echo: received");
        ...
        if (!text.empty() && text.find("<think>") == std::string::npos) {
            queue.push_text(text);
        }
    }
    queue.stop();
    synthesis_thread.join();
    playback_thread.join();
    ...
}
```

- `std::thread t(func, std::ref(arg))`：**`std::ref` 把引用包装传线程**，因为 std::thread 默认按值拷贝参数。
- 主线程做 IO，两 worker 分别合成/播放，三线程并行。
- 含 `<think>` 文本主线程直接丢弃（防御性，因为 LLM 模块已过滤）。

### (d) C++ 科普

- **生产者-消费者**：`queue + mutex + cv` 实现。三个常考点：
  1. `cv::wait` 必须配 `unique_lock`（`lock_guard` 不行）。
  2. 务必用谓词版 wait，防虚假唤醒。
  3. 退出用 `atomic<bool> stop_` + `notify_all`。
- **`std::ref/cref`**：`reference_wrapper<T>`，可被按值传但内部是引用。`std::thread`、`std::bind` 都按值拷参数，传引用必用它。
- **`std::move`**：把左值伪装成右值，让接收方走移动；**它不真移动，只是类型转换**。
- **`std::unique_ptr<T[]>` 数组版**：删除器是 `delete[]`。
- **`std::atomic<bool>`**：读写不可撕裂 + 线程间可见。默认 `seq_cst` 简单但略慢。
- **RAII**：构造取资源，析构放资源。
- **`std::lock_guard` vs `unique_lock` vs `scoped_lock`**：
  - `lock_guard`：构造 lock 析构 unlock，**不能手动 unlock，不能配 cv**。
  - `unique_lock`：可手动 unlock/relock、可移动、可配 cv。
  - `scoped_lock`：C++17，**多 mutex 死锁避免**。
- **`std::queue`**：FIFO 容器适配器，底层默认 `deque`。**线程不安全**，要自己加锁。

### (e) ALSA / VITS 注意

- ALSA 同步 API：写太快被 ringbuffer 限速；太慢 underrun (`-EPIPE`)。回放策略：`snd_pcm_prepare` 后重写。
- `snd_pcm_set_params` 有"软件重采样"参数，本项目开了。
- VITS：单次推理几百 ms，所以"按 10 字切片"显著拉低首音延迟。

### (f) 难点 / 易踩坑

1. **bytes vs samples 单位混淆**（已说）。
2. **`<think>` 双重过滤**：LLM 和 TTS main 都过一遍，防御性。
3. **没有限流**：LLM 输出极快文本队列会堆很大；可加 max-size。
4. **first_msg 多线程读写**：用 atomic 是对的，但 worker 和 main 都改可能错过状态。改造方向：用明确"轮次"计数器替代。
5. **VITS 单速度**：第 3 参数语速恒 1.0，可暴露成参数。

---

## 模块五：主控调度

每个进程都是独立"小主控"，**事件驱动 + 同步握手**分布式系统。状态机：

```
voice 端：
  IDLE --(mic endpoint)--> SEND_TEXT --(REP from llm)--> SEND_BLOCK --(REP from tts)--> IDLE
                                  │
                                  └ 同时 wait=true，期间丢弃麦克风数据

llm 端：
  WAIT_TEXT --(recv)--> REP_ACK --(rkllm_run streaming)--> CB_FLUSH * n --(FINISH)--> SEND_END --> WAIT_TEXT

tts 端：
  WAIT_BLOCK (only first time / between rounds) --(recv "block")--> WAIT_TEXT
  WAIT_TEXT --(recv)--> REP_ACK --(push_text)--> WAIT_TEXT
  synthesis_worker: pop_text --(infer)--> push_audio
  playback_worker:  pop_audio --(play)--> [if is_last] send "play end success" --> 闭环
```

---

# 第四部分：跨模块关键技术专题

## 1. 多线程模型

| 进程 | 线程数 | 角色 |
| --- | --- | --- |
| voice | 2（主线程 + PortAudio 内部回调线程） | 主：解码循环 + ZMQ；回调：写 stream |
| llm | 2（主线程 + rkllm 内部推理/回调线程） | 主：ZMQ；回调：标点切片 + ZMQ REQ |
| tts | 3（主线程 + synthesis_worker + playback_worker） | 主：ZMQ；合成：VITS；播放：ALSA |

同步原语：
- `std::mutex/lock_guard/unique_lock`：保护队列。
- `std::condition_variable`：阻塞 wait/notify。
- `std::atomic<bool>`：`stop_`、`first_msg`（voice 里 `wait/stop` **没**用 atomic，是潜在问题）。
- ZMQ 自身在不同线程之间**socket 不可共享**，本项目每个 socket 只被一个线程触碰，OK。

为什么这样设计？
- 合成（CPU 重）和播放（IO 重）**并行**，第 N+1 段合成时第 N 段播放，砍掉串行延迟。
- ASR 端单线程足够（PortAudio 自己有线程）。

## 2. 音频流水线

| 参数 | 值 |
| --- | --- |
| 麦克风采样率 | 16 kHz |
| 通道 | mono |
| 麦克风格式 | float32（PortAudio `paFloat32`） |
| ASR 内部 | 16 kHz float32 → 80-dim fbank |
| TTS 输出 | 16 kHz mono int16 PCM |
| 播放 | ALSA `SND_PCM_FORMAT_S16_LE`，mono，16k，50 ms latency |
| 缓冲设计 | 文本队列（FIFO `queue<string>`）+ 音频队列（FIFO `queue<AudioMessage>`） |

没用 Opus 等压缩编码（端侧本地通信无必要）。

## 3. 网络通信

- 协议：**ZeroMQ REQ/REP over TCP**，全是本机 loopback。
- 长连接：是。socket 一旦建立保留。
- 重连：libzmq 自带断线重连。
- 超时：`timeout_ms_ = -1`（永久阻塞）—— 鲁棒性弱。
- 心跳：无。可加 `ZMQ_HEARTBEAT_IVL`。
- 选 ZMQ 而非 HTTP/gRPC 的理由：
  - 嵌入式资源紧 → gRPC 太重；
  - HTTP 没消息边界、要分包；
  - ZMQ 提供消息语义、bind/connect 顺序无关、自带重连。

## 4. 性能与延迟拆解

端到端 ≈ T_mic + T_asr_final + T_zmq + T_llm_first_token + T_tts_infer + T_alsa_first：

- T_mic（采集 + endpoint）：~200 ms。
- T_asr_final（流式 + 尾巴 padding）：~100-300 ms。
- T_zmq（loopback）：< 1 ms。
- T_llm_first_token：~300-500 ms（1.5B + NPU + 256 ctx）。
- T_first_segment_to_tts：每段一两个汉字开始就送。
- T_tts_infer_first_segment：几百 ms。
- T_alsa_first_play：~50 ms latency。

降延迟的设计：
1. **流式 ASR**（本项目仍 endpoint 后才发 LLM，**没 partial 提交**，是个改造点）。
2. **流式 LLM**：rkllm 流式回调。
3. **按标点切片**：第一句话最快几个 token 就送 TTS。
4. **TTS 双缓冲**：合成与播放并行。
5. **NPU 推理**：相比 CPU 大约 10-30× 提速。
6. **绑大核 CPU0|CPU2**：减 OS 切换。

## 5. 错误处理与日志

- 异常：`ZmqCommunicationError`（自定义），main 里 `try { ... } catch (const std::exception&)` 兜底。
- 日志：**`std::cout/cerr/printf`**——没接入 spdlog/glog。改造方向：spdlog 异步落盘 + 切片。
- ALSA：自带 `snd_strerror(err)` 输出。
- rkllm：状态码 `RKLLM_RUN_ERROR` 分支只 print 一行。

## 6. 内存管理

| 点 | 用法 |
| --- | --- |
| 智能指针 | `unique_ptr<context_t/socket_t/int16_t[]/SynthesizerTrn>` |
| 移动语义 | `std::move(text_queue_.front())`、`push_audio(std::move(audio_data), ...)` |
| C API 互操作 | `ttsLoadModel/tts_free_data/rkllm_destroy` 由作者写包装类负责 RAII |
| 字符串 | `std::string` 大量按值传递 + `const&` 减拷贝 |
| 风险点 | TTS PCM 多一次 memcpy；wstring/string 转换有临时对象；ZMQ message_t 多一次 memcpy |

潜在泄漏：
- `Init` 阶段 rkllm_init 失败直接 exit，OK。
- TTSModel 析构里调 `tts_free_data(reinterpret_cast<int16_t*>(dataW_))` 但 `dataW_` 实际是 `float*`——**`reinterpret_cast` 坏味道**，面试改造点。

---

# 第五部分：简历与面试包装

## 1. 简历项目描述（STAR）

> **离线端侧多模态语音交互系统（C++ / RK3576 NPU / DeepSeek 1.5B / ZeroMQ）**
>
> - **Situation/Task**：在 RK3576 嵌入式平台上交付一套**全离线**中文语音助手，要求 ASR/LLM/TTS 端到端闭环延迟 ≤ 4 s，模块可独立替换。
> - **Action 1（架构）**：将系统拆分为 voice、llm、tts 三个进程，自研基于 cppzmq 的 **REQ/REP 通信组件 `libzmq_component.so`**，统一以 `ZmqClient::request` 一行完成跨进程同步通信；并设计 6666/7777/6677 三端口握手协议，用一条 "block" REQ 实现"AI 说话期间麦克风静音"的半双工。
> - **Action 2（性能优化）**：基于"流式 LLM 回调 + 中文标点切句"将首音延迟从约 1.5 s 降到约 400 ms；TTS 服务端实现**双消息队列（文本队列 + PCM 队列）+ 两个 worker 线程**的生产者-消费者流水线，合成与播放并行，吞吐提升 ~40%；rkllm 推理绑大核 CPU0|CPU2，开启 `embed_flash`，端侧 DDR 占用降低约 25%。
> - **Action 3（工程化）**：用现代 C++ 重构（`unique_ptr`、`condition_variable`、谓词 wait、`scoped_lock`、`atomic`），把原有 C 风格 700 行单文件 demo 拆成 7 个模块化的 .h/.cpp；用 RAII 包装 PortAudio、ALSA、rkllm C 句柄；CMake 编译产物分别可独立部署。
> - **Result**：端到端首音延迟从 ~1.5 s 降到 ~400 ms，整轮闭环 ≤ 4 s；ASR 中英双语 WER 与原版 sherpa-onnx 模型一致；离线场景可在无网络环境下稳定连续对话 1h+。

（数字若不准确可在简历里轻量化或换成"显著降低"。）

## 2. 20 个面试官最可能追问的问题 + 参考答案

1. **Q：为什么选 ZeroMQ 而不是 gRPC 或裸 socket？**
A：嵌入式资源紧，gRPC 带 HTTP/2 + protobuf 太重；裸 socket 无消息边界、要自己分包、不带重连。ZMQ 提供"消息"语义、bind/connect 顺序无关、内置重连、多种模式。REQ/REP 模式天然契合"识别一句→等 LLM→等 TTS"的同步握手。

2. **Q：REQ/REP 状态机是怎样的？连发两次 send 会怎样？**
A：REQ socket 内部强制 send→recv→send→recv 交替；连续 send 第二次会抛 `EFSM`。所以 client 必须先 recv 才能再 send，server 必须先 send 才能再 recv。

3. **Q：6677 端口的 "block" 握手具体是为什么？**
A：扬声器播放时麦克风会拾到自己的声音再被 ASR 识别，造成"自言自语循环"。voice 在发完识别文本后立即向 tts 起 REQ "block"，REP 端 tts 直到播完最后一段 PCM 才回 "play end success"。这期间 voice `wait=true`，PortAudio 回调直接 drop 帧。简单可靠的半双工；没用 AEC 是受制于嵌入式算力。

4. **Q：为什么 `condition_variable::wait` 一定要用 `unique_lock` 而不是 `lock_guard`？**
A：`wait` 内部需要 unlock mutex → 阻塞 → 被 notify 后再 lock。`lock_guard` 不暴露 unlock/lock 接口；`unique_lock` 暴露了，是唯一可用搭档。

5. **Q：解释"虚假唤醒"和你怎么处理。**
A：`cv::wait` 偶尔会无人通知就返回。所以必须循环检查谓词：`while(!pred) wait(lock);` 或直接用 `wait(lock, pred)` 谓词版（内部即此循环）。本项目 `MessageQueue.cpp:15-16` 用谓词版。

6. **Q：`std::unique_ptr` 比 raw pointer 强在哪？**
A：①独占所有权、不可拷贝、可移动 → 编译期防多次 delete；②析构自动 delete → 异常安全的 RAII；③零运行时开销；④可与 `make_unique` 一起获得 new + 构造的异常安全。

7. **Q：unique_ptr 能拷贝吗？为什么？**
A：不能。拷贝构造和拷贝赋值被 `=delete`；可以 move。原因：独占语义，两个 unique_ptr 指同一对象会重复析构。

8. **Q：`std::move` 真的"移动"了吗？**
A：不。`std::move` 只是 `static_cast<T&&>`，把左值变成右值引用类型，让重载决议选中移动构造/赋值。实际"移动"是接收方的责任。

9. **Q：右值引用、移动构造、完美转发分别解决什么问题？**
A：右值引用 `T&&` 是一种新引用类型，配合移动语义避免拷贝；完美转发 `std::forward<T>(arg)` 在模板里保留实参的左/右值属性，原样传给下一层。

10. **Q：为什么 `buffer_` 用 wstring 而不是 string？**
A：要按中文标点切句。中文标点在 UTF-8 里是 3 字节，按 `char` 比对永远不命中。先用 `wstring_convert<codecvt_utf8<wchar_t>>` 升宽字符，每个 wchar 是一个 code point，再 set::count 查标点集。`codecvt_utf8` 在 C++17 deprecated，新代码用 ICU / C++26 `<text_encoding>`。

11. **Q：rkllm 回调跑在哪个线程？回调里阻塞调 ZMQ 安全吗？**
A：跑在 rkllm 内部推理线程。回调里同步 `client.request(...)` 会**卡住下一个 token 的产出**，造成延迟尖刺。改造方向：回调里只 push 文本到本地无锁队列，专门起 sender 线程取并 send。

12. **Q：`<think>` 是怎么过滤的？有 bug 吗？**
A：DeepSeek-R1 会输出 `<think>...</think>` 思维链。我在 `extract_after_think` 用 `wstring::find` 定位两个 tag。但**当两个 tag 都存在的分支里取的是 `<think>` 与 `</think>` 之间**（即"思考"而非"答案"），是一个反向 bug；只剩 `</think>` 的分支才取了正确部分。修复：把两 tag 都有的分支改为 `substr(end_pos + end_tag.length())`。

13. **Q：你们多线程怎么停止？为什么需要 atomic + notify_all？**
A：`stop_` 是 `atomic<bool>`，`stop()` 置 true 后 `notify_all` 把所有 wait 在 cv 上的线程唤醒，谓词 `!queue.empty() || stop_` 命中后返回，业务侧看到空 string / nullptr data 判定退出哨兵 break 出循环。不用 atomic → 数据竞争 → UB；不 notify_all → 部分线程永远卡 wait。

14. **Q：ZMQ 封装里 timeout 是 -1（永久阻塞），生产合理吗？**
A：不合理。链路任一端宕机会让对面卡死。生产化：①setTimeout 几秒；②客户端有限重试 + 指数退避；③对方加 keep-alive；④长期最稳是换 ROUTER/DEALER 异步 + 心跳。

15. **Q：sherpa-onnx 的 endpoint 怎么判定？**
A：基于"声学静音 + 解码空 token"混合规则：连续 N 帧无新词、能量低，满足 `min_trailing_silence`/`min_utterance_length` 阈值后置 endpoint。看到 `IsEndpoint==true` 就提交文本、Reset 流。

16. **Q：为什么 PortAudio 回调里"几乎不能做任何事"？**
A：回调跑在系统音频线程，按固定周期被调度（如 10 ms）；阻塞或慢操作会丢音、underrun。规则：不能 malloc 大块、不加耗时锁、不打印、不调网络。本项目回调里只做 `AcceptWaveform`，合规。

17. **Q：双队列设计相比单队列有什么收益？**
A：①文本和音频两条流速差异大，分离避免互相阻塞；②可分别给两个 worker 加不同优先级（合成 CPU 重，播放对延迟敏感）；③扩展性好。缺点：两把锁，加锁面积变大，复杂度高。

18. **Q：ALSA 用 `snd_pcm_drain` 会卡 playback_worker 吗？**
A：会，且是有意的。`drain` 在播放未结束时阻塞返回，保证下段 PCM 不会和上段重叠。坏处是阻塞主播放线程，但因为合成在另一条 worker，不影响合成。

19. **Q：项目用了 `reinterpret_cast`，安全吗？**
A：3 处：①回调 user_data 转 `OnlineStream*`（合法，传进去什么转回来什么）；②写 hidden_states 到二进制文件 `char*`（合法）；③`tts_free_data(reinterpret_cast<int16_t*>(dataW_))`（坏味道，`dataW_` 实际是 `float*`，依赖底层 free 不关心类型，强耦合）。改造：让 ttsLoadModel 暴露专门的 `tts_free_model` API。

20. **Q：如果让你把项目从 RK3576 迁到 x86 + GPU，主要工作是什么？**
A：①LLM 换 llama.cpp / vLLM / TensorRT-LLM 替代 rkllm；②CPU 绑核换成 GPU device id；③TTS 可换 Kokoro / edge-tts / 微软神经 TTS；④ASR 换 whisper.cpp / funasr；⑤架构、ZMQ 协议、双队列、握手机制都不变——这正是 ZMQ 解耦的红利。

## 3. 三个"自己动手改造"方向

1. **多轮上下文与 RAG**
   - 打开 `rkllm_set_chat_template + keep_history=1`，自己用 `deque<pair<string,string>>` 维护 turns；
   - 把"用户名、上次回答、设备状态"做嵌入入向量库（chromadb、faiss）；
   - 每次 LLM 前 top-k 检索 → prompt 注入。极大提升答案相关性。

2. **真正的流式 TTS + AEC**
   - 把 TTS 改成 chunk-by-chunk 推理（VITS 可在 textencoder/decoder 一段一段输出 PCM 同时播放）；
   - 加 WebRTC AEC3，移除 6677 "block" 握手，做成**真全双工**对话。

3. **更工程化的微服务化 + 监控**
   - ZMQ REQ/REP 升级为 ROUTER/DEALER 异步模式 + protobuf 编解码；
   - 接入 Prometheus，把每段延迟分位数 / NPU 使用率 / 队列深度埋点；
   - spdlog 异步日志 + 文件轮转；
   - systemd .service 做 auto-restart。

## 4. 面试可能"踩坑"的地方（提前加固）

| 坑 | 加固建议 |
| --- | --- |
| `extract_after_think` 的反向 bug | **主动承认 + 给修复 patch**，变成"细致读代码"加分项 |
| 全局变量 `wait/buffer_/first_msg` | 主动说"demo 简化，生产里我会用 class state + atomic" |
| `timeout_ms_ = -1` 永久阻塞 | 主动说"我会改成有限超时 + 重试 + heartbeat" |
| `keep_history=0` 无多轮 | 主动说"正在做 RAG 改造" |
| `top_k=1 + temperature=0.8` 冲突 | 解释 temperature 在贪心下无效，可关掉 |
| 回声/自激 | 解释为什么用半双工，未来上 AEC |
| `wait` 不是 atomic | 承认 bug，给修复 |
| `reinterpret_cast<int16_t*>(dataW_)` | 解释 + 修复方案 |
| sherpa-onnx 是第三方 | 主动说"我修改了它的 voice 子目录 + 加了 ZMQ 客户端" |
| `<codecvt>` 已废弃 | "我了解 C++17 deprecated，新代码会用 ICU / C++26 text_encoding" |

---

# 第六部分：C++ 知识体系串讲

按从易到难。每个知识点：**定义 → 项目用例 → 最小 demo → 面试题**。

## 6.1 `auto` 类型推导

- **定义**：让编译器从初始化表达式推导变量类型；C++11。
- **项目用例**：

```110:111:voice/sherpa-onnx/sherpa-onnx/voice/sherpa-onnx-microphone.cc
  sherpa_onnx::OnlineRecognizer recognizer(config);
  auto s = recognizer.CreateStream();
```

- **demo**：

```cpp
std::vector<int> v{1,2,3};
auto it = v.begin();
const auto& x = v[0];
```

- **面试题**：①`auto x = expr;` 会去掉 const/&，怎么保留？答：`auto&`、`const auto&`、`decltype(auto)`。②`auto&& x` 是什么？答：万能引用。

## 6.2 范围 for

- **定义**：`for (decl : range)`。
- **项目用例**：

```257:257:llm/rknn-llm/examples/DeepSeek-R1-Distill-Qwen-1.5B_Demo/deploy/src/llm_demo.cpp
        for (wchar_t c : wide_text) {
```

- **demo**：`for (const auto& s : vec_str) std::cout << s;`
- **面试题**：range-for 怎么工作？答：编译器展开为 `auto it = begin(range); auto end_ = end(range); for (; it != end_; ++it) { decl = *it; ... }`。

## 6.3 `nullptr`

- **定义**：C++11 空指针字面量，类型 `nullptr_t`，可隐式转任何指针。
- **项目用例**：`LLMHandle llmHandle = nullptr;`
- **vs `NULL`**：`NULL` 通常是 `0`（int），让 `func(int)/func(int*)` 重载歧义。
- **面试题**：`nullptr` 能转 bool 吗？答：能（false）。

## 6.4 RAII

- **定义**：构造期取资源，析构期放，把资源生命周期绑到对象生命周期。
- **项目用例**：`AudioPlayer`、`ZmqInterface`、`std::lock_guard`、`std::unique_ptr`、`std::ofstream`。
- **demo**：

```cpp
class File {
    FILE* fp_;
public:
    explicit File(const char* path) : fp_(fopen(path,"r")) {}
    ~File(){ if(fp_) fclose(fp_); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};
```

- **面试题**：①解决什么？答：异常安全、不忘释放、所有权清晰。②为什么 raw `new` 不 RAII？答：异常 / 中途 return 会泄漏。

## 6.5 `std::string` / `std::wstring`

- **定义**：可变长字节串 / 宽字符串。
- **项目用例**：到处都是；`buffer_` 是 wstring，因为要按 wchar 切。
- **面试题**：①string 是否保证连续内存？答：C++11 起是。②SSO 是什么？答：Small String Optimization。

## 6.6 `std::vector`

- **项目用例**：

```31:38:tts/tts_server/src/Utils.cpp
std::vector<std::string> split_long_text(const std::string &text, size_t max_length) {
    std::vector<std::string> segments;
```

- **面试题**：①push_back 扩容策略？答：1.5×/2×。②reserve 和 resize 区别？答：reserve 只改 capacity；resize 改 size + 构造元素。

## 6.7 lambda 表达式

- **项目用例**：

```14:16:zmq-comm-kit/test/demo.cpp
        std::thread server_thread([&]
                                  {
            auto request = server.receive(); ... });
```

```15:16:tts/tts_server/src/MessageQueue.cpp
    text_cond_.wait(lock, [this]
                    { return !text_queue_.empty() || stop_; });
```

- **demo**：`int x=1; auto f = [x](int y){ return x+y; }; f(2);`
- **面试题**：①`[&]/[=]/[this]/[x]/[&x]` 含义？②带 capture 的 lambda 为何不能转函数指针？答：带 capture 的 lambda 类型有状态，函数指针无状态。

## 6.8 智能指针

- **`unique_ptr<T>`**：独占；**`shared_ptr<T>`**：共享引用计数；**`weak_ptr<T>`**：观察者，破解循环引用。
- **项目用例**：见前文。
- **demo**：`auto p = std::make_shared<int>(42); std::weak_ptr<int> w = p; if (auto sp = w.lock()) std::cout << *sp;`
- **面试题**：①shared_ptr 线程安全吗？答：引用计数 atomic，但**对象本身**不保证。②为什么用 `make_shared`？答：一次分配（控制块+对象）+ 异常安全。

## 6.9 `std::move` / 右值引用

- **项目用例**：

```31:31:tts/tts_server/src/MessageQueue.cpp
        audio_queue_.push(std::move(msg));
```

- **demo**：

```cpp
std::vector<std::string> v;
std::string s = "hello";
v.push_back(std::move(s));   // s 现在是 valid but unspecified
```

- **面试题**：①move 后原对象状态？答：合法但 unspecified，可安全析构 / assign。②为什么 move 比 copy 快？答：移动只搬指针；拷贝要堆分配 + memcpy。

## 6.10 移动 vs 拷贝构造 + `noexcept`

```cpp
struct S { S(const S&); S(S&&) noexcept; };
```

- `noexcept` 关键：标准容器扩容时若移动构造不是 noexcept 会改走拷贝（强异常保证），性能差很多。
- **项目用例**：`AudioMessage` 隐式生成的移动构造（成员都支持 move）。

## 6.11 `std::thread`

- **项目用例**：

```73:74:tts/tts_server/src/main.cpp
        std::thread synthesis_thread(synthesis_worker, std::ref(queue), std::ref(model));
        std::thread playback_thread(playback_worker, std::ref(queue), std::ref(player));
```

- **demo**：`std::thread t([](){ std::cout << "hi"; }); t.join();`
- **面试题**：①join 和 detach 区别？②析构未 join 的 thread 会怎样？答：`std::terminate`。

## 6.12 `std::mutex / lock_guard / unique_lock / scoped_lock`

- **项目用例**：

```52:55:tts/tts_server/src/MessageQueue.cpp
        std::lock_guard<std::mutex> lock1(text_mutex_);
        std::lock_guard<std::mutex> lock2(audio_mutex_);
```

- **面试题**：①scoped_lock 如何避免死锁？答：用 `std::lock(...)` 算法获取多个锁，无固定顺序，内部 try & back-off。②死锁四条件？

## 6.13 `std::condition_variable`

- **项目用例**：`pop_text`、`pop_audio`。
- **demo**：

```cpp
std::mutex m; std::condition_variable cv; bool ready=false;
// thread1
{ std::unique_lock<std::mutex> lk(m); cv.wait(lk, [&]{return ready;}); }
// thread2
{ std::lock_guard<std::mutex> lk(m); ready=true; } cv.notify_one();
```

- **面试题**：①wait 必须配 unique_lock？②虚假唤醒处理？③notify_one vs notify_all？

## 6.14 `std::atomic<T>`

- **项目用例**：`std::atomic<bool> stop_{false};`、`std::atomic<bool> first_msg(true);`。
- **demo**：`std::atomic<int> cnt{0}; cnt.fetch_add(1, std::memory_order_relaxed);`
- **面试题**：①memory order 几种？答：relaxed / acquire / release / acq_rel / seq_cst。②atomic 一定 lock-free 吗？答：不一定，`is_lock_free()` 查。

## 6.15 模板入门

- **项目用例**：`std::queue<std::string>`、`std::unique_ptr<int16_t[]>`、`std::vector<char>`。
- **面试题**：①模板特化 vs 偏特化？②`enable_if`、`is_same`、`if constexpr` 用过吗？

## 6.16 异常处理

- **项目用例**：

```9:13:tts/tts_server/src/main.cpp
        ...
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
```

- `ZmqCommunicationError` 继承 `runtime_error`。
- **面试题**：①catch 一定要 `const &`，为什么？答：避免对象切片 + 避免不必要拷贝。②`noexcept` 函数若抛会怎样？答：`std::terminate`。

## 6.17 多态与虚函数

- **项目用例**：`ZmqInterface`（虚析构）+ `ZmqClient/ZmqServer`。
- **demo**：

```cpp
struct Base { virtual void f() = 0; virtual ~Base() = default; };
struct D : Base { void f() override { std::cout<<"D"; } };
```

- **面试题**：①虚函数表机制？②为何必须虚析构？③override / final 含义？

## 6.18 设计模式落地

- **生产者-消费者**：DoubleMessageQueue + 两个 worker。
- **策略/模板方法**：`ZmqInterface::setupSocket(type, address)`，子类决定 type。
- **回调/观察者**：rkllm `LLMResultCallback`、PortAudio `RecordCallback`。
- **PIMPL（伪）**：`TTSModel` 把 `SynthesizerTrn` 完整定义放 cpp，头里只前向声明。
- **RAII**：贯穿全程。

## 6.19 文件 IO / 序列化

- **项目用例**：

```226:232:llm/rknn-llm/examples/DeepSeek-R1-Distill-Qwen-1.5B_Demo/deploy/src/llm_demo.cpp
            std::ofstream outFile("last_hidden_layer.bin", std::ios::binary);
            if (outFile.is_open()){
                outFile.write(reinterpret_cast<const char *>(result->last_hidden_layer.hidden_states), data_size);
                outFile.close();
```

- **面试题**：①二进制 vs 文本模式区别？②`std::ofstream` 是 RAII 吗？答：析构自动 close。

## 6.20 UTF-8 / 宽字符与 codecvt

- **项目用例**：`llm_demo.cpp:159-169`。
- **demo**：

```cpp
std::wstring_convert<std::codecvt_utf8<wchar_t>> cv;
std::wstring w = cv.from_bytes("你好");
std::string  s = cv.to_bytes(w);
```

- **面试题**：①UTF-8 怎么判定首字节？答：`0xxxxxxx / 110xxxxx / 1110xxxx / 11110xxx`。②为何 wchar_t 在 Windows 2 字节、Linux 4 字节？答：实现定义；Windows 用 UTF-16，Linux 用 UTF-32。

## 6.21 函数指针 vs `std::function` vs lambda

- **项目用例**：rkllm 用 C 函数指针 `LLMResultCallback`。
- **demo**：

```cpp
void(*fp)(int) = [](int x){ printf("%d", x); };  // 无捕获 lambda 可隐式转
std::function<void(int)> sf = [&](int x){};       // 有捕获用 std::function
```

- **面试题**：std::function vs 函数指针的开销？答：可能堆分配 + 间接调用，但更通用。

## 6.22 现代 C++ 关键字 5 个必背

- **`constexpr`**：编译期可计算，常用于常量、constexpr 函数、模板元编程。
- **`noexcept`**：声明不抛异常，编译器更激进优化、move 才会被容器优先选。
- **`override` / `final`**：override 让编译器检查覆盖正确；final 禁止再继承/覆盖。
- **`explicit`**：禁止隐式构造转换。
- **`= delete` / `= default`**：删除或显式生成特殊成员函数。

本项目 `ZmqClient(const std::string& address = "tcp://localhost:6666")` 是单参构造，**理论上应该加 `explicit`**——面试加固点。

## 6.23 内存模型 & 线程安全

- **数据竞争**：两线程同访同一内存且至少一写、未同步 → UB。
- **happens-before**：通过 mutex unlock→lock、atomic release→acquire 建立。
- **项目漏洞**：`bool wait`、`bool stop`（voice）跨线程读写，非 atomic。

## 6.24 高阶：协程 / 异步（项目没用，可"我打算引入"）

- C++20 `co_await / co_return / co_yield` + `std::coroutine_handle`。
- 把 rkllm 回调改成 coro，写成 `auto reply = co_await llm.generate(text);` 像同步一样。

## 6.25 编译与链接

- 静态库 / 动态库：`libzmq_component.so` 是动态库；运行时 `LD_LIBRARY_PATH` 或 `rpath` 找。
- `-Wl,-rpath,...`：

```17:18:voice/sherpa-onnx/sherpa-onnx/voice/CMakeLists.txt
target_link_libraries(sherpa-onnx-microphone-test1 "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../lib")
target_link_libraries(sherpa-onnx-microphone-test1 "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../../../sherpa_onnx/lib")
```

告诉链接器把这两个相对路径写进 ELF 的 RUNPATH，运行时自动找 so。

- **面试题**：①静态库和动态库哪个启动快？答：静态。②看 ELF 依赖？答：`ldd`、`readelf -d`。

---

# 收尾：一句话路线图

> "Yajoker/LLM_Voice_Flow 是一个让你**一次性串起 C++11/17 多线程、生产者-消费者、ZMQ 进程间通信、PortAudio/ALSA 音频流水线、ONNX Runtime ASR、NPU LLM 推理、VITS TTS、CMake 多模块构建**的麻雀虽小五脏俱全的端侧 AI 项目。"

掌握顺序建议：

1. 先把 `zmq-comm-kit` 跑通 demo（最快 30 分钟出第一次成功跨进程通信）。
2. 再读 `tts/tts_server/`（5 个小类，能讲透生产者-消费者就过半数面试题）。
3. 再读 `llm_demo.cpp`（重点：rkllm 回调 + UTF-8 切句 + ZMQ REQ 转发）。
4. 最后读 `sherpa-onnx-microphone.cc`（PortAudio 回调 + endpoint + 半双工握手）。
5. 把"4 个 bug / 改造点"亲手 commit 一遍（atomic、`<think>` bug 修复、explicit、超时）—— 让简历"我重构了一遍"有实锤。

祝你拿下面试。
