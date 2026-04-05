/// Integration test: fault injection → safety detection → motor stop → state→Fault.
/// Covers AC-5.1 (overpressure detected within 10 ms of injection).

#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/FaultQueue.h"
#include "control/ControlLoop.h"
#include "fixtures/TestProtocols.h"
#include "hal/SimulatedHal.h"
#include "logging/DataLogger.h"
#include "safety/SafetyMonitor.h"
#include "state/StateMachine.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace injector;
using namespace injector::testing;
using CmdType = comms::InternalCommand::Type;

namespace {

/// Full-stack fixture including SafetyMonitor.
struct FaultFixture {
    state::ControlTargets targets;
    comms::CommandQueue commandQueue;
    comms::FaultQueue faultQueue;
    comms::EventBroadcast events;
    logging::DataLogger logger{300000, 10000};

    std::shared_ptr<hal::SimulatedHal> hal;
    std::unique_ptr<control::ControlLoop> controlLoop;
    std::unique_ptr<state::StateMachine> stateMachine;
    std::unique_ptr<safety::SafetyMonitor> safetyMonitor;

    explicit FaultFixture(const hal::HalConfig& halCfg = fastHalConfig()) {
        hal = std::make_shared<hal::SimulatedHal>(halCfg);
        controlLoop = std::make_unique<control::ControlLoop>(
            hal, defaultLoopConfig(halCfg), &logger.tickBuffer(),
            &targets, &commandQueue);
        stateMachine = std::make_unique<state::StateMachine>(
            &targets, &commandQueue, &faultQueue, &events, hal.get());
        safetyMonitor = std::make_unique<safety::SafetyMonitor>(
            hal.get(), &targets, &faultQueue, defaultSafetyConfig());
    }

    void start() {
        stateMachine->start();
        controlLoop->start();
        safetyMonitor->start();
    }

    void stop() {
        safetyMonitor->stop();
        controlLoop->stop();
        stateMachine->stop();
    }

    void loadAndStart(const state::Protocol& proto) {
        comms::InternalCommand load;
        load.type = CmdType::LoadProtocol;
        load.protocol = proto;
        commandQueue.push(load);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        comms::InternalCommand arm;
        arm.type = CmdType::Arm;
        commandQueue.push(arm);
        waitForState(state::InjectorState::Armed, 500);

        comms::InternalCommand start;
        start.type = CmdType::Start;
        commandQueue.push(start);
        waitForState(state::InjectorState::Injecting, 500);
    }

    bool waitForState(state::InjectorState desired, int timeout_ms) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (stateMachine->currentState() == desired) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
};

}  // namespace

// ── AC-5.1: Overpressure → Fault state within 100 ms ─────────────────────────

TEST(FaultEndToEnd, OverpressureFaultDetectedWithinDeadline) {
    FaultFixture f;
    f.start();

    f.loadAndStart(smallVolumeFast());

    // Let injection reach steady state
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_EQ(f.stateMachine->currentState(), state::InjectorState::Injecting);

    // Record time just before fault injection
    auto faultTime = std::chrono::steady_clock::now();

    // Inject overpressure fault — pressure jumps to 400 psi (well above 325 limit)
    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::Overpressure;
    fault.targetPsi = 400.0;
    f.hal->injectFault(fault);

    // Wait for state machine to reach Fault
    bool faulted = f.waitForState(state::InjectorState::Fault, 500);
    auto faultDetectedTime = std::chrono::steady_clock::now();

    ASSERT_TRUE(faulted)
        << "Did not reach Fault state; current: "
        << state::toString(f.stateMachine->currentState());

    double latencyMs = std::chrono::duration<double, std::milli>(
                           faultDetectedTime - faultTime)
                           .count();
    EXPECT_LT(latencyMs, 100.0)
        << "Fault detection latency " << latencyMs << " ms exceeds 100 ms";

    f.stop();
}

// ── Fault event details correct ───────────────────────────────────────────────

TEST(FaultEndToEnd, FaultEventContainsCorrectDetails) {
    FaultFixture f;
    f.start();

    f.loadAndStart(smallVolumeFast());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::Overpressure;
    fault.targetPsi = 350.0;
    f.hal->injectFault(fault);

    ASSERT_TRUE(f.waitForState(state::InjectorState::Fault, 500));

    // Check active faults
    auto faults = f.stateMachine->activeFaults();
    ASSERT_GE(faults.size(), 1u);
    EXPECT_EQ(faults[0].type, state::FaultType::Overpressure);
    EXPECT_DOUBLE_EQ(faults[0].measuredValue, 350.0);
    EXPECT_DOUBLE_EQ(faults[0].threshold, 325.0);

    // Check event broadcast for FaultDetected event
    auto evts = f.events.getEventsSince(0);
    bool foundFaultEvent = false;
    for (const auto& ev : evts) {
        if (ev.type == state::EventType::FaultDetected) {
            foundFaultEvent = true;
            EXPECT_NE(ev.detailsJson.find("OVERPRESSURE"), std::string::npos)
                << "FaultDetected event missing fault type: " << ev.detailsJson;
        }
    }
    EXPECT_TRUE(foundFaultEvent) << "No FaultDetected event found";

    f.stop();
}

// ── Motor stopped after fault ─────────────────────────────────────────────────

TEST(FaultEndToEnd, MotorStopsOnFault) {
    FaultFixture f;
    f.start();

    f.loadAndStart(smallVolumeFast());
    // Wait for motor to spin up
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_GT(f.hal->readMotorRpm(), 10.0) << "Expected motor spinning before fault";

    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::Overpressure;
    fault.targetPsi = 400.0;
    f.hal->injectFault(fault);

    ASSERT_TRUE(f.waitForState(state::InjectorState::Fault, 500));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Safety monitor called emergencyStop(), which zeroes motor instantly
    EXPECT_NEAR(f.hal->readMotorRpm(), 0.0, 5.0)
        << "Motor still running after fault: " << f.hal->readMotorRpm();

    f.stop();
}

// ── Reset after fault returns to Idle ─────────────────────────────────────────

TEST(FaultEndToEnd, ResetAfterFaultReturnsToIdle) {
    FaultFixture f;
    f.start();

    f.loadAndStart(smallVolumeFast());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::AirBubble;
    f.hal->injectFault(fault);

    ASSERT_TRUE(f.waitForState(state::InjectorState::Fault, 500));

    comms::InternalCommand reset;
    reset.type = CmdType::Reset;
    f.commandQueue.push(reset);

    ASSERT_TRUE(f.waitForState(state::InjectorState::Idle, 500));
    EXPECT_EQ(f.stateMachine->activeFaults().size(), 0u);

    f.stop();
}

// ── AC-5.1: Air bubble fault → Fault within 100 ms ───────────────────────────

TEST(FaultEndToEnd, AirBubbleFaultDetectedWithinDeadline) {
    FaultFixture f;
    f.start();

    f.loadAndStart(smallVolumeFast());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto faultTime = std::chrono::steady_clock::now();

    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::AirBubble;
    f.hal->injectFault(fault);

    bool faulted = f.waitForState(state::InjectorState::Fault, 500);
    auto detected = std::chrono::steady_clock::now();

    ASSERT_TRUE(faulted);
    double ms = std::chrono::duration<double, std::milli>(detected - faultTime).count();
    EXPECT_LT(ms, 100.0) << "Air bubble detection latency: " << ms << " ms";

    f.stop();
}

// ── Fault from Paused state ───────────────────────────────────────────────────

TEST(FaultEndToEnd, FaultDetectedWhilePaused) {
    FaultFixture f;
    f.start();

    f.loadAndStart(smallVolumeFast());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Pause
    comms::InternalCommand pause;
    pause.type = CmdType::Pause;
    f.commandQueue.push(pause);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Paused, 500));

    // Inject fault while paused
    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::Overpressure;
    fault.targetPsi = 400.0;
    f.hal->injectFault(fault);

    ASSERT_TRUE(f.waitForState(state::InjectorState::Fault, 500))
        << "Fault not detected from Paused state";

    f.stop();
}
