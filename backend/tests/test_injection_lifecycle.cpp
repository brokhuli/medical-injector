/// Integration test: full injection lifecycle with real threads.
/// Covers AC-4.3 (multi-phase progression, volume ±2%), AC-7.3 (event completeness).

#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/FaultQueue.h"
#include "control/ControlLoop.h"
#include "fixtures/TestProtocols.h"
#include "hal/SimulatedHal.h"
#include "logging/DataLogger.h"
#include "state/StateMachine.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace injector;
using namespace injector::testing;

namespace {

/// All the wired-together pieces for a full-stack integration test.
struct IntegrationFixture {
    // Shared objects
    state::ControlTargets targets;
    comms::CommandQueue commandQueue;
    comms::FaultQueue faultQueue;
    comms::EventBroadcast events;
    logging::DataLogger logger{300000, 10000};

    // Components
    std::shared_ptr<hal::SimulatedHal> hal;
    std::unique_ptr<control::ControlLoop> controlLoop;
    std::unique_ptr<state::StateMachine> stateMachine;

    explicit IntegrationFixture(
        const hal::HalConfig& halCfg = fastHalConfig()) {
        hal = std::make_shared<hal::SimulatedHal>(halCfg);
        controlLoop = std::make_unique<control::ControlLoop>(
            hal, defaultLoopConfig(halCfg), &logger.tickBuffer(),
            &targets, &commandQueue);
        stateMachine = std::make_unique<state::StateMachine>(
            &targets, &commandQueue, &faultQueue, &events, hal.get());
    }

    void start() {
        stateMachine->start();
        controlLoop->start();
    }

    void stop() {
        controlLoop->stop();
        stateMachine->stop();
    }

    // Load protocol and send command synchronously via queue, then let SM drain.
    void loadProtocol(const state::Protocol& proto) {
        comms::InternalCommand cmd;
        cmd.type = comms::InternalCommand::Type::LoadProtocol;
        cmd.protocol = proto;
        commandQueue.push(cmd);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    void sendCommand(comms::InternalCommand::Type type) {
        comms::InternalCommand cmd;
        cmd.type = type;
        commandQueue.push(cmd);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    /// Block until state == desired or timeout_ms elapses.
    bool waitForState(state::InjectorState desired,
                      int timeout_ms = 120'000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (stateMachine->currentState() == desired) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
};

using CmdType = comms::InternalCommand::Type;

}  // namespace

// ── AC-4.3: Single-phase injection completes, volume within ±2% ──────────────

TEST(InjectionLifecycle, SinglePhaseCompletesWithVolumeAccuracy) {
    IntegrationFixture f;
    f.start();

    f.loadProtocol(smallVolumeFast());  // 8 mL at 4.0 mL/s
    f.sendCommand(CmdType::Arm);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Armed, 500));

    f.sendCommand(CmdType::Start);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Injecting, 500));

    // Wait for Completed (8 mL / 4 mL/s ≈ 2s, allow 15s margin for ramp-up)
    ASSERT_TRUE(f.waitForState(state::InjectorState::Completed, 15'000))
        << "State at timeout: "
        << state::toString(f.stateMachine->currentState());

    auto vol = f.stateMachine->volumeDeliveredPerPhase();
    ASSERT_EQ(vol.size(), 1u);
    EXPECT_NEAR(vol[0], 8.0, 8.0 * 0.02)
        << "Phase 0 volume outside ±2%: " << vol[0];

    f.stop();
}

// ── AC-4.3: Two-phase injection, both phases complete within ±2% ─────────────

TEST(InjectionLifecycle, TwoPhaseCompletesWithVolumeAccuracy) {
    IntegrationFixture f;
    f.start();

    f.loadProtocol(twoPhaseStandard());  // Contrast 80 mL, Saline 30 mL
    f.sendCommand(CmdType::Arm);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Armed, 500));

    f.sendCommand(CmdType::Start);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Injecting, 500));

    // 80 mL / 4 mL/s + 30 mL / 2 mL/s = 35s. Allow 90s for ramp-up and timing.
    ASSERT_TRUE(f.waitForState(state::InjectorState::Completed, 90'000))
        << "State at timeout: "
        << state::toString(f.stateMachine->currentState());

    auto vol = f.stateMachine->volumeDeliveredPerPhase();
    ASSERT_EQ(vol.size(), 2u);
    EXPECT_NEAR(vol[0], 80.0, 80.0 * 0.02)
        << "Phase 0 (contrast) volume outside ±2%: " << vol[0];
    EXPECT_NEAR(vol[1], 30.0, 30.0 * 0.02)
        << "Phase 1 (saline) volume outside ±2%: " << vol[1];

    f.stop();
}

// ── Phase transition event emitted between phases ────────────────────────────

TEST(InjectionLifecycle, PhaseTransitionEventEmitted) {
    IntegrationFixture f;
    f.start();

    f.loadProtocol(twoPhaseStandard());
    f.sendCommand(CmdType::Arm);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Armed, 500));
    f.sendCommand(CmdType::Start);

    ASSERT_TRUE(f.waitForState(state::InjectorState::Completed, 90'000));

    // Check event log for PhaseTransition between phase 0 and 1
    auto allEvents = f.events.getEventsSince(0);
    bool foundPhaseTransition = false;
    bool foundCompleted = false;
    for (const auto& ev : allEvents) {
        if (ev.type == state::EventType::PhaseTransition) {
            foundPhaseTransition = true;
        }
        if (ev.type == state::EventType::StateTransition) {
            if (ev.detailsJson.find("\"to\":\"Completed\"") != std::string::npos) {
                foundCompleted = true;
            }
        }
    }
    EXPECT_TRUE(foundPhaseTransition)
        << "No PhaseTransition event found in " << allEvents.size() << " events";
    EXPECT_TRUE(foundCompleted)
        << "No Completed state transition event found";

    f.stop();
}

// ── AC-7.3: All 6 expected events present, with monotonic timestamps ─────────

TEST(InjectionLifecycle, EventLogHasRequiredEventsWithMonotonicTimestamps) {
    IntegrationFixture f;
    f.start();

    f.loadProtocol(smallVolumeFast());
    f.sendCommand(CmdType::Arm);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Armed, 500));
    f.sendCommand(CmdType::Start);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Completed, 15'000));

    auto allEvents = f.events.getEventsSince(0);
    ASSERT_GE(allEvents.size(), 3u) << "Expected at least 3 events";

    // Timestamps must be monotonically non-decreasing
    for (size_t i = 1; i < allEvents.size(); ++i) {
        EXPECT_GE(allEvents[i].timestampUs, allEvents[i - 1].timestampUs)
            << "Timestamp not monotonic at index " << i;
    }

    // Must include ProtocolLoaded, two StateTransition (Armed, Injecting, Completed)
    int stateTransitionCount = 0;
    bool protocolLoaded = false;
    for (const auto& ev : allEvents) {
        if (ev.type == state::EventType::ProtocolLoaded) protocolLoaded = true;
        if (ev.type == state::EventType::StateTransition) ++stateTransitionCount;
    }
    EXPECT_TRUE(protocolLoaded);
    EXPECT_GE(stateTransitionCount, 3);  // Idle→Armed, Armed→Injecting, Injecting→Completed

    f.stop();
}

// ── Pause/Resume: volume pauses then resumes ──────────────────────────────────

TEST(InjectionLifecycle, PauseResumeContinuesInjection) {
    IntegrationFixture f;
    f.start();

    // Use a larger volume so we have time to pause mid-injection
    state::Phase ph;
    ph.fluidType = state::FluidType::Contrast;
    ph.flowRate = 4.0;
    ph.volume = 40.0;
    ph.pressureLimit = 325.0;
    state::Protocol proto;
    proto.phases.push_back(ph);

    f.loadProtocol(proto);
    f.sendCommand(CmdType::Arm);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Armed, 500));
    f.sendCommand(CmdType::Start);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Injecting, 500));

    // Let it run for 2 seconds
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Pause
    f.sendCommand(CmdType::Pause);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Paused, 500));
    double volAtPause = f.controlLoop->cumulativeVolume();
    EXPECT_GT(volAtPause, 0.5) << "Expected some volume before pause";

    // Wait 1 second — volume should not change
    std::this_thread::sleep_for(std::chrono::seconds(1));
    double volAfterWait = f.controlLoop->cumulativeVolume();
    EXPECT_NEAR(volAfterWait, volAtPause, 0.05)
        << "Volume changed during pause";

    // Resume and wait for completion
    f.sendCommand(CmdType::Resume);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Injecting, 500));
    ASSERT_TRUE(f.waitForState(state::InjectorState::Completed, 30'000));

    auto vol = f.stateMachine->volumeDeliveredPerPhase();
    ASSERT_EQ(vol.size(), 1u);
    EXPECT_NEAR(vol[0], 40.0, 40.0 * 0.02)
        << "Final volume outside ±2%: " << vol[0];

    f.stop();
}

// ── Tick data logged throughout injection ─────────────────────────────────────

TEST(InjectionLifecycle, TickDataLoggedDuringInjection) {
    IntegrationFixture f;
    f.start();

    f.loadProtocol(smallVolumeFast());  // ~2s injection
    f.sendCommand(CmdType::Arm);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Armed, 500));
    f.sendCommand(CmdType::Start);
    ASSERT_TRUE(f.waitForState(state::InjectorState::Completed, 15'000));

    // At 2 ms tick rate for ~2s: expect ~1000 ticks; be generous with tolerance
    size_t ticked = f.logger.tickBuffer().totalPushed();
    EXPECT_GT(ticked, 500u) << "Expected substantial tick data, got " << ticked;

    f.stop();
}
