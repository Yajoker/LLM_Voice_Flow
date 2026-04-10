# 端侧语音AI系统 —— 从零开始的项目学习指南

> 本文档是为C++基础不太扎实的学习者编写的，目标是带你一步步读懂、理解整个项目。
> 建议按照阶段顺序阅读，每个阶段理解后再进入下一阶段。

---

## 阶段一：先别看代码——建立全局认知

### 1.1 这个项目到底在做什么？

**一句话总结**：这是一个"不联网也能和你聊天"的语音助手，跑在一块巴掌大的开发板上。

用生活中的类比来理解：

想象你有一个**私人翻译团队**，由三个人组成：
- **小耳（ASR）**：负责"听"——把你说的话变成文字
- **小脑（LLM）**：负责"想"——读懂文字，想出回答
- **小嘴（TTS）**：负责"说"——把回答变成语音播放出来

在云端（比如Siri、小爱同学），这三个人都在"远程办公"——你的语音需要通过网络传到服务器，处理后再传回来。而这个项目做的事情是：**把这三个人全部搬到一块RK3576开发板上**，让他们在本地工作，完全不需要网络。

这就是所谓的"**端侧部署**"或"**边缘端AI**"。

### 1.2 模块关系图

```
┌─────────────────────────────────────────────────────────────────┐
│                     RK3576 开发板（边缘设备）                      │
│                                                                 │
│  ┌──────────────┐   ZMQ (端口6666)   ┌──────────────┐          │
│  │              │ ─────────────────> │              │          │
│  │   ASR模块     │   "你好,今天天气     │   LLM模块    │          │
│  │ (sherpa-onnx) │    怎么样"          │ (rknn-llm)   │          │
│  │              │                    │              │          │
│  │  麦克风输入    │                    │  NPU推理      │          │
│  │  语音→文字    │                    │  文字→回答     │          │
│  └──────────────┘                    └──────┬───────┘          │
│        ^                                     │                  │
│        │                            ZMQ (端口7777)              │
│        │ ZMQ (端口6677)                      │                  │
│        │ (播放完成通知)                       v                  │
│  ┌─────┴────────────────────────────────────────────┐          │
│  │                    TTS模块                        │          │
│  │               (tts_server)                        │          │
│  │                                                   │          │
│  │   接收文本 → 合成语音 → 播放到扬声器                  │          │
│  │                                                   │          │
│  │   [ZMQ接收线程] → [合成线程] → [播放线程]             │          │
│  └───────────────────────────────────────────────────┘          │
│                                                                 │
│  ┌──────────────┐                                               │
│  │ zmq-comm-kit │  ← 所有模块共用的"通信工具箱"                    │
│  │  (通信封装库)  │                                               │
│  └──────────────┘                                               │
└─────────────────────────────────────────────────────────────────┘
```

**四个模块各自的角色**：

| 模块 | 对应目录 | 角色 | 核心技术 |
|------|---------|------|---------|
| ASR（语音识别） | `voice/sherpa-onnx/` | 听：把语音变成文字 | ONNX Runtime 推理 |
| LLM（大语言模型） | `llm/` | 想：理解问题，生成回答 | RKNN NPU 推理 |
| TTS（语音合成） | `tts/` | 说：把文字变成语音播放 | Eigen 矩阵运算 + ALSA音频 |
| 通信层 | `zmq-comm-kit/` | 传话：模块之间的消息传递 | ZeroMQ 封装 |

### 1.3 一个完整的使用场景

让我们跟着一句话走完整个系统：

**用户对着麦克风说**："今天天气怎么样？"

```
步骤1 [ASR模块 - 听]
   麦克风采集到音频波形（一串数字）
   → sherpa-onnx 的流式识别器不断处理音频帧
   → 检测到说话结束（端点检测/VAD）
   → 输出文字："今天天气怎么样"

步骤2 [ASR→LLM - 传话]
   ASR模块通过 ZeroMQ 把文字发送到端口 6666
   → LLM模块的 ZmqServer 在端口 6666 监听，收到文字

步骤3 [LLM模块 - 想]
   RKNN LLM 在 NPU 上运行 DeepSeek 模型
   → 模型流式生成回答，一段一段地输出
   → 比如先输出"今天是晴天"，再输出"，温度大概25度"

步骤4 [LLM→TTS - 传话]
   LLM模块每生成一段文字，就通过 ZeroMQ 发到端口 7777
   → TTS模块的 ZmqServer 在端口 7777 监听，收到文字段

步骤5 [TTS模块 - 说]
   收到文字 → 放入文本队列
   → 合成线程从队列取出文字，调用TTS模型推理，生成PCM音频数据
   → 音频数据放入音频队列
   → 播放线程从音频队列取出数据，通过ALSA驱动送到扬声器
   → 用户听到："今天是晴天，温度大概25度"

步骤6 [TTS→ASR - 通知]
   播放完毕后，TTS通过端口 6677 通知ASR："我说完了"
   → ASR重新开始监听麦克风，等待用户下一句话
```

**关键设计思想**：
- **流水线并行**：LLM不需要等全部想完再说，而是想到一点说一点（流式输出）
- **双缓冲队列**：TTS内部有"文本队列"和"音频队列"两级缓冲，合成和播放可以并行
- **松耦合**：每个模块是独立进程，通过ZMQ通信，可以单独开发、测试、替换

---

## 阶段二：前置知识补课

### 2.1 CMake 构建系统

**CMake是什么？** 它是一个"构建系统生成器"。C++代码不能直接运行，需要编译。CMake帮你生成编译规则。

**核心概念**：

```cmake
# 最低CMake版本要求
cmake_minimum_required(VERSION 3.12)

# 项目名称和版本
project(zmq_component VERSION 1.0.0 LANGUAGES CXX)

# 找到系统已安装的库（类似Python的 import）
find_package(PkgConfig REQUIRED)
pkg_search_module(ZMQ REQUIRED libzmq)

# 创建一个"共享库"（.so文件），由这些源文件编译而成
add_library(zmq_component SHARED
    src/ZmqInterface.cpp
    src/ZmqServer.cpp
    src/ZmqClient.cpp
)

# 这个库需要链接的其他库
target_link_libraries(zmq_component zmq Threads::Threads)

# 创建一个可执行文件
add_executable(demo test/demo.cpp)

# 这个可执行文件需要链接我们刚才创建的库
target_link_libraries(demo zmq_component)
```

**关键术语**：
- **target（目标）**：你要构建的东西，可以是库（library）或可执行文件（executable）
- **library（库）**：一堆编译好的函数，给别人调用。`SHARED`=动态库（.so），`STATIC`=静态库（.a）
- **executable（可执行文件）**：最终能运行的程序
- **find_package**：在系统里找已安装的库
- **add_subdirectory**：把另一个目录的CMakeLists.txt包含进来，类似"子项目"
- **target_include_directories**：告诉编译器去哪里找头文件
- **target_link_libraries**：告诉链接器需要哪些库

### 2.2 ZeroMQ 基础

**什么是消息队列？** 想象两个人打电话 vs 寄信：
- 打电话（TCP socket）：必须双方同时在线，实时对话
- 寄信（消息队列）：写好信扔进邮箱，对方有空了再取。即使对方暂时不在也没关系

**ZeroMQ** 是一个高性能的消息库，比直接用socket简单很多。

**REQ/REP 模式**（本项目使用的模式）：

```
客户端(REQ)                    服务端(REP)
   │                              │
   │──── 发送请求 ──────────────>│
   │                              │── 处理请求
   │<──── 返回响应 ──────────────│
   │                              │
```

这就像"去柜台办事"：你（REQ）提交材料，柜员（REP）处理后给你回执。
必须严格遵循"一问一答"的节奏。

**为什么用ZMQ而不是原生socket？**
- socket需要处理很多底层细节（连接管理、字节序、粘包拆包）
- ZMQ帮你处理好了这些，你只需要`send()`和`recv()`
- ZMQ支持多种通信模式（REQ/REP、PUB/SUB等），切换方便
- ZMQ还支持进程内通信（inproc://）、进程间通信（ipc://）、网络通信（tcp://）

**最小示例**：

```cpp
// 服务端
zmq::context_t context(1);
zmq::socket_t socket(context, ZMQ_REP);
socket.bind("tcp://*:6666");       // 监听6666端口

zmq::message_t request;
socket.recv(request);              // 阻塞等待消息
std::string msg(static_cast<char*>(request.data()), request.size());

zmq::message_t reply(5);
memcpy(reply.data(), "Hello", 5);
socket.send(reply);                // 回复

// 客户端
zmq::context_t context(1);
zmq::socket_t socket(context, ZMQ_REQ);
socket.connect("tcp://localhost:6666");  // 连接服务端

zmq::message_t request(5);
memcpy(request.data(), "World", 5);
socket.send(request);              // 发送请求

zmq::message_t reply;
socket.recv(reply);                // 接收回复
```

### 2.3 ONNX Runtime 基础

**ONNX是什么？** ONNX (Open Neural Network Exchange) 是一种通用的AI模型格式，就像PDF是文档的通用格式一样。不管你用PyTorch还是TensorFlow训练的模型，都可以导出成`.onnx`文件。

**推理引擎是什么？** 训练好的模型就像一张"配方"，推理引擎就是按照配方"做菜"的厨师。ONNX Runtime就是这样的推理引擎——给它一个ONNX模型和输入数据，它帮你算出结果。

**核心概念**：
- **Session（会话）**：加载模型后创建的"工作环境"。你把数据扔给Session，它返回结果
- **ExecutionProvider（执行提供者）**：决定在哪里运算——CPU、GPU、还是NPU

```cpp
// 简化的ONNX Runtime使用流程
Ort::Env env;                                    // 初始化环境
Ort::SessionOptions session_options;             // 配置选项
Ort::Session session(env, "model.onnx", session_options);  // 加载模型

// 准备输入数据 → 运行推理 → 获取输出
auto output = session.Run(..., input_tensors, ...);
```

在本项目中，`sherpa-onnx`就是在ONNX Runtime之上构建的语音处理工具包，它封装了ASR、TTS、VAD等模型的完整推理流程。

### 2.4 Eigen 基础

**Eigen** 是一个C++矩阵运算库。AI模型本质上是大量的矩阵乘法和数学运算。

在本项目的TTS模块中，Eigen充当的是"数学计算引擎"的角色——TTS模型没有使用ONNX Runtime，而是手动实现了神经网络的前向传播，所有的卷积、矩阵乘法、激活函数等操作都靠Eigen来完成。

```cpp
#include <Eigen/Dense>

// 创建矩阵
Eigen::MatrixXf A(2, 3);  // 2行3列的浮点矩阵
Eigen::VectorXf v(3);     // 3维向量

// 矩阵乘法（神经网络的核心操作）
Eigen::VectorXf result = A * v;  // 就这么简单
```

为什么TTS用Eigen而不用ONNX Runtime？因为SummerTTS是一个轻量级实现，直接用C++/Eigen手写推理，不依赖额外的推理引擎，更适合嵌入式设备。

### 2.5 多线程基础

**为什么需要多线程？** TTS模块需要同时做三件事：接收文字、合成语音、播放语音。如果串行执行，播放完一句才能合成下一句，用户体验会很差。

**核心组件**：

```cpp
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

// std::thread - 创建线程
std::thread worker([]() {
    // 这段代码会在新线程中运行
    std::cout << "我在另一个线程里！" << std::endl;
});
worker.join();  // 等待线程结束

// std::mutex - 互斥锁（防止多个线程同时修改同一个数据）
std::mutex mtx;
mtx.lock();     // 上锁 —— 别人进不来
// ... 安全地操作共享数据 ...
mtx.unlock();   // 解锁 —— 别人可以进来了

// std::lock_guard - 更安全的上锁方式（自动解锁）
{
    std::lock_guard<std::mutex> lock(mtx);  // 进入作用域时上锁
    // ... 操作共享数据 ...
}  // 离开作用域时自动解锁（即使发生异常也会解锁）

// std::condition_variable - 条件变量（线程间的通知机制）
std::condition_variable cv;
std::queue<std::string> queue;

// 消费者线程：等待数据
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, [&]{ return !queue.empty(); });  // 队列空就睡觉，有数据就醒来
auto data = queue.front();
queue.pop();

// 生产者线程：放入数据并通知
{
    std::lock_guard<std::mutex> lock(mtx);
    queue.push("new data");
}
cv.notify_one();  // 叫醒一个等待的消费者
```

**生产者-消费者模型**：本项目的TTS模块就是典型的两级生产者-消费者：
```
[ZMQ接收] --push_text--> [文本队列] --pop_text--> [合成线程]
                                                      |
                                                 push_audio
                                                      |
                                                      v
[扬声器] <--pop_audio-- [音频队列] <── [合成线程产出的音频]
```

### 2.6 项目中用到的关键C++特性

#### 智能指针（Smart Pointers）

普通指针需要手动`delete`，忘了就内存泄漏。智能指针帮你自动管理内存。

```cpp
// std::unique_ptr - 独占所有权，不能复制，只能移动
std::unique_ptr<int[]> audio_data = std::make_unique<int[]>(1024);
// 离开作用域时自动释放内存，不需要手动delete

// std::move - 转移所有权
auto new_owner = std::move(audio_data);
// 现在 audio_data 变成空了，new_owner 拥有那块内存
```

#### RAII（资源获取即初始化）

"谁创建，谁负责清理"——通过构造函数获取资源，析构函数释放资源。

```cpp
class AudioPlayer {
public:
    AudioPlayer() { initialize(); }   // 构造时：打开音频设备
    ~AudioPlayer() { cleanup(); }     // 析构时：关闭音频设备
    // 不需要手动调用cleanup()，对象销毁时自动调用
};
```

#### 虚函数与多态

```cpp
// 基类定义接口
class OnlineRecognizerImpl {
public:
    virtual void DecodeStreams(...) = 0;  // = 0 表示"纯虚函数"，子类必须实现
    virtual ~OnlineRecognizerImpl() = default;
};

// 不同子类提供不同实现
class OnlineRecognizerTransducerImpl : public OnlineRecognizerImpl {
    void DecodeStreams(...) override { /* Transducer模型的解码逻辑 */ }
};

class OnlineRecognizerCtcImpl : public OnlineRecognizerImpl {
    void DecodeStreams(...) override { /* CTC模型的解码逻辑 */ }
};

// 使用时：通过基类指针调用，自动选择正确的实现
std::unique_ptr<OnlineRecognizerImpl> impl = Create(config);
impl->DecodeStreams(...);  // 到底调哪个？取决于Create()返回了哪个子类
```

#### std::atomic

```cpp
std::atomic<bool> first_msg(true);  // 原子变量，多线程读写安全
first_msg = false;  // 不需要加锁，编译器保证操作的原子性
```

---

## 阶段三：从最小的模块开始读代码 —— zmq-comm-kit

### 3.1 目录结构

```
zmq-comm-kit/
├── CMakeLists.txt          # 构建配置
├── include/                # 头文件（.h）——定义接口
│   ├── zmq.hpp             # ZeroMQ的C++头文件封装（第三方提供）
│   ├── ZmqInterface.h      # 基类：定义共有行为
│   ├── ZmqClient.h         # 客户端类
│   └── ZmqServer.h         # 服务端类
├── src/                    # 源文件（.cpp）——实现逻辑
│   ├── ZmqInterface.cpp
│   ├── ZmqClient.cpp
│   └── ZmqServer.cpp
└── test/
    └── demo.cpp            # 使用示例
```

### 3.2 类的继承关系

```
        ZmqInterface（基类）
       /            \
  ZmqClient        ZmqServer
  (客户端)          (服务端)
```

### 3.3 逐文件代码讲解

#### 3.3.1 ZmqInterface.h —— 基类定义

> 文件路径：`zmq-comm-kit/include/ZmqInterface.h`

```cpp
#pragma once                    // 防止头文件被重复包含
#include <zmq.hpp>              // ZeroMQ的C++封装
#include <memory>               // 智能指针
#include <stdexcept>            // 异常类
#include <string>

namespace zmq_component {       // 命名空间，避免名字冲突

// 自定义异常类，继承自标准异常
class ZmqCommunicationError : public std::runtime_error {
public:
    explicit ZmqCommunicationError(const std::string& what);
    // explicit 关键字：防止隐式类型转换
    // 比如不能写 throw "some error"，必须写 throw ZmqCommunicationError("some error")
};

class ZmqInterface {
protected:  // protected：自己和子类可以访问，外部不行
    std::unique_ptr<zmq::context_t> context_;   // ZMQ上下文（管理所有socket的"大管家"）
    std::unique_ptr<zmq::socket_t> socket_;     // ZMQ套接字（实际收发消息的"窗口"）
    int timeout_ms_ = -1;                       // 超时时间，-1表示无限等待

    void setupSocket(int socket_type, const std::string& address);
    // socket_type: ZMQ_REQ(客户端) 或 ZMQ_REP(服务端)
    // address: 如 "tcp://localhost:6666"
    
public:
    virtual ~ZmqInterface();    // virtual析构函数：确保通过基类指针删除子类时能正确清理
    void setTimeout(int milliseconds);
};

} // namespace zmq_component
```

**知识点解释**：
- `std::unique_ptr<zmq::context_t>`：用智能指针管理ZMQ对象的生命周期，离开作用域自动释放
- `context_t`（上下文）：ZMQ的"运行环境"，一个程序通常只需要一个。把它想象成"邮局总部"
- `socket_t`（套接字）：基于context创建的"收发窗口"，真正干活的。把它想象成"邮局的柜台"
- 变量名尾部的下划线 `_`：C++的命名约定，表示这是类的成员变量

#### 3.3.2 ZmqInterface.cpp —— 基类实现

> 文件路径：`zmq-comm-kit/src/ZmqInterface.cpp`

```cpp
#include "ZmqInterface.h"

namespace zmq_component {

// 自定义异常的构造函数，在原始消息前加上 "ZMQ Error: " 前缀
ZmqCommunicationError::ZmqCommunicationError(const std::string& what)
    : std::runtime_error("ZMQ Error: " + what) {}

void ZmqInterface::setupSocket(int socket_type, const std::string& address) {
    try {
        // 1. 创建上下文，参数1表示IO线程数
        context_ = std::make_unique<zmq::context_t>(1);
        // 2. 创建套接字
        socket_ = std::make_unique<zmq::socket_t>(*context_, socket_type);
        
        // 3. 设置收发超时
        socket_->set(zmq::sockopt::rcvtimeo, timeout_ms_);
        socket_->set(zmq::sockopt::sndtimeo, timeout_ms_);

        // 4. 关键区别：服务端bind()，客户端connect()
        //    三元运算符：条件 ? 真 : 假
        (socket_type == ZMQ_REP) ? socket_->bind(address) 
                                  : socket_->connect(address);
        // bind("tcp://*:6666")  → 服务端：我在6666端口等着，谁来都接待
        // connect("tcp://localhost:6666") → 客户端：我去连接6666端口的服务端
    } catch (const zmq::error_t& e) {
        throw ZmqCommunicationError(e.what());
    }
}

ZmqInterface::~ZmqInterface() {
    // RAII：析构时自动关闭socket和context，释放资源
    if (socket_) socket_->close();
    if (context_) context_->close();
}

void ZmqInterface::setTimeout(int milliseconds) {
    timeout_ms_ = milliseconds;
    if (socket_) {
        socket_->set(zmq::sockopt::rcvtimeo, timeout_ms_);
        socket_->set(zmq::sockopt::sndtimeo, timeout_ms_);
    }
}

} // namespace zmq_component
```

**设计要点**：`setupSocket` 用一个函数同时处理了客户端和服务端的初始化，区别仅在于 `bind` vs `connect`。这是一个简洁的设计——共性代码放基类，差异通过参数处理。

#### 3.3.3 ZmqServer.h + ZmqServer.cpp —— 服务端

> 头文件路径：`zmq-comm-kit/include/ZmqServer.h`

```cpp
#pragma once
#include "ZmqInterface.h"

namespace zmq_component {

class ZmqServer : public ZmqInterface {  // 继承自ZmqInterface
public:
    // 构造函数，默认监听所有网卡的6666端口
    // "tcp://*:6666" 中的 * 表示接受来自任何IP的连接
    explicit ZmqServer(const std::string& address = "tcp://*:6666");
    
    std::string receive();                    // 接收消息（阻塞等待）
    void send(const std::string& response);   // 发送响应
};

} // namespace zmq_component
```

> 实现文件路径：`zmq-comm-kit/src/ZmqServer.cpp`

```cpp
#include "ZmqServer.h"

namespace zmq_component {

ZmqServer::ZmqServer(const std::string& address) {
    setupSocket(ZMQ_REP, address);  // ZMQ_REP = Reply模式，即服务端
}

std::string ZmqServer::receive() {
    zmq::message_t request;           // 创建消息容器
    if (!socket_->recv(request)) {    // 阻塞等待，直到收到消息或超时
        throw ZmqCommunicationError("Receive timeout");
    }
    // 将收到的原始字节转换为std::string
    // static_cast<char*>(request.data()) 获取消息的字节指针
    // request.size() 获取消息长度
    return {static_cast<char*>(request.data()), request.size()};
}

void ZmqServer::send(const std::string& response) {
    zmq::message_t reply(response.size());                    // 创建指定大小的消息
    memcpy(reply.data(), response.data(), response.size());   // 复制数据到消息中
    if (!socket_->send(reply, zmq::send_flags::none)) {
        throw ZmqCommunicationError("Send timeout");
    }
}

} // namespace zmq_component
```

#### 3.3.4 ZmqClient.h + ZmqClient.cpp —— 客户端

> 头文件路径：`zmq-comm-kit/include/ZmqClient.h`

```cpp
#pragma once
#include "ZmqInterface.h"

namespace zmq_component {

class ZmqClient : public ZmqInterface {
public:
    // 默认连接本机6666端口
    explicit ZmqClient(const std::string& address = "tcp://localhost:6666");
    
    void sendRequest(const std::string& message);     // 只发送
    std::string receiveResponse();                     // 只接收
    std::string request(const std::string& message);   // 发送+接收（一步到位）
};

} // namespace zmq_component
```

> 实现文件路径：`zmq-comm-kit/src/ZmqClient.cpp`

```cpp
#include "ZmqClient.h"

namespace zmq_component {

ZmqClient::ZmqClient(const std::string& address) {
    setupSocket(ZMQ_REQ, address);  // ZMQ_REQ = Request模式，即客户端
}

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

// 便利方法：发送请求并等待响应
std::string ZmqClient::request(const std::string& message) {
    sendRequest(message);
    return receiveResponse();
}

} // namespace zmq_component
```

#### 3.3.5 demo.cpp —— 使用示例

> 文件路径：`zmq-comm-kit/test/demo.cpp`

```cpp
#include "ZmqServer.h"
#include "ZmqClient.h"
#include <iostream>
#include <thread>

int main() {
    try {
        // 创建服务端和客户端（使用默认地址 tcp://*:6666 和 tcp://localhost:6666）
        zmq_component::ZmqServer server;
        zmq_component::ZmqClient client;

        // 服务端在单独线程中运行（因为receive()会阻塞）
        std::thread server_thread([&] {
            // [&] 是Lambda表达式，捕获外部变量server的引用
            auto request = server.receive();   // 阻塞等待客户端消息
            std::cout << "Server received: " << request << std::endl;
            server.send("Echo: " + request);   // 回复：在消息前加 "Echo: "
        });

        // 客户端发送请求并接收响应
        auto response = client.request("Hello World!");
        std::cout << "Client received: " << response << std::endl;
        // 输出: Client received: Echo: Hello World!

        server_thread.join();  // 等待服务端线程结束
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

### 3.4 模块总结

**zmq-comm-kit 做了什么？**
把ZeroMQ的C API封装成了两个简洁的C++类（`ZmqServer` 和 `ZmqClient`），让其他模块只需要几行代码就能实现进程间通信。

**暴露的接口**：

| 类 | 方法 | 作用 |
|---|------|-----|
| `ZmqServer` | `receive()` | 阻塞等待并接收消息 |
| `ZmqServer` | `send(msg)` | 发送响应 |
| `ZmqClient` | `request(msg)` | 发送请求并等待响应（一步到位） |
| `ZmqClient` | `sendRequest(msg)` | 只发送请求 |
| `ZmqClient` | `receiveResponse()` | 只接收响应 |
| 基类通用 | `setTimeout(ms)` | 设置超时时间 |

**其他模块怎么用它？**

```cpp
// TTS服务端 - 监听7777端口等待LLM发来的文字
zmq_component::ZmqServer server("tcp://*:7777");
std::string text = server.receive();
server.send("收到了");

// LLM客户端 - 连接TTS的7777端口发送文字
zmq_component::ZmqClient tts_client("tcp://localhost:7777");
auto response = tts_client.request("今天天气不错");
```

**CMakeLists.txt中的构建方式**：编译为共享库`libzmq_component.so`，其他模块通过链接这个库来使用。

---

## 阶段四：读 tts_server —— 理解一个完整的服务是怎么跑起来的

### 4.1 目录结构

```
tts/tts_server/
├── include/
│   ├── AudioPlayer.h       # 音频播放器
│   ├── MessageQueue.h      # 双缓冲消息队列
│   ├── TTSModel.h          # TTS模型封装
│   ├── TextProcessor.h     # 文本预处理
│   └── Utils.h             # 工具函数
└── src/
    ├── main.cpp            # 程序入口 ★从这里开始读
    ├── AudioPlayer.cpp     # ALSA音频播放实现
    ├── MessageQueue.cpp    # 线程安全队列实现
    ├── TTSModel.cpp        # TTS推理封装
    ├── TextProcessor.cpp   # 文本清洗
    └── Utils.cpp           # 工具函数实现
```

### 4.2 从 main.cpp 入手

> 文件路径：`tts/tts_server/src/main.cpp`

让我们一段一段来读：

#### 全局变量和头文件

```cpp
#include "TTSModel.h"
#include "MessageQueue.h"
#include "AudioPlayer.h"
#include "TextProcessor.h"
#include "Utils.h"
#include "ZmqServer.h"      // 来自 zmq-comm-kit

#include <thread>
#include <iostream>
#include <atomic>
#include <memory>

// 全局变量
zmq_component::ZmqServer server("tcp://*:7777");        // 监听7777：接收LLM发来的文字
zmq_component::ZmqServer status_server("tcp://*:6677"); // 监听6677：与ASR模块通信状态
std::atomic<bool> first_msg(true);  // 标记：当前是否在等待一轮对话的第一条消息
```

**为什么有两个ZmqServer？**
- `server`（7777端口）：接收LLM发来的待合成文本
- `status_server`（6677端口）：通知ASR"我说完了，你可以开始听了"

#### main 函数

```cpp
int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <model_path>" << std::endl;
        return 1;
    }
    // 启动命令：./tts_server /path/to/model

    try {
        TTSModel model(argv[1]);        // 1. 加载TTS模型
        AudioPlayer player;             // 2. 初始化音频播放器（打开ALSA设备）
        DoubleMessageQueue queue;       // 3. 创建双缓冲队列

        // 4. 启动两个工作线程
        std::thread synthesis_thread(synthesis_worker, std::ref(queue), std::ref(model));
        std::thread playback_thread(playback_worker, std::ref(queue), std::ref(player));
        // std::ref() 用于传递引用给线程函数（线程默认会拷贝参数）

        // 5. 主线程：无限循环接收消息
        while (true) {
            if (first_msg) {
                // 等待ASR通知："用户说完了，轮到你了"
                std::string req = status_server.receive();
                std::cout << "[voice -> tts] received: " << req << std::endl;
            }
            first_msg = false;

            // 从LLM接收一段文字
            std::string text = server.receive();
            server.send("Echo: received");  // 回复LLM确认收到
            std::cout << "[llm -> tts] received: " << text << std::endl;

            // 过滤掉LLM的"思考过程"标签（DeepSeek会输出<think>...</think>）
            if (!text.empty() && text.find("<think>") == std::string::npos) {
                queue.push_text(text);  // 放入文本队列
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

### 4.3 三线程架构

```
┌──────────────────────────────────────────────────────────────────────────┐
│                        tts_server 进程                                   │
│                                                                          │
│  ┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐    │
│  │    主线程         │     │   合成线程        │     │   播放线程       │    │
│  │  (ZMQ接收)       │     │ (synthesis_worker)│     │ (playback_worker)│   │
│  │                  │     │                  │     │                  │    │
│  │ 1.等ASR通知      │     │ 1.从文本队列取文字 │     │ 1.从音频队列取数据│    │
│  │ 2.收LLM文字      │     │ 2.调用TTS推理     │     │ 2.送到ALSA播放   │    │
│  │ 3.放入文本队列   │──>──│ 3.音频放入音频队列│──>──│ 3.播完通知ASR    │    │
│  │                  │     │                  │     │                  │    │
│  └─────────────────┘     └─────────────────┘     └─────────────────┘    │
│           |                       |                       |              │
│      [文本队列]              [音频队列]                                    │
│     text_queue_             audio_queue_                                  │
└──────────────────────────────────────────────────────────────────────────┘
```

### 4.4 synthesis_worker —— 合成线程

```cpp
void synthesis_worker(DoubleMessageQueue &queue, TTSModel &model) {
    while (true) {
        std::string text = queue.pop_text();  // 阻塞等待文本
        if (text.empty()) break;              // 收到空字符串=停止信号

        // 检查是否包含"END"标记（表示LLM输出完毕）
        if (text.find("END") != std::string::npos) {
            first_msg = true;                 // 重置状态，准备下一轮对话
            size_t end_pos = text.find("END");
            text = text.substr(0, end_pos);   // 截掉"END"标记，保留前面的文字
        }

        int32_t audio_len = 0;
        if (!text.empty()) {
            std::cout << "[TTS infer] Inferring text: " << text << std::endl;
            // 核心：调用TTS模型推理，文字 → PCM音频数据
            int16_t* wavData = model.infer(text, audio_len);
            
            if (wavData && audio_len > 0) {
                // 拷贝音频数据到智能指针管理的内存中
                auto audio_data = std::make_unique<int16_t[]>(audio_len);
                memcpy(audio_data.get(), wavData, audio_len * sizeof(int16_t));
                // 放入音频队列
                queue.push_audio(std::move(audio_data), audio_len, first_msg);
                model.free_data(wavData);  // 释放模型返回的原始数据
            }
        } else {
            // 文本为空但不是停止信号，放入空音频作为"结束标记"
            auto empty_audio = std::make_unique<int16_t[]>(0);
            queue.push_audio(std::move(empty_audio), 0, first_msg);
        }
    }
}
```

**关键点**：
- `model.infer(text, audio_len)` 返回的是模型内部的缓冲区指针，所以需要拷贝一份再放入队列
- `std::move` 转移所有权：避免不必要的拷贝，音频数据可能很大

### 4.5 playback_worker —— 播放线程

```cpp
void playback_worker(DoubleMessageQueue &queue, AudioPlayer &player) {
    while (true) {
        auto msg = queue.pop_audio();  // 阻塞等待音频数据
        if (msg.data == nullptr) break;
        
        // 播放音频
        player.play(msg.data.get(), msg.length * sizeof(int16_t), 1.0f);
        
        // 如果这是最后一段音频，通知ASR可以开始听了
        if (msg.is_last) {
            status_server.send("[tts -> voice]play end success");
        }
    }
}
```

### 4.6 DoubleMessageQueue —— 双缓冲队列的实现

> 文件路径：`tts/tts_server/src/MessageQueue.cpp`

这是整个多线程架构的核心——线程安全的生产者-消费者队列。

```cpp
void DoubleMessageQueue::push_text(const std::string &msg) {
    {
        std::lock_guard<std::mutex> lock(text_mutex_);  // 自动上锁
        text_queue_.push(msg);                          // 放入队列
    }                                                   // 自动解锁
    text_cond_.notify_one();  // 通知等待的消费者："有新数据了！"
}

std::string DoubleMessageQueue::pop_text() {
    std::unique_lock<std::mutex> lock(text_mutex_);
    // wait的意思：如果队列为空且没有停止，就释放锁并睡觉
    // 被notify_one唤醒后，重新获取锁，再检查条件
    text_cond_.wait(lock, [this] { return !text_queue_.empty() || stop_; });

    if (stop_) return "";  // 停止信号

    std::string msg = std::move(text_queue_.front());
    text_queue_.pop();
    return msg;
}
```

**为什么叫"双缓冲"？** 因为有两个独立的队列：
1. `text_queue_`（文本队列）：主线程→合成线程
2. `audio_queue_`（音频队列）：合成线程→播放线程

每个队列有独立的mutex和condition_variable，互不干扰。

### 4.7 TTSModel —— TTS模型封装

> 文件路径：`tts/tts_server/src/TTSModel.cpp`

```cpp
TTSModel::TTSModel(const std::string &model_path) {
    load_model(model_path);
}

bool TTSModel::load_model(const std::string &model_path) {
    // 从文件加载模型权重到内存
    std::vector<char> model_path_copy(model_path.begin(), model_path.end());
    model_path_copy.push_back('\0');

    modelSize_ = ttsLoadModel(model_path_copy.data(), &dataW_);
    // dataW_ 现在指向加载到内存中的模型权重数据
    
    if (modelSize_ <= 0 || !dataW_) return false;
    
    // 用模型权重创建合成器
    synthesizer_ = std::make_unique<SynthesizerTrn>(dataW_, modelSize_);
    return true;
}

int16_t* TTSModel::infer(const std::string &text, int32_t &audio_len) {
    if (!synthesizer_) return nullptr;
    // 文字 → 音素 → 神经网络推理 → PCM波形
    return synthesizer_->infer(text, 0, 1.0, audio_len);
    // 参数：文本, 说话人ID, 语速, 输出音频长度
}
```

**TTS推理内部流程**（`synthesizer_->infer`内部，不在tts_server目录中）：
```
"你好世界" 
  → 汉字转拼音（Hanz2Piny）
  → 拼音转音素ID（hanzi2phoneid）
  → 文本编码器（TextEncoder，基于Eigen的矩阵运算）
  → 时长预测器（DurationPredictor）
  → 声码器（Generator，HiFi-GAN变体）
  → PCM音频采样数据（16kHz, 16bit, 单声道）
```

### 4.8 AudioPlayer —— ALSA音频播放

> 文件路径：`tts/tts_server/src/AudioPlayer.cpp`

```cpp
AudioPlayer::AudioPlayer() {
    initialize();
}

bool AudioPlayer::initialize() {
    if (initialized_) return true;
    // 打开默认ALSA播放设备
    int err = snd_pcm_open(&pcm_handle_, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cerr << "ALSA Error: Cannot open PCM device: " << snd_strerror(err) << std::endl;
        return false;
    }
    initialized_ = true;
    return true;
}

void AudioPlayer::play(const int16_t* audioData, int audio_len, float speed) {
    if (!initialized_ || !pcm_handle_) return;

    // 设置ALSA参数：采样格式S16LE, 单声道, 16kHz采样率
    unsigned int sample_rate = static_cast<unsigned int>(16000 * speed);
    int err = snd_pcm_set_params(pcm_handle_,
                                 SND_PCM_FORMAT_S16_LE,      // 16位有符号小端
                                 SND_PCM_ACCESS_RW_INTERLEAVED, // 交错模式
                                 1,                           // 1个声道（单声道）
                                 sample_rate,                 // 采样率
                                 1,                           // 允许软件重采样
                                 50000);                      // 延迟50ms

    // 将PCM数据写入ALSA设备（实际驱动扬声器发声）
    const snd_pcm_uframes_t frames = audio_len / 2;
    err = snd_pcm_writei(pcm_handle_, audioData, frames);
    
    if (err == -EPIPE) {        // underrun：播放缓冲区空了
        snd_pcm_prepare(pcm_handle_);  // 重新准备设备
    }
    
    // 等待所有数据播放完毕
    if (snd_pcm_state(pcm_handle_) == SND_PCM_STATE_RUNNING) {
        snd_pcm_drain(pcm_handle_);
    }
}
```

**ALSA 是什么？** Advanced Linux Sound Architecture，Linux的音频子系统。你可以把它想象成Linux操作系统内置的"声卡驱动接口"。`snd_pcm_writei()` 就是"把数字音频数据送到声卡，让扬声器发出声音"。

### 4.9 tts_server 总结

```
启动流程：
1. 加载TTS模型到内存
2. 打开ALSA音频设备
3. 创建双缓冲队列
4. 启动合成线程和播放线程
5. 主线程进入消息接收循环

数据流：
LLM(ZMQ:7777) → 主线程 → [文本队列] → 合成线程 → [音频队列] → 播放线程 → 扬声器
                                                                          ↓
                                                              ASR(ZMQ:6677) ← 播放完成通知
```

---

## 阶段五：读 sherpa-onnx 核心 —— 理解大型C++项目的架构设计

### 5.1 目录结构与分层逻辑

```
voice/sherpa-onnx/
├── CMakeLists.txt                    # 项目总构建文件
├── sherpa-onnx/                      # 核心代码目录
│   ├── csrc/                         # ★ C++核心实现（所有AI能力的源码）
│   │   ├── online-recognizer*.h/cc   #   在线流式识别
│   │   ├── offline-recognizer*.h/cc  #   离线识别
│   │   ├── online-stream.h/cc        #   音频数据流
│   │   ├── features.h/cc             #   特征提取
│   │   ├── rknn/                     #   RKNN NPU加速支持
│   │   └── ...                       #   几百个源文件
│   ├── c-api/                        # C语言API封装
│   │   ├── c-api.h                   #   C接口声明
│   │   ├── c-api.cc                  #   C接口实现
│   │   ├── cxx-api.h                 #   现代C++接口
│   │   └── cxx-api.cc
│   ├── python/                       # Python绑定
│   ├── jni/                          # Java/Android绑定
│   └── java-api/                     # Java API
├── c-api-examples/                   # C API使用示例
├── cxx-api-examples/                 # C++ API使用示例
└── cmake/                            # CMake辅助模块
```

**分层设计思想**：

```
最顶层：各语言绑定（Python/Java/Dart/Go...）
  ↓ 调用
中间层：C API（c-api/c-api.h） ← 所有语言绑定都通过它
  ↓ 调用  
核心层：C++实现（csrc/）← 真正的算法和逻辑
  ↓ 调用
底层：ONNX Runtime / RKNN ← 实际的AI推理
```

**为什么要分这么多层？**
- `csrc/` 用C++写核心逻辑，发挥C++的性能优势
- `c-api/` 导出C接口，因为C接口是"万能胶水"——几乎所有语言都能调用C函数
- 上层各种语言绑定（Python/Java/Go/...）都通过C API来调用核心功能

### 5.2 以"在线流式语音识别"为主线走一遍代码路径

**什么是"在线流式识别"？** "在线"不是指联网，而是指"实时的"——音频一边录一边识别，不需要录完再处理。

#### 第一站：OnlineRecognizer —— 对外门面

> 文件路径：`voice/sherpa-onnx/sherpa-onnx/csrc/online-recognizer.h`

```cpp
class OnlineRecognizer {
 public:
  explicit OnlineRecognizer(const OnlineRecognizerConfig &config);
  
  std::unique_ptr<OnlineStream> CreateStream() const;  // 创建音频数据流
  bool IsReady(OnlineStream *s) const;                  // 是否有足够数据可以解码
  void DecodeStream(OnlineStream *s) const;             // 解码单个流
  void DecodeStreams(OnlineStream **ss, int32_t n) const; // 批量解码
  OnlineRecognizerResult GetResult(OnlineStream *s) const; // 获取识别结果
  bool IsEndpoint(OnlineStream *s) const;               // 是否检测到端点（说话结束）
  void Reset(OnlineStream *s) const;                    // 重置流状态

 private:
  std::unique_ptr<OnlineRecognizerImpl> impl_;  // ← 真正干活的在这里
};
```

**这是"门面模式"（Facade Pattern）**：`OnlineRecognizer` 对外提供简洁的接口，内部复杂逻辑全部委托给 `impl_`。

#### 第二站：OnlineRecognizerImpl —— 工厂模式 + 策略模式

> 文件路径：`voice/sherpa-onnx/sherpa-onnx/csrc/online-recognizer-impl.h`

```cpp
class OnlineRecognizerImpl {
 public:
  // ★ 工厂方法：根据配置创建正确的实现类
  static std::unique_ptr<OnlineRecognizerImpl> Create(
      const OnlineRecognizerConfig &config);

  // 纯虚函数 —— 定义接口，不提供实现
  virtual std::unique_ptr<OnlineStream> CreateStream() const = 0;
  virtual bool IsReady(OnlineStream *s) const = 0;
  virtual void DecodeStreams(OnlineStream **ss, int32_t n) const = 0;
  virtual OnlineRecognizerResult GetResult(OnlineStream *s) const = 0;
  virtual bool IsEndpoint(OnlineStream *s) const = 0;
  virtual void Reset(OnlineStream *s) const = 0;
};
```

**工厂模式在这里的作用**：

```cpp
// 在 online-recognizer-impl.cc 中（简化）：
std::unique_ptr<OnlineRecognizerImpl> OnlineRecognizerImpl::Create(
    const OnlineRecognizerConfig &config) {
  // 根据模型类型，创建不同的实现类
  if (model_type == "transducer") {
    return std::make_unique<OnlineRecognizerTransducerImpl>(config);
  } else if (model_type == "paraformer") {
    return std::make_unique<OnlineRecognizerParaformerImpl>(config);
  } else if (model_type == "ctc") {
    return std::make_unique<OnlineRecognizerCtcImpl>(config);
  }
  // ...
}
```

**这就是策略模式**：同样的接口（识别语音），不同的策略（Transducer/Paraformer/CTC）。用户只需要改配置，不需要改代码，系统自动选择合适的算法。

**对应的实现类文件**：

| 文件 | 模型类型 | 特点 |
|------|---------|------|
| `online-recognizer-transducer-impl.h` | Transducer (RNN-T) | 最常用的流式模型 |
| `online-recognizer-paraformer-impl.h` | Paraformer | 阿里达摩院的模型 |
| `online-recognizer-ctc-impl.h` | CTC | 基于连接时序分类的模型 |
| `rknn/online-recognizer-transducer-rknn-impl.h` | Transducer on RKNN | NPU加速版本 |
| `rknn/online-recognizer-ctc-rknn-impl.h` | CTC on RKNN | NPU加速版本 |

#### 第三站：使用流程（伪代码）

```cpp
// 1. 配置
OnlineRecognizerConfig config;
config.model_config.transducer.encoder = "encoder.onnx";
config.model_config.transducer.decoder = "decoder.onnx";
config.model_config.transducer.joiner  = "joiner.onnx";
config.model_config.tokens = "tokens.txt";

// 2. 创建识别器（内部会通过工厂方法创建正确的Impl）
OnlineRecognizer recognizer(config);

// 3. 创建音频流
auto stream = recognizer.CreateStream();

// 4. 不断喂入音频数据
while (has_more_audio) {
    stream->AcceptWaveform(sample_rate, audio_samples, num_samples);
    
    while (recognizer.IsReady(stream.get())) {
        recognizer.DecodeStream(stream.get());
    }
    
    auto result = recognizer.GetResult(stream.get());
    std::cout << result.text;  // 输出当前识别到的文字
    
    if (recognizer.IsEndpoint(stream.get())) {
        recognizer.Reset(stream.get());  // 检测到说话结束，重置
    }
}
```

### 5.3 C API 的设计

> 文件路径：`voice/sherpa-onnx/sherpa-onnx/c-api/c-api.h`

```cpp
#ifdef __cplusplus
extern "C" {            // 告诉C++编译器：以下函数按C的方式导出
#endif

// SHERPA_ONNX_API 宏的作用：控制符号可见性
// Linux上展开为：__attribute__((visibility("default")))
// 意思是"这个函数可以被外部使用"
// Windows上展开为：__declspec(dllexport) 或 __declspec(dllimport)
#define SHERPA_ONNX_API __attribute__((visibility("default")))

// C风格的结构体（没有方法，只有数据）
SHERPA_ONNX_API typedef struct SherpaOnnxOnlineTransducerModelConfig {
  const char *encoder;     // 编码器模型路径
  const char *decoder;     // 解码器模型路径
  const char *joiner;      // 联合器模型路径
} SherpaOnnxOnlineTransducerModelConfig;

// C风格的函数接口
SHERPA_ONNX_API const SherpaOnnxOnlineRecognizer *SherpaOnnxCreateOnlineRecognizer(
    const SherpaOnnxOnlineRecognizerConfig *config);

SHERPA_ONNX_API void SherpaOnnxDestroyOnlineRecognizer(
    const SherpaOnnxOnlineRecognizer *recognizer);

#ifdef __cplusplus
}                       // extern "C" 结束
#endif
```

**为什么要导出C接口？**

1. **跨语言兼容**：C的ABI（应用二进制接口）是稳定的。Python的ctypes、Java的JNI、Go的cgo都能直接调用C函数
2. **C++的名字修饰（name mangling）问题**：C++编译器会把函数名改成类似`_ZN10sherpa_onnx16OnlineRecognizer11DecodeStreamsEPPS0_i`这样的乱码，其他语言无法调用。而C函数名保持原样
3. **二进制兼容性**：不同C++编译器的ABI可能不兼容，C接口则没有这个问题

### 5.4 sherpa-onnx 架构总结

**设计模式清单**：

| 设计模式 | 在哪里用 | 为什么用 |
|---------|---------|---------|
| 工厂模式 | `OnlineRecognizerImpl::Create()` | 根据配置自动创建正确的模型实现 |
| 策略模式 | 多个`*Impl`子类 | 同一接口，不同算法（Transducer/CTC/Paraformer） |
| 门面模式 | `OnlineRecognizer` | 隐藏内部复杂性，对外暴露简洁接口 |
| PImpl模式 | `impl_`指针 | 头文件不暴露实现细节，加快编译速度 |
| RAII | 到处都有 | 智能指针管理模型、Session等资源的生命周期 |

---

## 阶段六：读 LLM 模块 —— 理解 NPU 推理和模块串联

### 6.1 RKNN LLM 的基本原理

**NPU 是什么？** Neural Processing Unit（神经网络处理单元），是专门为AI计算设计的芯片。

类比：
- **CPU** = 通才，什么都能做，但做矩阵运算不够快
- **GPU** = 擅长并行计算的"工厂"，适合大批量矩阵运算，但功耗高
- **NPU** = 专门做AI推理的"专才"，功耗低、速度快，但只能做AI相关的运算

**RK3576** 是瑞芯微（Rockchip）的一款SoC芯片，内置了NPU。RKNN（Rockchip Neural Network）是瑞芯微提供的NPU工具链，让你可以把AI模型部署到他们的NPU上运行。

在本项目中：
- ASR 模型通过 ONNX Runtime 运行（也可以通过RKNN加速）
- LLM（DeepSeek）通过 RKNN 跑在 NPU 上
- TTS 模型用 Eigen 在 CPU 上运行

### 6.2 llm_test.cpp 代码讲解

> 文件路径：`llm/test/llm_test.cpp`

这个文件展示了LLM模块如何接收ASR的文字并将回复发送给TTS。

```cpp
// 创建ZMQ通信对象
zmq_component::ZmqServer server;  // 默认监听6666端口，等待ASR发来的文字
zmq_component::ZmqClient tts_client_("tcp://localhost:7777");  // 连接TTS的7777端口
```

**LLM模块同时扮演两个角色**：
- 对ASR来说，它是**服务端**（监听6666，等ASR发消息来）
- 对TTS来说，它是**客户端**（主动连接TTS的7777端口发消息）

#### 消息处理函数

```cpp
void message_worker(const std::string &rag_text) {
    // 正则表达式：匹配中文标点作为分隔符
    static const std::wregex wide_delimiter(
        L"([。！？；：\n]|\\?\\s|\\!\\s|\\；|\\，|\\、|\\|)");
    const std::wstring END_MARKER = L"END";

    // UTF-8 → wchar_t 宽字符转换（处理中文需要）
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::wstring wide_text = converter.from_bytes(rag_text) + END_MARKER;
    // 在文本末尾加上"END"标记，告诉TTS这轮回答结束了

    // 用标点分割文本
    std::wsregex_iterator it(wide_text.begin(), wide_text.end(), wide_delimiter);
    std::wsregex_iterator end;

    // 跳过前两个分隔符（避免发送太短的片段）
    int skip_counter = 0;
    size_t last_pos = 0;
    while (it != end && skip_counter < 2) {
        last_pos = it->position() + it->length();
        ++it;
        ++skip_counter;
    }

    // 每找到一个标点，就把这一段文字发给TTS
    while (it != end) {
        size_t seg_start = last_pos;
        size_t seg_end = it->position();
        last_pos = seg_end + it->length();

        std::wstring wide_segment = wide_text.substr(seg_start, seg_end - seg_start);

        // 去除首尾空白
        wide_segment.erase(0, wide_segment.find_first_not_of(L" \t\n\r"));
        wide_segment.erase(wide_segment.find_last_not_of(L" \t\n\r") + 1);

        if (!wide_segment.empty()) {
            // 发送给TTS并等待确认
            auto response1 = tts_client_.request(converter.to_bytes(wide_segment));
            std::cout << "[tts -> llm] received: " << response1 << std::endl;
        }
        ++it;
    }

    // 处理最后剩余的文本（包含END标记）
    if (last_pos < wide_text.length()) {
        std::wstring last_segment = wide_text.substr(last_pos);
        if (!last_segment.empty()) {
            auto response1 = tts_client_.request(converter.to_bytes(last_segment));
        }
    }
}
```

**为什么要按标点分割？** 因为TTS一次合成一整段话会很慢。按标点分割成短句，可以实现"边想边说"——LLM想到一句，TTS就说一句，用户不需要等全部想完。

#### 主循环

```cpp
void receive_asr_data_and_process() {
    while (true) {
        // 等待ASR发来的语音识别结果
        std::string input_str = server.receive();
        std::cout << "[voice -> llm] received: " << input_str << std::endl;
        server.send("llm sucess reply !!!");  // 回复ASR确认收到

        // 处理并转发给TTS
        // （实际项目中，这里会先调用RKNN LLM推理生成回复，再发给TTS）
        message_worker(input_str);
    }
}
```

注意：`llm_test.cpp` 是一个**测试/演示文件**，它直接把ASR的文字转发给TTS（没有经过LLM推理）。在完整的系统中，这里会调用RKNN LLM API进行推理。

### 6.3 完整的端到端流程（所有模块串联）

```
用户说话 → [麦克风]
               ↓
         ┌─────────────────────────────────────┐
         │  ASR模块 (sherpa-onnx)               │
         │  1. 音频流 → 特征提取(FBank)          │
         │  2. ONNX模型推理 → 解码              │
         │  3. 端点检测 → 确认用户说完           │
         │  4. 输出识别文字                      │
         └──────────┬──────────────────────────┘
                    │ ZMQ tcp://6666
                    │ "今天天气怎么样"
                    ↓
         ┌─────────────────────────────────────┐
         │  LLM模块 (rknn-llm)                  │
         │  1. ZmqServer接收文字                │
         │  2. 构造prompt → 送入NPU推理          │
         │  3. 流式输出，按标点分句               │
         │  4. 每句通过ZmqClient发给TTS          │
         └──────────┬──────────────────────────┘
                    │ ZMQ tcp://7777
                    │ "今天是晴天" → "温度大概25度END"
                    ↓
         ┌─────────────────────────────────────┐
         │  TTS模块 (tts_server)                │
         │  主线程：ZMQ接收 → 文本队列           │
         │  合成线程：文本 → 拼音 → 模型推理 → PCM│
         │  播放线程：PCM → ALSA → 扬声器        │
         └──────────┬──────────────────────────┘
                    │ ZMQ tcp://6677
                    │ "play end success"
                    ↓
         ┌─────────────────────────────────────┐
         │  ASR模块                              │
         │  收到播放完成通知 → 重新开始监听麦克风   │
         └─────────────────────────────────────┘
               ↓
         等待用户下一句话... (循环)
```

**整个系统的通信端口总结**：

| 端口 | 协议 | 方向 | 内容 |
|------|------|------|------|
| 6666 | ZMQ REQ/REP | ASR → LLM | 语音识别的文字结果 |
| 7777 | ZMQ REQ/REP | LLM → TTS | 待合成的文字（按句分割） |
| 6677 | ZMQ REQ/REP | TTS ↔ ASR | 播放状态通知 |

---

## 阶段七：回顾与提炼 —— 为简历和面试做准备

### 7.1 简历技术亮点提炼

**项目名称**：基于DeepSeek与RK3576的模块化离线智能语音交互系统

**建议的简历描述**：

> - 设计并实现了**全离线、模块化**的端侧智能语音交互系统，集成流式ASR、DeepSeek LLM推理、TTS语音合成三大模块，在RK3576 NPU上实现**4秒内**的语音输入→LLM思考→语音输出闭环
> - 基于**ZeroMQ REQ/REP模式**实现模块间松耦合通信，封装了zmq-comm-kit通信库，支持跨进程消息传递
> - 设计**双缓冲生产者-消费者架构**的TTS服务，采用三线程流水线（接收/合成/播放），结合mutex+condition_variable实现线程安全的数据传递
> - 基于sherpa-onnx框架实现流式语音识别，理解其**工厂模式+策略模式**的架构设计，支持Transducer/CTC/Paraformer等多种ASR模型热切换
> - 利用**RKNN NPU**加速DeepSeek大模型推理，实现LLM在嵌入式设备上的端侧部署
> - 采用**流式处理+分句并行**策略，LLM按标点分句、TTS边合成边播放，显著降低用户感知延迟

### 7.2 面试高频问题与回答思路

#### Q1：请介绍一下这个项目的整体架构？

**回答思路**：
- 先说目标：在无网络的嵌入式设备上实现完整的语音交互闭环
- 再说架构：三个核心模块（ASR/LLM/TTS）+ 一个通信层（zmq-comm-kit），各自独立进程，通过ZeroMQ松耦合通信
- 最后说数据流：语音→ASR→文字→LLM→回答→TTS→语音播放

#### Q2：为什么选择 ZeroMQ 而不是其他通信方式？

**回答思路**：
- 对比直接socket：ZMQ封装了连接管理、消息分帧、自动重连等底层逻辑，开发效率高
- 对比gRPC/HTTP：嵌入式设备资源有限，ZMQ是轻量级的消息库，零依赖、高性能
- 对比共享内存：ZMQ支持多种传输层（tcp/ipc/inproc），模块可以灵活部署在同一机器或不同机器上
- 松耦合优势：模块可以独立开发、测试、替换，比如换一个TTS引擎，只要监听同一端口就行

#### Q3：TTS 模块的多线程架构是怎么设计的？为什么要这样设计？

**回答思路**：
- 三线程流水线：主线程（接收消息）、合成线程（模型推理）、播放线程（音频输出）
- 双缓冲队列：文本队列 + 音频队列，每个队列有独立的mutex和condition_variable
- 为什么这样设计：TTS推理耗时较长（百毫秒级），如果串行执行，播放完一句才能合成下一句。流水线设计让合成和播放并行，合成第N+1句的同时播放第N句
- condition_variable的作用：当队列为空时，消费者线程不是忙等待（busy-wait），而是睡眠等待，节省CPU

#### Q4：sherpa-onnx 用了哪些设计模式？为什么？

**回答思路**：
- 工厂模式（`OnlineRecognizerImpl::Create`）：根据配置自动创建对应的模型实现。好处是新增模型类型时不需要修改调用方代码
- 策略模式（多个Impl子类）：Transducer/CTC/Paraformer等不同ASR算法共享同一接口。好处是可以在运行时通过配置切换算法
- PImpl模式（`std::unique_ptr<OnlineRecognizerImpl> impl_`）：头文件不暴露实现细节，减少编译依赖，加快编译速度
- 门面模式（`OnlineRecognizer`）：内部有复杂的模型加载、特征提取、解码等流程，对外只暴露简单的`CreateStream/DecodeStream/GetResult`

#### Q5：为什么要导出 C API？直接用 C++ 接口不行吗？

**回答思路**：
- C++ 的 ABI 不稳定：不同编译器（gcc/clang/msvc）、不同版本的编译器，编译出的C++符号可能不兼容
- C++ 的名字修饰（name mangling）：编译后函数名变成了人类不可读的符号
- C 的 ABI 是标准化的：几乎所有编程语言都提供了调用C函数的机制（Python的ctypes、Java的JNI、Go的cgo、Dart的FFI等）
- 所以 sherpa-onnx 用 C++ 实现核心逻辑，通过 C API 导出，上层才能支持这么多语言绑定

#### Q6：在嵌入式设备上部署AI模型有什么挑战？你们怎么解决的？

**回答思路**：
- **内存受限**：模型不能太大。解决方案：模型量化（8bit/4bit）减小体积
- **算力受限**：CPU做推理太慢。解决方案：利用NPU硬件加速（RKNN）
- **延迟要求高**：用户说完到系统回复不能太久。解决方案：流式处理（ASR边听边转写，LLM边想边说，TTS边合成边播放）
- **离线部署**：没有网络，所有模型必须在本地运行。解决方案：选择轻量级模型（DeepSeek-R1-Distill-Qwen-1.5B）

### 7.3 项目中的设计取舍（Trade-off）

#### Trade-off 1：进程间通信 vs 线程间通信

- **选择**：模块间用独立进程 + ZMQ通信（而不是同一进程内的多线程）
- **好处**：
  - 松耦合：一个模块崩溃不会影响其他模块
  - 可独立开发和测试
  - 可以灵活分配到不同的处理器核心
- **代价**：
  - 进程间通信有序列化/反序列化开销
  - 比共享内存慢
  - 需要额外的消息协议约定

#### Trade-off 2：TTS 用 Eigen 手写推理 vs 用 ONNX Runtime

- **选择**：SummerTTS 用 Eigen 手动实现前向传播
- **好处**：
  - 不依赖 ONNX Runtime，减少依赖
  - 可以精细控制每一步计算，更容易在嵌入式设备上优化
  - 编译产物更小
- **代价**：
  - 开发工作量大（需要手写每一层神经网络的计算）
  - 不如 ONNX Runtime 通用（换模型需要改代码）
  - 难以利用硬件加速（ONNX Runtime可以自动选择CPU/GPU/NPU）

#### Trade-off 3：REQ/REP 模式 vs PUB/SUB 模式

- **选择**：使用 ZMQ 的 REQ/REP（请求-响应）模式
- **好处**：
  - 有确认机制：发送方知道对方收到了
  - 流程清晰：一问一答，易于调试
- **代价**：
  - 同步阻塞：发送方必须等响应才能发下一条
  - 不支持广播：如果将来要添加多个TTS实例，架构需要调整
  - PUB/SUB模式更适合"一对多"的场景

#### Trade-off 4：流式 vs 非流式处理

- **选择**：全链路流式处理（ASR流式识别、LLM流式输出、TTS分句合成播放）
- **好处**：
  - 大幅降低首字延迟（First Token Latency）
  - 用户体验好：系统"秒回"而不是沉默很久
- **代价**：
  - 系统复杂度增加：需要处理部分结果、END标记、状态同步等
  - 可能的错误累积：ASR中间结果可能不准确
  - 多线程同步增加了调试难度

---

## 附录：项目关键文件速查表

| 文件路径 | 作用 |
|---------|------|
| `zmq-comm-kit/include/ZmqServer.h` | ZMQ服务端接口定义 |
| `zmq-comm-kit/include/ZmqClient.h` | ZMQ客户端接口定义 |
| `zmq-comm-kit/src/ZmqInterface.cpp` | ZMQ通信基类实现 |
| `tts/tts_server/src/main.cpp` | TTS服务主程序入口 |
| `tts/tts_server/src/MessageQueue.cpp` | 双缓冲队列实现 |
| `tts/tts_server/src/TTSModel.cpp` | TTS模型封装 |
| `tts/tts_server/src/AudioPlayer.cpp` | ALSA音频播放 |
| `tts/CMakeLists.txt` | TTS服务构建配置 |
| `voice/sherpa-onnx/sherpa-onnx/csrc/online-recognizer.h` | 在线识别器接口 |
| `voice/sherpa-onnx/sherpa-onnx/csrc/online-recognizer-impl.h` | 识别器工厂/策略模式 |
| `voice/sherpa-onnx/sherpa-onnx/c-api/c-api.h` | C API接口定义 |
| `llm/test/llm_test.cpp` | LLM模块测试/演示 |
| `llm/rknn-llm/rkllm-runtime/Linux/librkllm_api/include/rkllm.h` | RKNN LLM运行时API |

---

> 本教程基于项目实际代码编写，所有代码引用均指向仓库中的真实文件。
> 建议配合 IDE 打开对应文件边读边学，效果更佳。
