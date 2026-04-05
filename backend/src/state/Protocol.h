#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace injector::state {

/// Which syringe barrel to draw fluid from.
enum class FluidType {
    Contrast = 0,
    Saline = 1
};

/// A single segment of an injection protocol.
struct Phase {
    FluidType fluidType = FluidType::Contrast;
    double flowRate = 0.0;         // mL/s  [0.1, 10.0]
    double volume = 0.0;           // mL    [1.0, 200.0]
    double pressureLimit = 325.0;  // psi   [50.0, 400.0]
};

/// A user-defined injection plan with 1-20 sequential phases.
struct Protocol {
    std::string name;
    std::vector<Phase> phases;

    [[nodiscard]] bool empty() const { return phases.empty(); }
    [[nodiscard]] size_t phaseCount() const { return phases.size(); }
};

/// Shared control targets written by the State Machine and read by the Control Loop.
/// Atomics for hot-path reads (every 2 ms); protocol details accessed under mutex only
/// on phase transitions.
struct ControlTargets {
    std::atomic<double> targetFlowRate{0.0};     // mL/s
    std::atomic<double> pressureLimit{325.0};    // psi
    std::atomic<int> activePhaseIndex{-1};       // -1 = not injecting
    std::atomic<bool> injecting{false};
    std::atomic<int> activeFluidChannel{0};      // 0 = contrast, 1 = saline

    // Written by StateMachine on phase start; read by ControlLoop to detect phase completion.
    std::atomic<double> currentPhaseVolumeTarget{0.0};  // mL; 0 = no active phase

    // Written by ControlLoop each tick; read by SafetyMonitor for divergence/timing checks.
    std::atomic<double> lastCommandedRpm{0.0};
    std::atomic<uint64_t> lastTickTimestampUs{0};  // microseconds since steady_clock epoch

    mutable std::mutex protocolMutex;
    Protocol loadedProtocol;

    ControlTargets() = default;
    ControlTargets(const ControlTargets&) = delete;
    ControlTargets& operator=(const ControlTargets&) = delete;
};

}  // namespace injector::state
