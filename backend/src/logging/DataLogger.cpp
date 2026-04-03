#include "logging/DataLogger.h"

#include <spdlog/spdlog.h>

#include <sstream>

namespace injector::logging {

DataLogger::DataLogger(size_t ringBufferSize, size_t maxEvents)
    : tickBuffer_(ringBufferSize),
      maxEvents_(maxEvents),
      startTime_(std::chrono::steady_clock::now()) {}

RingBuffer<control::TickData>& DataLogger::tickBuffer() {
    return tickBuffer_;
}

void DataLogger::logEvent(const std::string& category,
                          const std::string& message) {
    auto now = std::chrono::steady_clock::now();
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - startTime_)
            .count();

    std::lock_guard<std::mutex> lock(eventMutex_);
    if (events_.size() >= maxEvents_) {
        spdlog::warn("Event log full ({} events), dropping oldest", maxEvents_);
        events_.erase(events_.begin());
    }
    events_.push_back(
        {static_cast<uint64_t>(us), category, message});
}

std::vector<SystemEvent> DataLogger::events() const {
    std::lock_guard<std::mutex> lock(eventMutex_);
    return events_;
}

size_t DataLogger::eventCount() const {
    std::lock_guard<std::mutex> lock(eventMutex_);
    return events_.size();
}

std::string DataLogger::exportCsv() const {
    auto data = tickBuffer_.snapshot();

    std::ostringstream oss;
    oss << "timestamp_us,target_flow_rate,actual_flow_rate,pressure,"
           "motor_rpm_commanded,motor_rpm_actual,contrast_valve,saline_valve,"
           "state,phase_index,contrast_remaining,saline_remaining\n";

    for (const auto& td : data) {
        oss << td.timestampUs << ","
            << td.targetFlowRate << ","
            << td.actualFlowRate << ","
            << td.pressure << ","
            << td.motorRpmCommanded << ","
            << td.motorRpmActual << ","
            << static_cast<int>(td.contrastValve) << ","
            << static_cast<int>(td.salineValve) << ","
            << static_cast<int>(td.state) << ","
            << static_cast<int>(td.phaseIndex) << ","
            << td.contrastRemaining << ","
            << td.salineRemaining << "\n";
    }

    return oss.str();
}

std::string DataLogger::exportJson() const {
    auto data = tickBuffer_.snapshot();

    std::ostringstream oss;
    oss << "[\n";
    for (size_t i = 0; i < data.size(); ++i) {
        const auto& td = data[i];
        oss << "  {"
            << "\"timestamp_us\":" << td.timestampUs
            << ",\"target_flow_rate\":" << td.targetFlowRate
            << ",\"actual_flow_rate\":" << td.actualFlowRate
            << ",\"pressure\":" << td.pressure
            << ",\"motor_rpm_commanded\":" << td.motorRpmCommanded
            << ",\"motor_rpm_actual\":" << td.motorRpmActual
            << ",\"contrast_valve\":" << static_cast<int>(td.contrastValve)
            << ",\"saline_valve\":" << static_cast<int>(td.salineValve)
            << ",\"state\":" << static_cast<int>(td.state)
            << ",\"phase_index\":" << static_cast<int>(td.phaseIndex)
            << ",\"contrast_remaining\":" << td.contrastRemaining
            << ",\"saline_remaining\":" << td.salineRemaining
            << "}";
        if (i + 1 < data.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "]\n";

    return oss.str();
}

}  // namespace injector::logging
