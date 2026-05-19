#include "ZmqPuller.h"

namespace zmq_component {

ZmqPuller::ZmqPuller(const std::string& address)
{
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PULL);
        socket_->set(zmq::sockopt::rcvtimeo, timeout_ms_);
        socket_->bind(address);
    } catch (const zmq::error_t& e) {
        throw ZmqCommunicationError(e.what());
    }
}

std::string ZmqPuller::pull()
{
    zmq::message_t request;
    if (!socket_->recv(request)) {
        throw ZmqCommunicationError("Pull timeout");
    }
    return {static_cast<char*>(request.data()), request.size()};
}

}  // namespace zmq_component
