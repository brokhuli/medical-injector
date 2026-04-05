#include "comms/CommandQueue.h"

namespace injector::comms {

void CommandQueue::push(InternalCommand cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(cmd));
}

std::optional<InternalCommand> CommandQueue::pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    InternalCommand cmd = std::move(queue_.front());
    queue_.pop();
    return cmd;
}

bool CommandQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

size_t CommandQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

}  // namespace injector::comms
