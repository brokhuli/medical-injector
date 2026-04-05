#include "comms/FaultQueue.h"

namespace injector::comms {

void FaultQueue::push(state::FaultEvent fault) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(fault));
}

std::optional<state::FaultEvent> FaultQueue::pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    state::FaultEvent f = std::move(queue_.front());
    queue_.pop();
    return f;
}

bool FaultQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

size_t FaultQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

}  // namespace injector::comms
