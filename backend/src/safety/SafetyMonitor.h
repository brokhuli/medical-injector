#pragma once

#include "comms/FaultQueue.h"
#include "config/Config.h"
#include "hal/IHalInterface.h"
#include "state/InjectorState.h"
#include "state/Protocol.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace injector::safety {

/// Independent safety watchdog. Runs at 1 ms tick (configurable), reads HAL directly,
/// and is architecturally independent from the control loop — it still works even if
/// the control loop hangs. On any fault: calls hal->emergencyStop() first, then pushes
/// a FaultEvent to the fault queue for the state machine to process.
class SafetyMonitor {
public:
    SafetyMonitor(hal::IHalInterface* hal,
                  const state::ControlTargets* targets,
                  comms::FaultQueue* faultQueue,
                  const config::SafetyConfig& cfg);

    ~SafetyMonitor();

    /// Perform one safety check synchronously (for unit tests — no thread needed).
    /// @param nowUs  Current time in microseconds since steady_clock epoch.
    /// @returns      All faults detected this tick. Empty = no fault.
    [[nodiscard]] std::vector<state::FaultEvent> checkOnce(uint64_t nowUs);

    /// Reset internal counters (motor divergence streak, etc.).
    void resetCounters();

    void start();
    void stop();
    [[nodiscard]] bool running() const;

private:
    void run();

#ifdef _WIN32
    void setHighPriority();
#else
    void setHighPriority();
#endif

    hal::IHalInterface* hal_;
    const state::ControlTargets* targets_;
    comms::FaultQueue* faultQueue_;
    config::SafetyConfig cfg_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    // Motor divergence streak counter (resets on recovery)
    int motorDivergenceCount_ = 0;

    // Once a fault is reported, suppress re-reporting until resetCounters() is called.
    bool faultReported_ = false;
};

}  // namespace injector::safety
