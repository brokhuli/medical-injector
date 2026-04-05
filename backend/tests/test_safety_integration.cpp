/// Integration test: safety monitor independence (AC-5.5).
/// Verifies that the safety monitor detects faults and halts hardware
/// even when the state machine is stalled or not running.

#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/FaultQueue.h"
#include "fixtures/TestProtocols.h"
#include "hal/SimulatedHal.h"
#include "safety/SafetyMonitor.h"
#include "state/Protocol.h"
#include "state/StateMachine.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace injector;
using namespace injector::testing;

// ── AC-5.5: Safety monitor operates independently of the state machine ────────

TEST(SafetyIntegration, DetectsFaultWithoutStateMachine) {
    // No StateMachine, no ControlLoop — just HAL + SafetyMonitor + FaultQueue.
    // This verifies the safety monitor is self-contained.
    auto hal = std::make_shared<hal::SimulatedHal>(fastHalConfig());

    state::ControlTargets targets;
    comms::FaultQueue faultQueue;

    safety::SafetyMonitor sm(hal.get(), &targets, &faultQueue,
                              defaultSafetyConfig());
    sm.start();

    // Let monitor run for a tick to establish baseline
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(faultQueue.empty()) << "Unexpected fault at baseline";

    // Inject overpressure directly on HAL — safety monitor should catch it
    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::Overpressure;
    fault.targetPsi = 400.0;
    hal->injectFault(fault);

    // Wait up to 50 ms for fault to appear in queue
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (std::chrono::steady_clock::now() < deadline && faultQueue.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_FALSE(faultQueue.empty()) << "Safety monitor did not detect overpressure";

    auto f = faultQueue.pop();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->type, state::FaultType::Overpressure);

    sm.stop();
}

// ── AC-5.5: Emergency stop called on HAL before state machine is notified ─────

TEST(SafetyIntegration, EmergencyStopCalledBeforeFaultQueued) {
    // Safety monitor must call hal->emergencyStop() synchronously in its own
    // thread — motor stops even if the state machine queue is never drained.
    auto hal = std::make_shared<hal::SimulatedHal>(fastHalConfig());

    // Spin the motor up manually (simulating control loop having done so)
    hal->setValve(hal::FluidChannel::Contrast, hal::ValveState::Open);
    hal->setMotorRpm(400.0);
    // Tick a few times to let motor approach setpoint
    for (int i = 0; i < 50; ++i) {
        hal->tick(0.002);
    }
    EXPECT_GT(hal->readMotorRpm(), 10.0) << "Motor should be spinning";

    state::ControlTargets targets;
    targets.injecting.store(true);
    comms::FaultQueue faultQueue;

    safety::SafetyMonitor sm(hal.get(), &targets, &faultQueue,
                              defaultSafetyConfig());
    sm.start();

    // Inject overpressure
    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::Overpressure;
    fault.targetPsi = 400.0;
    hal->injectFault(fault);

    // Wait for the safety monitor to act
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (std::chrono::steady_clock::now() < deadline && faultQueue.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ASSERT_FALSE(faultQueue.empty());
    sm.stop();

    // Motor must be zero — emergency stop was called
    EXPECT_NEAR(hal->readMotorRpm(), 0.0, 5.0)
        << "Motor still running after safety emergency stop";
}

// ── AC-5.5: Safety monitor detects air bubble regardless of state machine ─────

TEST(SafetyIntegration, AirBubbleDetectedWithoutStateMachine) {
    auto hal = std::make_shared<hal::SimulatedHal>(fastHalConfig());

    state::ControlTargets targets;
    comms::FaultQueue faultQueue;

    safety::SafetyMonitor sm(hal.get(), &targets, &faultQueue,
                              defaultSafetyConfig());
    sm.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::AirBubble;
    hal->injectFault(fault);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (std::chrono::steady_clock::now() < deadline && faultQueue.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ASSERT_FALSE(faultQueue.empty());
    auto f = faultQueue.pop();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->type, state::FaultType::AirBubble);

    sm.stop();
}

// ── Fault faults from real physics (not injected) — near-limit config ─────────

TEST(SafetyIntegration, RealPhysicsOverpressureDetected) {
    // Near-limit config: at 4 mL/s pressure = 4*80+10 = 330 psi > 325 limit
    auto hal = std::make_shared<hal::SimulatedHal>(nearLimitHalConfig());

    // Set motor to produce 4 mL/s (RPM = 4.0 / 0.01 = 400)
    hal->setValve(hal::FluidChannel::Contrast, hal::ValveState::Open);
    hal->setMotorRpm(400.0);
    // Tick until motor converges: 50 ticks × 2ms = 100ms = 10 time constants (10ms τ)
    // → 99.995% of 400 RPM = ~399 RPM → flow ≈ 3.99 mL/s → pressure ≈ 329 psi > 325
    for (int i = 0; i < 50; ++i) {
        hal->tick(0.002);
    }

    state::ControlTargets targets;
    // Tell the safety monitor that we commanded 400 RPM so motor divergence
    // doesn't fire before overpressure (commanded=400, actual≈220 → divergence 180 < 200).
    targets.lastCommandedRpm.store(400.0);
    comms::FaultQueue faultQueue;

    auto cfg = defaultSafetyConfig();
    cfg.defaultPressureLimitPsi = 325.0;

    safety::SafetyMonitor sm(hal.get(), &targets, &faultQueue, cfg);
    sm.start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (std::chrono::steady_clock::now() < deadline && faultQueue.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_FALSE(faultQueue.empty())
        << "Expected overpressure from real physics (pressure="
        << hal->readPressure() << " psi)";

    if (!faultQueue.empty()) {
        auto f = faultQueue.pop();
        ASSERT_TRUE(f.has_value());
        EXPECT_EQ(f->type, state::FaultType::Overpressure);
        EXPECT_GT(f->measuredValue, 325.0);
    }

    sm.stop();
}

// ── Safety monitor continues after state machine thread exits ─────────────────

TEST(SafetyIntegration, SafetyMonitorOutlivesStateMachine) {
    auto hal = std::make_shared<hal::SimulatedHal>(fastHalConfig());

    state::ControlTargets targets;
    comms::FaultQueue faultQueue;
    comms::EventBroadcast events;
    comms::CommandQueue commandQueue;

    // Start and immediately stop the state machine
    {
        state::StateMachine sm(&targets, &commandQueue, &faultQueue, &events,
                               hal.get());
        sm.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        sm.stop();
        // sm destroyed here
    }

    // Safety monitor should still function after state machine is gone
    safety::SafetyMonitor monitor(hal.get(), &targets, &faultQueue,
                                   defaultSafetyConfig());
    monitor.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(monitor.running());

    // Inject fault — safety monitor still detects it
    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::Overpressure;
    fault.targetPsi = 400.0;
    hal->injectFault(fault);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (std::chrono::steady_clock::now() < deadline && faultQueue.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_FALSE(faultQueue.empty())
        << "Safety monitor failed to detect fault after SM exit";

    monitor.stop();
}
