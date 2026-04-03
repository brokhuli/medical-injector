#pragma once

#include "control/PidController.h"
#include "hal/IHalInterface.h"
#include "logging/RingBuffer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace injector::control {

/// Per-tick telemetry snapshot pushed to the ring buffer.
struct TickData {
    uint64_t timestampUs = 0;
    double targetFlowRate = 0.0;
    double actualFlowRate = 0.0;
    double pressure = 0.0;
    double motorRpmCommanded = 0.0;
    double motorRpmActual = 0.0;
    uint8_t contrastValve = 0;
    uint8_t salineValve = 0;
    uint8_t state = 0;
    uint8_t phaseIndex = 255;
    double contrastRemaining = 0.0;
    double salineRemaining = 0.0;
};

/// Timing statistics collected over the control loop's lifetime.
struct TimingStats {
    double meanTickMs = 0.0;
    double maxTickMs = 0.0;
    double minTickMs = 0.0;
    uint64_t totalTicks = 0;
    uint64_t overrunCount = 0;  // ticks that exceeded 2x the target period
};

struct ControlLoopConfig {
    int tickRateMs = 2;
    bool pinCore = true;
    double flowPerRpm = 0.01;  // HAL's RPM-to-flow conversion factor
    PidConfig pid;
};

class ControlLoop {
public:
    ControlLoop(std::shared_ptr<hal::IHalInterface> hal,
                const ControlLoopConfig& config,
                logging::RingBuffer<TickData>* tickBuffer = nullptr);

    ~ControlLoop();

    /// Start the control loop thread.
    void start();

    /// Stop the control loop thread and join.
    void stop();

    /// Set the target flow rate (thread-safe).
    void setTargetFlowRate(double mlPerSec);

    /// Get the current target flow rate.
    [[nodiscard]] double targetFlowRate() const;

    /// Get cumulative volume delivered (mL), thread-safe.
    [[nodiscard]] double cumulativeVolume() const;

    /// Get timing statistics (thread-safe snapshot).
    [[nodiscard]] TimingStats timingStats() const;

    /// Whether the loop is currently running.
    [[nodiscard]] bool running() const;

private:
    void run();
    void pinToCore();

    std::shared_ptr<hal::IHalInterface> hal_;
    ControlLoopConfig config_;
    PidController pid_;
    logging::RingBuffer<TickData>* tickBuffer_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<double> targetFlowRate_{0.0};
    std::atomic<double> cumulativeVolume_{0.0};

    // Timing stats (written only by the loop thread, read atomically)
    std::atomic<double> meanTickMs_{0.0};
    std::atomic<double> maxTickMs_{0.0};
    std::atomic<double> minTickMs_{1e9};
    std::atomic<uint64_t> totalTicks_{0};
    std::atomic<uint64_t> overrunCount_{0};

    std::chrono::steady_clock::time_point startTime_;
};

}  // namespace injector::control
