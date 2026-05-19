#pragma once
#include "ZmqInterface.h"

namespace zmq_component {

/** 单向推送（LLM → TTS），connect 到对端 PULL */
class ZmqPusher : public ZmqInterface {
public:
    explicit ZmqPusher(const std::string& address = "tcp://localhost:7777");
    void push(const std::string& message);
};

}  // namespace zmq_component
