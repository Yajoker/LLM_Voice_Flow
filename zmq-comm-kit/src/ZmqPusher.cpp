#include "ZmqPusher.h"

namespace zmq_component {

ZmqPusher::ZmqPusher(const std::string& address)
{
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUSH);
        socket_->set(zmq::sockopt::sndtimeo, timeout_ms_);
        socket_->connect(address);
    } catch (const zmq::error_t& e) {
        throw ZmqCommunicationError(e.what());
    }
}

void ZmqPusher::push(const std::string& message)
{
    zmq::message_t msg(message.size());
    if (!message.empty()) {
        memcpy(msg.data(), message.data(), message.size());
    }
    if (!socket_->send(msg, zmq::send_flags::none)) {
        throw ZmqCommunicationError("Push timeout");
    }
}

}  // namespace zmq_component
