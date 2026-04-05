#include "safety/SafetyMonitor.h"
#include "config/Config.h"
#include "mocks/MockHal.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace injector::safety;
using namespace injector::state;
using namespace injector::comms;
using namespace injector::config;
using namespace injector::testing;
using ::testing::Return;
using ::testing::_;

namespace {

SafetyConfig makeConfig() {
    SafetyConfig cfg;
    cfg.defaultPressureLimitPsi = 325.0;
    cfg.motorDivergenceThreshold = 200.0;
    cfg.motorDivergenceTicks = 3;   // low for faster tests
    cfg.jitterToleranceMs = 3.0;
    cfg.tickRateMs = 1;
    return cfg;
}

// Returns a microsecond timestamp
constexpr uint64_t kNowUs = 1'000'000;

}  // namespace

// ── AC-5.1: Overpressure detection ──────────────────────────────────────────

TEST(SafetyMonitor, NoFaultWhenPressureNormal) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(200.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    SafetyMonitor sm(&hal, &targets, nullptr, makeConfig());

    auto faults = sm.checkOnce(kNowUs);
    EXPECT_TRUE(faults.empty());
}

TEST(SafetyMonitor, DetectsOverpressure) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(400.0));  // > 325
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    SafetyMonitor sm(&hal, &targets, nullptr, makeConfig());

    auto faults = sm.checkOnce(kNowUs);
    ASSERT_EQ(faults.size(), 1u);
    EXPECT_EQ(faults[0].type, FaultType::Overpressure);
    EXPECT_DOUBLE_EQ(faults[0].measuredValue, 400.0);
    EXPECT_DOUBLE_EQ(faults[0].threshold, 325.0);
}

TEST(SafetyMonitor, OverpressureUsesPerPhaseLimitFromTargets) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(280.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    targets.pressureLimit.store(250.0);  // lower per-phase limit

    SafetyMonitor sm(&hal, &targets, nullptr, makeConfig());
    auto faults = sm.checkOnce(kNowUs);

    ASSERT_EQ(faults.size(), 1u);
    EXPECT_EQ(faults[0].type, FaultType::Overpressure);
    EXPECT_DOUBLE_EQ(faults[0].threshold, 250.0);
}

TEST(SafetyMonitor, PressureAtExactLimitIsNotFault) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(325.0));  // exactly at limit
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    SafetyMonitor sm(&hal, &targets, nullptr, makeConfig());

    auto faults = sm.checkOnce(kNowUs);
    EXPECT_TRUE(faults.empty());
}

// ── AC-5.2: Air bubble detection ─────────────────────────────────────────────

TEST(SafetyMonitor, DetectsAirBubble) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(100.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(true));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    SafetyMonitor sm(&hal, &targets, nullptr, makeConfig());

    auto faults = sm.checkOnce(kNowUs);
    ASSERT_EQ(faults.size(), 1u);
    EXPECT_EQ(faults[0].type, FaultType::AirBubble);
}

TEST(SafetyMonitor, NoAirBubbleWhenDetectorFalse) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(100.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    SafetyMonitor sm(&hal, &targets, nullptr, makeConfig());

    auto faults = sm.checkOnce(kNowUs);
    EXPECT_TRUE(faults.empty());
}

// ── AC-5.3: Motor stall / divergence detection ───────────────────────────────

TEST(SafetyMonitor, MotorDivergenceNotFaultUntilNConsecutive) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(100.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    targets.lastCommandedRpm.store(400.0);  // commanded but actual is 0 → divergence 400

    auto cfg = makeConfig();
    cfg.motorDivergenceThreshold = 200.0;
    cfg.motorDivergenceTicks = 3;
    SafetyMonitor sm(&hal, &targets, nullptr, cfg);

    // First 2 ticks: count builds but no fault yet
    EXPECT_TRUE(sm.checkOnce(kNowUs).empty());
    EXPECT_TRUE(sm.checkOnce(kNowUs).empty());

    // 3rd tick: fault fires
    auto faults = sm.checkOnce(kNowUs);
    ASSERT_EQ(faults.size(), 1u);
    EXPECT_EQ(faults[0].type, FaultType::MotorStall);
    EXPECT_DOUBLE_EQ(faults[0].measuredValue, 400.0);  // divergence = 400-0
}

TEST(SafetyMonitor, MotorDivergenceCounterResetsOnRecovery) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(100.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));

    ControlTargets targets;
    targets.lastCommandedRpm.store(400.0);

    auto cfg = makeConfig();
    cfg.motorDivergenceThreshold = 200.0;
    cfg.motorDivergenceTicks = 3;
    SafetyMonitor sm(&hal, &targets, nullptr, cfg);

    // 2 ticks of high divergence
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));
    (void)sm.checkOnce(kNowUs);
    (void)sm.checkOnce(kNowUs);

    // Recovery tick: RPM matches
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(390.0));
    EXPECT_TRUE(sm.checkOnce(kNowUs).empty());

    // Back to divergence but counter was reset — need another N ticks
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));
    EXPECT_TRUE(sm.checkOnce(kNowUs).empty());   // count = 1, no fault
    EXPECT_TRUE(sm.checkOnce(kNowUs).empty());   // count = 2, no fault
    auto faults = sm.checkOnce(kNowUs);          // count = 3, fault
    ASSERT_EQ(faults.size(), 1u);
    EXPECT_EQ(faults[0].type, FaultType::MotorStall);
}

TEST(SafetyMonitor, NoMotorFaultWhenDivergenceBelowThreshold) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(100.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(395.0));

    ControlTargets targets;
    targets.lastCommandedRpm.store(400.0);  // divergence = 5, below 200 threshold

    SafetyMonitor sm(&hal, &targets, nullptr, makeConfig());
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(sm.checkOnce(kNowUs).empty());
    }
}

// ── AC-5.4: Control loop timing violation ────────────────────────────────────

TEST(SafetyMonitor, NoTimingFaultWhenNotInjecting) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(100.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    targets.injecting.store(false);
    // Set lastTickTimestampUs to far in the past
    targets.lastTickTimestampUs.store(kNowUs - 100'000);

    SafetyMonitor sm(&hal, &targets, nullptr, makeConfig());
    // Even though tick is late, we're not injecting → no timing fault
    auto faults = sm.checkOnce(kNowUs);
    EXPECT_TRUE(faults.empty());
}

TEST(SafetyMonitor, DetectsTimingViolationWhileInjecting) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(100.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    auto cfg = makeConfig();
    // controlLoopTickRateMs=2, jitterToleranceMs=3 → max allowed = 5ms
    cfg.controlLoopTickRateMs = 2;
    cfg.jitterToleranceMs = 3.0;

    ControlTargets targets;
    targets.injecting.store(true);
    // Last tick was 10 ms ago (well beyond the 5ms tolerance)
    targets.lastTickTimestampUs.store(kNowUs - 10'000);

    SafetyMonitor sm(&hal, &targets, nullptr, cfg);
    auto faults = sm.checkOnce(kNowUs);

    ASSERT_EQ(faults.size(), 1u);
    EXPECT_EQ(faults[0].type, FaultType::TimingDelay);
    EXPECT_GT(faults[0].measuredValue, 4.0);  // elapsed > max
}

TEST(SafetyMonitor, NoTimingFaultWithinTolerance) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(100.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    auto cfg = makeConfig();
    cfg.controlLoopTickRateMs = 2;
    cfg.jitterToleranceMs = 3.0;  // max = 2+3 = 5ms

    ControlTargets targets;
    targets.injecting.store(true);
    targets.lastTickTimestampUs.store(kNowUs - 3'000);  // 3ms ago, within 5ms

    SafetyMonitor sm(&hal, &targets, nullptr, cfg);
    auto faults = sm.checkOnce(kNowUs);
    EXPECT_TRUE(faults.empty());
}

// ── FaultQueue integration ───────────────────────────────────────────────────

TEST(SafetyMonitor, FaultQueueReceivesFaultEvent) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(500.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));
    EXPECT_CALL(hal, emergencyStop()).Times(1);

    ControlTargets targets;
    FaultQueue faultQueue;
    auto cfg = makeConfig();

    SafetyMonitor sm(&hal, &targets, &faultQueue, cfg);
    sm.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    sm.stop();

    EXPECT_FALSE(faultQueue.empty());
    auto fault = faultQueue.pop();
    ASSERT_TRUE(fault.has_value());
    EXPECT_EQ(fault->type, FaultType::Overpressure);
}

// ── Multiple simultaneous faults ─────────────────────────────────────────────

TEST(SafetyMonitor, MultipleSimultaneousFaults) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(400.0));   // overpressure
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(true)); // air bubble
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    SafetyMonitor sm(&hal, &targets, nullptr, makeConfig());

    auto faults = sm.checkOnce(kNowUs);
    ASSERT_GE(faults.size(), 2u);

    bool hasOverpressure = false;
    bool hasAirBubble = false;
    for (const auto& f : faults) {
        if (f.type == FaultType::Overpressure) hasOverpressure = true;
        if (f.type == FaultType::AirBubble) hasAirBubble = true;
    }
    EXPECT_TRUE(hasOverpressure);
    EXPECT_TRUE(hasAirBubble);
}

// ── resetCounters ─────────────────────────────────────────────────────────────

TEST(SafetyMonitor, ResetCountersClearsMotorDivergenceStreak) {
    MockHal hal;
    EXPECT_CALL(hal, readPressure()).WillRepeatedly(Return(100.0));
    EXPECT_CALL(hal, readAirDetector()).WillRepeatedly(Return(false));
    EXPECT_CALL(hal, readMotorRpm()).WillRepeatedly(Return(0.0));

    ControlTargets targets;
    targets.lastCommandedRpm.store(400.0);

    auto cfg = makeConfig();
    cfg.motorDivergenceTicks = 3;
    SafetyMonitor sm(&hal, &targets, nullptr, cfg);

    // Build streak to 2
    (void)sm.checkOnce(kNowUs);
    (void)sm.checkOnce(kNowUs);

    // Reset — streak cleared
    sm.resetCounters();

    // Need another N ticks to trigger fault
    EXPECT_TRUE(sm.checkOnce(kNowUs).empty());  // count = 1
    EXPECT_TRUE(sm.checkOnce(kNowUs).empty());  // count = 2
    auto faults = sm.checkOnce(kNowUs);         // count = 3
    ASSERT_EQ(faults.size(), 1u);
}
