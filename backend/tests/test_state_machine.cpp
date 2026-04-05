#include "state/StateMachine.h"
#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/FaultQueue.h"

#include <gtest/gtest.h>

using namespace injector::state;
using namespace injector::comms;
using InjState = injector::state::InjectorState;
using CmdType = InternalCommand::Type;

namespace {

// Builds a valid 2-phase protocol
Protocol makeProtocol(int phases = 2) {
    Protocol p;
    p.name = "TestProto";
    Phase ph1;
    ph1.fluidType = FluidType::Contrast;
    ph1.flowRate = 4.0;
    ph1.volume = 80.0;
    ph1.pressureLimit = 300.0;
    p.phases.push_back(ph1);
    if (phases >= 2) {
        Phase ph2;
        ph2.fluidType = FluidType::Saline;
        ph2.flowRate = 2.0;
        ph2.volume = 30.0;
        ph2.pressureLimit = 200.0;
        p.phases.push_back(ph2);
    }
    return p;
}

InternalCommand makeCmd(CmdType type) {
    InternalCommand cmd;
    cmd.type = type;
    return cmd;
}

InternalCommand makeLoadProtocol(int phases = 2) {
    InternalCommand cmd;
    cmd.type = CmdType::LoadProtocol;
    cmd.protocol = makeProtocol(phases);
    return cmd;
}

// Minimal fixture: StateMachine with real ControlTargets and EventBroadcast,
// no CommandQueue/FaultQueue (tests call processCommand directly).
struct SM {
    ControlTargets targets;
    EventBroadcast events;
    StateMachine sm{&targets, nullptr, nullptr, &events, nullptr};
};

}  // namespace

// ── AC-4.1: Valid state transitions ─────────────────────────────────────────

TEST(StateMachine, InitialStateIsIdle) {
    SM f;
    EXPECT_EQ(f.sm.currentState(), InjState::Idle);
}

TEST(StateMachine, LoadProtocolInIdle) {
    SM f;
    auto result = f.sm.processCommand(makeLoadProtocol());
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(f.sm.hasProtocol());
}

TEST(StateMachine, IdleToArmed) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    auto r = f.sm.processCommand(makeCmd(CmdType::Arm));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Armed);
}

TEST(StateMachine, ArmedToInjecting) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    auto r = f.sm.processCommand(makeCmd(CmdType::Start));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Injecting);
}

TEST(StateMachine, InjectingToPaused) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));
    auto r = f.sm.processCommand(makeCmd(CmdType::Pause));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Paused);
}

TEST(StateMachine, PausedToInjecting) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));
    (void)f.sm.processCommand(makeCmd(CmdType::Pause));
    auto r = f.sm.processCommand(makeCmd(CmdType::Resume));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Injecting);
}

TEST(StateMachine, ArmedToIdle) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    auto r = f.sm.processCommand(makeCmd(CmdType::Disarm));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Idle);
}

TEST(StateMachine, FaultToIdle) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));

    FaultEvent fe;
    fe.type = FaultType::Overpressure;
    fe.measuredValue = 400.0;
    fe.threshold = 300.0;
    f.sm.processFaultEvent(fe);
    EXPECT_EQ(f.sm.currentState(), InjState::Fault);

    auto r = f.sm.processCommand(makeCmd(CmdType::Reset));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Idle);
}

// ── AC-4.2: Invalid transitions are rejected ─────────────────────────────────

TEST(StateMachine, ArmWithoutProtocol) {
    SM f;
    auto r = f.sm.processCommand(makeCmd(CmdType::Arm));
    EXPECT_FALSE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Idle);
}

TEST(StateMachine, StartFromIdle) {
    SM f;
    auto r = f.sm.processCommand(makeCmd(CmdType::Start));
    EXPECT_FALSE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Idle);
}

TEST(StateMachine, PauseFromIdle) {
    SM f;
    auto r = f.sm.processCommand(makeCmd(CmdType::Pause));
    EXPECT_FALSE(r.success);
}

TEST(StateMachine, ResumeFromArmed) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    auto r = f.sm.processCommand(makeCmd(CmdType::Resume));
    EXPECT_FALSE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Armed);
}

TEST(StateMachine, ResetFromInjecting) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));
    auto r = f.sm.processCommand(makeCmd(CmdType::Reset));
    EXPECT_FALSE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Injecting);
}

TEST(StateMachine, LoadProtocolNotInIdle) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    auto r = f.sm.processCommand(makeLoadProtocol());
    EXPECT_FALSE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Armed);
}

TEST(StateMachine, DisarmNotFromArmed) {
    SM f;
    auto r = f.sm.processCommand(makeCmd(CmdType::Disarm));
    EXPECT_TRUE(r.success);  // idempotent when already Idle
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));
    r = f.sm.processCommand(makeCmd(CmdType::Disarm));
    EXPECT_FALSE(r.success);
}

// ── AC-4.2: Idempotent commands ──────────────────────────────────────────────

TEST(StateMachine, ArmWhenAlreadyArmed) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    auto r = f.sm.processCommand(makeCmd(CmdType::Arm));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Armed);
}

TEST(StateMachine, StartWhenAlreadyInjecting) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));
    auto r = f.sm.processCommand(makeCmd(CmdType::Start));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Injecting);
}

// ── AC-4.3: Multi-phase protocol progression ─────────────────────────────────

TEST(StateMachine, TwoPhaseProtocolProgression) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol(2));
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));

    EXPECT_EQ(f.sm.currentPhaseIndex(), 0);
    EXPECT_EQ(f.sm.currentState(), InjState::Injecting);

    // Complete phase 0
    InternalCommand phaseCmd;
    phaseCmd.type = CmdType::PhaseComplete;
    phaseCmd.phaseIndex = 0;
    phaseCmd.volumeDelivered = 80.0;
    (void)f.sm.processCommand(phaseCmd);

    EXPECT_EQ(f.sm.currentState(), InjState::Injecting);
    EXPECT_EQ(f.sm.currentPhaseIndex(), 1);

    // Complete phase 1 → Completed
    phaseCmd.phaseIndex = 1;
    phaseCmd.volumeDelivered = 30.0;
    (void)f.sm.processCommand(phaseCmd);

    EXPECT_EQ(f.sm.currentState(), InjState::Completed);
}

TEST(StateMachine, VolumeTrackedPerPhase) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol(2));
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));

    InternalCommand phaseCmd;
    phaseCmd.type = CmdType::PhaseComplete;
    phaseCmd.phaseIndex = 0;
    phaseCmd.volumeDelivered = 79.5;
    (void)f.sm.processCommand(phaseCmd);

    auto vol = f.sm.volumeDeliveredPerPhase();
    ASSERT_EQ(vol.size(), 2u);
    EXPECT_DOUBLE_EQ(vol[0], 79.5);
    EXPECT_DOUBLE_EQ(vol[1], 0.0);
}

TEST(StateMachine, SinglePhaseCompletesToCompleted) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol(1));
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));

    InternalCommand phaseCmd;
    phaseCmd.type = CmdType::PhaseComplete;
    phaseCmd.phaseIndex = 0;
    phaseCmd.volumeDelivered = 80.0;
    (void)f.sm.processCommand(phaseCmd);

    EXPECT_EQ(f.sm.currentState(), InjState::Completed);
}

// ── AC-4.3: ControlTargets updated on transitions ────────────────────────────

TEST(StateMachine, TargetsUpdatedOnStart) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol(2));
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));

    EXPECT_DOUBLE_EQ(f.targets.targetFlowRate.load(), 4.0);
    EXPECT_DOUBLE_EQ(f.targets.pressureLimit.load(), 300.0);
    EXPECT_EQ(f.targets.activePhaseIndex.load(), 0);
    EXPECT_TRUE(f.targets.injecting.load());
    EXPECT_EQ(f.targets.activeFluidChannel.load(), 0);  // Contrast
}

TEST(StateMachine, TargetsClearedOnPause) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));
    (void)f.sm.processCommand(makeCmd(CmdType::Pause));

    EXPECT_FALSE(f.targets.injecting.load());
    EXPECT_DOUBLE_EQ(f.targets.targetFlowRate.load(), 0.0);
}

TEST(StateMachine, TargetsUpdatedOnPhaseAdvance) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol(2));
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));

    InternalCommand phaseCmd;
    phaseCmd.type = CmdType::PhaseComplete;
    phaseCmd.phaseIndex = 0;
    phaseCmd.volumeDelivered = 80.0;
    (void)f.sm.processCommand(phaseCmd);

    // Phase 1 is saline at 2.0 mL/s
    EXPECT_DOUBLE_EQ(f.targets.targetFlowRate.load(), 2.0);
    EXPECT_EQ(f.targets.activeFluidChannel.load(), 1);  // Saline
    EXPECT_EQ(f.targets.activePhaseIndex.load(), 1);
}

// ── AC-4.4: EMERGENCY_STOP always works ─────────────────────────────────────

TEST(StateMachine, EmergencyStopFromArmed) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    auto r = f.sm.processCommand(makeCmd(CmdType::EmergencyStop));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Fault);
}

TEST(StateMachine, EmergencyStopFromInjecting) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));
    auto r = f.sm.processCommand(makeCmd(CmdType::EmergencyStop));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Fault);
    EXPECT_FALSE(f.targets.injecting.load());
}

TEST(StateMachine, EmergencyStopFromPaused) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));
    (void)f.sm.processCommand(makeCmd(CmdType::Pause));
    auto r = f.sm.processCommand(makeCmd(CmdType::EmergencyStop));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Fault);
}

TEST(StateMachine, EmergencyStopFromIdleIsNoop) {
    SM f;
    auto r = f.sm.processCommand(makeCmd(CmdType::EmergencyStop));
    EXPECT_TRUE(r.success);
    EXPECT_EQ(f.sm.currentState(), InjState::Idle);
}

// ── Protocol validation ──────────────────────────────────────────────────────

TEST(StateMachine, LoadProtocolEmptyRejected) {
    SM f;
    InternalCommand cmd;
    cmd.type = CmdType::LoadProtocol;
    // protocol.phases is empty
    auto r = f.sm.processCommand(cmd);
    EXPECT_FALSE(r.success);
}

TEST(StateMachine, LoadProtocolInvalidFlowRate) {
    SM f;
    InternalCommand cmd;
    cmd.type = CmdType::LoadProtocol;
    Phase ph;
    ph.flowRate = 0.05;  // below 0.1
    ph.volume = 50.0;
    ph.pressureLimit = 200.0;
    cmd.protocol.phases.push_back(ph);
    auto r = f.sm.processCommand(cmd);
    EXPECT_FALSE(r.success);
}

TEST(StateMachine, LoadProtocolInvalidVolume) {
    SM f;
    InternalCommand cmd;
    cmd.type = CmdType::LoadProtocol;
    Phase ph;
    ph.flowRate = 4.0;
    ph.volume = 0.5;  // below 1.0
    ph.pressureLimit = 200.0;
    cmd.protocol.phases.push_back(ph);
    auto r = f.sm.processCommand(cmd);
    EXPECT_FALSE(r.success);
}

// ── Fault processing ─────────────────────────────────────────────────────────

TEST(StateMachine, FaultFromInjecting) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));

    FaultEvent fe;
    fe.type = FaultType::AirBubble;
    fe.measuredValue = 1.0;
    fe.threshold = 0.0;
    f.sm.processFaultEvent(fe);

    EXPECT_EQ(f.sm.currentState(), InjState::Fault);
    EXPECT_FALSE(f.targets.injecting.load());
    ASSERT_EQ(f.sm.activeFaults().size(), 1u);
    EXPECT_EQ(f.sm.activeFaults()[0].type, FaultType::AirBubble);
}

TEST(StateMachine, FaultFromPaused) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    (void)f.sm.processCommand(makeCmd(CmdType::Start));
    (void)f.sm.processCommand(makeCmd(CmdType::Pause));

    FaultEvent fe;
    fe.type = FaultType::MotorStall;
    f.sm.processFaultEvent(fe);
    EXPECT_EQ(f.sm.currentState(), InjState::Fault);
}

TEST(StateMachine, FaultClearedOnReset) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));

    FaultEvent fe;
    fe.type = FaultType::Overpressure;
    f.sm.processFaultEvent(fe);
    EXPECT_EQ(f.sm.activeFaults().size(), 1u);

    (void)f.sm.processCommand(makeCmd(CmdType::Reset));
    EXPECT_EQ(f.sm.activeFaults().size(), 0u);
    EXPECT_EQ(f.sm.currentState(), InjState::Idle);
}

// ── Events emitted ───────────────────────────────────────────────────────────

TEST(StateMachine, StateTransitionEventEmitted) {
    SM f;
    (void)f.sm.processCommand(makeLoadProtocol());

    size_t seq = 0;
    auto events = f.events.getEventsSince(seq);
    // ProtocolLoaded event
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events.back().type, injector::state::EventType::ProtocolLoaded);

    seq = f.events.totalPublished();
    (void)f.sm.processCommand(makeCmd(CmdType::Arm));
    events = f.events.getEventsSince(seq);
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].type, injector::state::EventType::StateTransition);
}
