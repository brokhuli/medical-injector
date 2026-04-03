#pragma once

#include "control/ControlLoop.h"  // TickData
#include "logging/RingBuffer.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace injector::logging {

struct SystemEvent {
    uint64_t timestampUs = 0;
    std::string category;  // "state", "fault", "command", "phase"
    std::string message;
};

class DataLogger {
public:
    explicit DataLogger(size_t ringBufferSize = 300000,
                        size_t maxEvents = 10000);

    /// Get the tick ring buffer (passed to ControlLoop for writing).
    [[nodiscard]] RingBuffer<control::TickData>& tickBuffer();

    /// Log a system event (thread-safe).
    void logEvent(const std::string& category, const std::string& message);

    /// Get all events (thread-safe copy).
    [[nodiscard]] std::vector<SystemEvent> events() const;

    /// Export tick data as CSV string.
    [[nodiscard]] std::string exportCsv() const;

    /// Export tick data as JSON string.
    [[nodiscard]] std::string exportJson() const;

    /// Number of events logged.
    [[nodiscard]] size_t eventCount() const;

private:
    RingBuffer<control::TickData> tickBuffer_;
    mutable std::mutex eventMutex_;
    std::vector<SystemEvent> events_;
    size_t maxEvents_;
    std::chrono::steady_clock::time_point startTime_;
};

}  // namespace injector::logging
