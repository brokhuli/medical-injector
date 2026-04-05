#pragma once

#include "state/InjectorState.h"

#include <mutex>
#include <optional>
#include <queue>

namespace injector::comms {

/// Thread-safe FIFO queue for FaultEvent.
/// Producer: Safety Monitor thread. Consumer: State Machine thread.
class FaultQueue {
public:
    /// Enqueue a fault event (thread-safe, non-blocking).
    void push(state::FaultEvent fault);

    /// Dequeue the oldest fault event, or nullopt if empty (thread-safe, non-blocking).
    [[nodiscard]] std::optional<state::FaultEvent> pop();

    /// Returns true if no fault events are pending (thread-safe).
    [[nodiscard]] bool empty() const;

    /// Number of fault events pending (thread-safe).
    [[nodiscard]] size_t size() const;

private:
    mutable std::mutex mutex_;
    std::queue<state::FaultEvent> queue_;
};

}  // namespace injector::comms
