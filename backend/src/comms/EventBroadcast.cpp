#include "comms/EventBroadcast.h"

namespace injector::comms {

void EventBroadcast::publish(state::InternalEvent event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(event));
    }
    cv_.notify_all();
}

std::vector<state::InternalEvent> EventBroadcast::waitForEvents(
    size_t& sequence, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timeout, [this, &sequence] {
        return events_.size() > sequence;
    });

    if (events_.size() <= sequence) {
        return {};
    }

    std::vector<state::InternalEvent> result(events_.begin() + static_cast<ptrdiff_t>(sequence),
                                             events_.end());
    sequence = events_.size();
    return result;
}

std::vector<state::InternalEvent> EventBroadcast::getEventsSince(size_t sequence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sequence >= events_.size()) {
        return {};
    }
    return {events_.begin() + static_cast<ptrdiff_t>(sequence), events_.end()};
}

size_t EventBroadcast::totalPublished() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

}  // namespace injector::comms
