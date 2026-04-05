#pragma once

#include "state/InjectorState.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace injector::comms {

/// Broadcasts InternalEvents from the State Machine / Safety Monitor to gRPC StreamEvents.
/// Publishers call publish(); the gRPC handler waits on waitForEvents() and streams immediately.
class EventBroadcast {
public:
    /// Publish an event to all waiting subscribers (thread-safe).
    void publish(state::InternalEvent event);

    /// Block until new events are available since `sequence`, or until timeout.
    /// On return, `sequence` is updated to the new total count.
    /// Returns all new events (may be empty on timeout).
    [[nodiscard]] std::vector<state::InternalEvent> waitForEvents(
        size_t& sequence,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(100));

    /// Get all events published since `sequence` without blocking.
    [[nodiscard]] std::vector<state::InternalEvent> getEventsSince(size_t sequence) const;

    /// Total number of events published so far.
    [[nodiscard]] size_t totalPublished() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<state::InternalEvent> events_;
};

}  // namespace injector::comms
