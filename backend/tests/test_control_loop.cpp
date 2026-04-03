#include "control/ControlLoop.h"
#include "hal/SimulatedHal.h"
#include "logging/RingBuffer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace injector::control;
using namespace injector::hal;
using namespace injector::logging;

namespace {

ControlLoopConfig makeTestConfig() {
    ControlLoopConfig cfg;
    cfg.tickRateMs = 2;
    cfg.pinCore = false;  // Don't pin in tests
    cfg.flowPerRpm = 0.01;
    cfg.pid.kp = 100.0;
    cfg.pid.ki = 50.0;
    cfg.pid.kd = 5.0;
    cfg.pid.iTermMax = 500.0;
    cfg.pid.maxRpm = 1500.0;
    cfg.pid.maxAcceleration = 10.0;
    return cfg;
}

}  // namespace

TEST(ControlLoopTest, StartsAndStops) {
    auto hal = std::make_shared<SimulatedHal>();
    ControlLoop loop(hal, makeTestConfig());

    EXPECT_FALSE(loop.running());
    loop.start();
    EXPECT_TRUE(loop.running());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();
    EXPECT_FALSE(loop.running());
}

TEST(ControlLoopTest, DoubleStartIsNoop) {
    auto hal = std::make_shared<SimulatedHal>();
    ControlLoop loop(hal, makeTestConfig());

    loop.start();
    loop.start();  // should not crash or create a second thread
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    loop.stop();
}

TEST(ControlLoopTest, DestructorStopsLoop) {
    auto hal = std::make_shared<SimulatedHal>();
    {
        ControlLoop loop(hal, makeTestConfig());
        loop.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Should not hang or crash — destructor calls stop()
}

TEST(ControlLoopTest, SetAndGetTargetFlowRate) {
    auto hal = std::make_shared<SimulatedHal>();
    ControlLoop loop(hal, makeTestConfig());

    EXPECT_DOUBLE_EQ(loop.targetFlowRate(), 0.0);
    loop.setTargetFlowRate(4.0);
    EXPECT_DOUBLE_EQ(loop.targetFlowRate(), 4.0);
}

TEST(ControlLoopTest, DeliversVolumeAtTargetFlow) {
    HalConfig halCfg;
    auto hal = std::make_shared<SimulatedHal>(halCfg);

    // Open contrast valve so flow actually happens
    hal->setValve(FluidChannel::Contrast, ValveState::Open);

    auto loopCfg = makeTestConfig();
    ControlLoop loop(hal, loopCfg);

    loop.setTargetFlowRate(4.0);
    loop.start();

    // Run for ~2 seconds
    std::this_thread::sleep_for(std::chrono::seconds(2));
    loop.stop();

    double vol = loop.cumulativeVolume();
    // At 4 mL/s for 2s ≈ 8 mL. Allow wide tolerance for ramp-up + timing
    EXPECT_GT(vol, 2.0) << "Volume too low: " << vol;
    EXPECT_LT(vol, 12.0) << "Volume too high: " << vol;
}

TEST(ControlLoopTest, TimingStatsReasonable) {
    auto hal = std::make_shared<SimulatedHal>();
    auto cfg = makeTestConfig();
    ControlLoop loop(hal, cfg);

    loop.setTargetFlowRate(0.0);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    loop.stop();

    auto stats = loop.timingStats();
    EXPECT_GT(stats.totalTicks, 100u) << "Expected at least 100 ticks in 500ms";
    EXPECT_GT(stats.meanTickMs, 0.0);
    EXPECT_LT(stats.meanTickMs, 10.0)
        << "Mean tick too high: " << stats.meanTickMs;
}

TEST(ControlLoopTest, PushesTickDataToRingBuffer) {
    auto hal = std::make_shared<SimulatedHal>();
    hal->setValve(FluidChannel::Contrast, ValveState::Open);

    RingBuffer<TickData> buffer(4096);
    auto cfg = makeTestConfig();
    ControlLoop loop(hal, cfg, &buffer);

    loop.setTargetFlowRate(2.0);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop.stop();

    EXPECT_GT(buffer.totalPushed(), 50u)
        << "Expected tick data in ring buffer";

    // Verify a sample entry
    auto snap = buffer.snapshot();
    ASSERT_FALSE(snap.empty());

    auto& last = snap.back();
    EXPECT_GT(last.timestampUs, 0u);
    EXPECT_GE(last.pressure, 0.0);
}

TEST(ControlLoopTest, ZeroTargetProducesNoVolume) {
    auto hal = std::make_shared<SimulatedHal>();
    auto cfg = makeTestConfig();
    ControlLoop loop(hal, cfg);

    loop.setTargetFlowRate(0.0);
    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop.stop();

    EXPECT_NEAR(loop.cumulativeVolume(), 0.0, 0.01);
}
