#include "control/PidController.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace injector::control;

namespace {
constexpr double DT = 0.002;  // 2ms tick
constexpr double FLOW_PER_RPM = 0.01;
constexpr double MOTOR_TAU_S = 0.050;  // 50ms time constant

// With default gains (Kp=100, Ki=50, Kd=5) and flowPerRpm=0.01,
// the proportional DC gain is 1.0 and integral time constant ~2s.
// Full convergence needs ~10s (5000 ticks) for the integral to
// fill the steady-state error gap.
constexpr int CONVERGE_TICKS = 10000;

// Simulate one tick: PID → motor lag → flow
void simTick(PidController& pid, double target, double& actualRpm,
             double& flow) {
    double commandedRpm = pid.compute(target, flow, DT);
    actualRpm += (commandedRpm - actualRpm) * (DT / MOTOR_TAU_S);
    flow = actualRpm * FLOW_PER_RPM;
}
}  // namespace

TEST(PidControllerTest, DefaultConstruction) {
    PidController pid;
    EXPECT_DOUBLE_EQ(pid.rampedTarget(), 0.0);
    EXPECT_DOUBLE_EQ(pid.lastOutput(), 0.0);
}

TEST(PidControllerTest, ZeroTargetProducesZeroOutput) {
    PidController pid;
    double output = pid.compute(0.0, 0.0, DT);
    EXPECT_DOUBLE_EQ(output, 0.0);
}

// AC-3.1: Steady-state accuracy — mean 4.0 ± 2% after convergence
TEST(PidControllerTest, SteadyStateAccuracy) {
    PidController pid;

    constexpr double TARGET = 4.0;
    double actualRpm = 0.0;
    double flow = 0.0;

    // Let integral converge fully
    for (int i = 0; i < CONVERGE_TICKS; ++i) {
        simTick(pid, TARGET, actualRpm, flow);
    }

    // Measure mean over 500 ticks at steady state
    double flowSum = 0.0;
    for (int i = 0; i < 500; ++i) {
        simTick(pid, TARGET, actualRpm, flow);
        flowSum += flow;
    }

    double meanFlow = flowSum / 500;
    EXPECT_NEAR(meanFlow, TARGET, TARGET * 0.02)
        << "Mean flow " << meanFlow << " not within 2% of " << TARGET;
}

// AC-3.3: Acceleration ramp — flow rate derivative ≤ maxAcceleration
TEST(PidControllerTest, AccelerationRamp) {
    PidConfig cfg;
    cfg.maxAcceleration = 10.0;
    PidController pid(cfg);

    double prevRamped = 0.0;
    constexpr double TARGET = 4.0;

    for (int i = 0; i < 500; ++i) {
        (void)pid.compute(TARGET, 0.0, DT);
        double ramped = pid.rampedTarget();
        double rateOfChange = (ramped - prevRamped) / DT;

        EXPECT_LE(rateOfChange, cfg.maxAcceleration + 1e-9)
            << "Ramp rate exceeded at tick " << i;
        EXPECT_GE(rateOfChange, -cfg.maxAcceleration - 1e-9);

        prevRamped = ramped;
    }
}

// AC-3.4: No oscillation — stays within 5% after convergence
TEST(PidControllerTest, SettlesWithoutOscillation) {
    PidController pid;

    constexpr double TARGET = 4.0;
    double actualRpm = 0.0;
    double flow = 0.0;

    for (int i = 0; i < CONVERGE_TICKS; ++i) {
        simTick(pid, TARGET, actualRpm, flow);
    }

    // Verify it stays within 5% for 500 ticks (no oscillation)
    for (int i = 0; i < 500; ++i) {
        simTick(pid, TARGET, actualRpm, flow);
        EXPECT_NEAR(flow, TARGET, TARGET * 0.05)
            << "Oscillation detected at tick " << i;
    }
}

TEST(PidControllerTest, AntiWindup) {
    PidConfig cfg;
    cfg.iTermMax = 100.0;
    PidController pid(cfg);

    double actualRpm = 0.0;
    double flow = 0.0;

    // Drive with large error to saturate I term
    for (int i = 0; i < 2000; ++i) {
        simTick(pid, 10.0, actualRpm, flow);
    }

    // Set target to 0 — should recover
    bool recovered = false;
    for (int i = 0; i < 5000; ++i) {
        simTick(pid, 0.0, actualRpm, flow);
        if (flow < 0.5) {
            recovered = true;
            break;
        }
    }
    EXPECT_TRUE(recovered) << "Anti-windup failed: PID did not recover to 0";
}

TEST(PidControllerTest, OutputClampedToMaxRpm) {
    PidConfig cfg;
    cfg.maxRpm = 500.0;
    cfg.maxAcceleration = 1000.0;
    PidController pid(cfg);

    double output = pid.compute(100.0, 0.0, DT);
    EXPECT_LE(output, 500.0);
}

TEST(PidControllerTest, OutputClampedToZeroFloor) {
    PidConfig cfg;
    PidController pid(cfg);

    double output = pid.compute(0.0, 10.0, DT);
    EXPECT_GE(output, 0.0);
}

TEST(PidControllerTest, Reset) {
    PidController pid;

    (void)pid.compute(4.0, 0.0, DT);
    (void)pid.compute(4.0, 1.0, DT);
    EXPECT_NE(pid.rampedTarget(), 0.0);

    pid.reset();
    EXPECT_DOUBLE_EQ(pid.rampedTarget(), 0.0);
    EXPECT_DOUBLE_EQ(pid.lastOutput(), 0.0);
}

// Pressure-aware decel scaling helper.
TEST(PressureAwareDecel, BelowThresholdReturnsBaseRate) {
    // pressure = 50% of limit, threshold = 80% → no scaling
    double r = pressureAwareDecelRate(10.0, 125.0, 250.0, 0.80, 2.0);
    EXPECT_DOUBLE_EQ(r, 10.0);
}

TEST(PressureAwareDecel, AtLimitScalesByOnePlusGain) {
    // pressure = limit → scale = 1 + gain
    double r = pressureAwareDecelRate(10.0, 250.0, 250.0, 0.80, 2.0);
    EXPECT_DOUBLE_EQ(r, 30.0);
}

TEST(PressureAwareDecel, GainZeroCollapsesToFixedRate) {
    // Any pressure, gain=0 → returns baseRate
    EXPECT_DOUBLE_EQ(pressureAwareDecelRate(10.0, 300.0, 250.0, 0.80, 0.0), 10.0);
    EXPECT_DOUBLE_EQ(pressureAwareDecelRate(10.0, 0.0, 250.0, 0.80, 0.0), 10.0);
}

TEST(PressureAwareDecel, HalfwayBetweenThresholdAndLimit) {
    // threshold=0.8, ratio=0.9 → headroom = (0.9-0.8)/0.2 = 0.5
    // scale = 1 + 2*0.5 = 2.0 → 10 * 2 = 20
    double r = pressureAwareDecelRate(10.0, 225.0, 250.0, 0.80, 2.0);
    EXPECT_DOUBLE_EQ(r, 20.0);
}

TEST(PressureAwareDecel, InvalidLimitReturnsBaseRate) {
    EXPECT_DOUBLE_EQ(pressureAwareDecelRate(10.0, 100.0, 0.0, 0.80, 2.0), 10.0);
    EXPECT_DOUBLE_EQ(pressureAwareDecelRate(10.0, 100.0, -1.0, 0.80, 2.0), 10.0);
}

TEST(PressureAwareDecel, ThresholdOneReturnsBaseRate) {
    // Degenerate: threshold=1 means "never scale"
    EXPECT_DOUBLE_EQ(pressureAwareDecelRate(10.0, 250.0, 250.0, 1.0, 2.0), 10.0);
}

// maxAccelOverride replaces config.maxAcceleration for a single tick's ramp
// limiter (used by the pressure-aware decel path in ControlLoop).
TEST(PidControllerTest, MaxAccelOverrideScalesRamp) {
    PidConfig cfg;
    cfg.maxAcceleration = 10.0;
    PidController pid(cfg);

    // Warm up ramped target to 4.0 mL/s using the config rate so we're
    // comparing the downward ramp from the same starting point.
    for (int i = 0; i < 500; ++i) {
        (void)pid.compute(4.0, 4.0, DT);
    }
    double start = pid.rampedTarget();
    EXPECT_NEAR(start, 4.0, 1e-6);

    // One tick of downward ramp with a 3x override should move the ramped
    // target 3x as far as the config rate would.
    (void)pid.compute(0.0, 4.0, DT, /*override=*/30.0);
    double deltaOverride = start - pid.rampedTarget();
    EXPECT_NEAR(deltaOverride, 30.0 * DT, 1e-9);

    // Same setup, no override → moves at config rate.
    PidController pid2(cfg);
    for (int i = 0; i < 500; ++i) {
        (void)pid2.compute(4.0, 4.0, DT);
    }
    (void)pid2.compute(0.0, 4.0, DT);
    double deltaConfig = 4.0 - pid2.rampedTarget();
    EXPECT_NEAR(deltaConfig, 10.0 * DT, 1e-9);
}

TEST(PidControllerTest, MaxAccelOverrideNegativeUsesConfig) {
    PidConfig cfg;
    cfg.maxAcceleration = 10.0;
    PidController pid(cfg);

    // A negative override must be ignored; ramp uses config rate.
    (void)pid.compute(4.0, 0.0, DT, -1.0);
    EXPECT_NEAR(pid.rampedTarget(), 10.0 * DT, 1e-9);
}

TEST(PidControllerTest, ZeroDtReturnsLastOutput) {
    PidController pid;
    (void)pid.compute(4.0, 0.0, DT);
    double last = pid.lastOutput();
    double output = pid.compute(4.0, 0.0, 0.0);
    EXPECT_DOUBLE_EQ(output, last);
}

// Verify convergence at multiple flow targets
TEST(PidControllerTest, ConvergesAtDifferentTargets) {
    for (double target : {1.0, 2.0, 4.0, 5.0}) {
        PidController pid;
        double actualRpm = 0.0;
        double flow = 0.0;

        for (int i = 0; i < CONVERGE_TICKS; ++i) {
            simTick(pid, target, actualRpm, flow);
        }

        EXPECT_NEAR(flow, target, target * 0.02)
            << "Failed to converge at target " << target;
    }
}
