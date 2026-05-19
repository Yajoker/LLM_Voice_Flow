#pragma once
#include "ZmqInterface.h"

namespace zmq_component {

/** 单向拉取（TTS ← LLM），bind 等待 PUSH 连接 */
class ZmqPuller : public ZmqInterface {
public:
    explicit ZmqPuller(const std::string& address = "tcp://*:7777");
    std::string pull();
};

}  // namespace zmq_component
