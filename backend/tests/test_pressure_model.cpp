#include "hal/PressureModel.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace injector::hal;

class PressureModelTest : public ::testing::Test {
protected:
    // Default: 50 psi/(mL/s) resistance, 10 psi baseline, 400 ms compliance tau
    PressureModel pressure{50.0, 10.0, 400.0};
};

TEST_F(PressureModelTest, BaselinePressureAtZeroFlow) {
    EXPECT_DOUBLE_EQ(pressure.compute(0.0), 10.0);
}

TEST_F(PressureModelTest, LinearPressureWithFlow) {
    // 4 mL/s * 50 + 10 = 210 psi
    EXPECT_DOUBLE_EQ(pressure.compute(4.0), 210.0);
}

TEST_F(PressureModelTest, HighFlowPressure) {
    // 10 mL/s * 50 + 10 = 510 psi
    EXPECT_DOUBLE_EQ(pressure.compute(10.0), 510.0);
}

TEST_F(PressureModelTest, OverpressureFaultOverridesComputed) {
    pressure.setPressureOverride(500.0);
    EXPECT_DOUBLE_EQ(pressure.compute(0.0), 500.0);
    EXPECT_DOUBLE_EQ(pressure.compute(4.0), 500.0);
}

TEST_F(PressureModelTest, PartialOcclusionMultipliesResistance) {
    pressure.setResistanceMultiplier(3.0);
    // 4 mL/s * 50 * 3 + 10 = 610 psi
    EXPECT_DOUBLE_EQ(pressure.compute(4.0), 610.0);
}

TEST_F(PressureModelTest, ClearFaultsRestoresNormal) {
    pressure.setPressureOverride(500.0);
    pressure.setResistanceMultiplier(3.0);
    pressure.clearFaults();

    EXPECT_DOUBLE_EQ(pressure.compute(4.0), 210.0);
}

TEST_F(PressureModelTest, CustomParameters) {
    PressureModel custom{100.0, 5.0};
    // 2 mL/s * 100 + 5 = 205 psi
    EXPECT_DOUBLE_EQ(custom.compute(2.0), 205.0);
}

// ── step() / first-order lag tests ───────────────────────────────────────────

TEST_F(PressureModelTest, StepInitializesToBaseline) {
    EXPECT_NEAR(pressure.filteredPressure(), 10.0, 1e-9);
}

TEST_F(PressureModelTest, StepAtZeroFlowStaysAtBaseline) {
    for (int i = 0; i < 1000; ++i) {
        pressure.step(0.0, 0.002);
    }
    EXPECT_NEAR(pressure.filteredPressure(), 10.0, 1e-9);
}

TEST_F(PressureModelTest, StepApproachesTargetWithCorrectTimeConstant) {
    // 4 mL/s * 50 + 10 = 210 psi steady-state. Starting from 10, after one
    // time constant (400 ms) the filter should reach ~63% of the 200 psi
    // gap → ~136 psi.
    constexpr double kFlow = 4.0;
    constexpr double kDt = 0.002;
    // 400 ms / 2 ms = 200 ticks
    for (int i = 0; i < 200; ++i) {
        pressure.step(kFlow, kDt);
    }
    const double expected = 10.0 + (210.0 - 10.0) * (1.0 - std::exp(-1.0));
    EXPECT_NEAR(pressure.filteredPressure(), expected, 1.0);
}

TEST_F(PressureModelTest, StepReachesSteadyStateAfterManyTaus) {
    constexpr double kFlow = 4.0;
    constexpr double kDt = 0.002;
    // 5 time constants → > 99% of target
    for (int i = 0; i < 5 * 200; ++i) {
        pressure.step(kFlow, kDt);
    }
    EXPECT_NEAR(pressure.filteredPressure(), 210.0, 2.0);
}

TEST_F(PressureModelTest, StepFaultOverrideJumpsInstantly) {
    // Run to partial rise
    for (int i = 0; i < 100; ++i) {
        pressure.step(4.0, 0.002);
    }
    EXPECT_GT(pressure.filteredPressure(), 10.0);
    EXPECT_LT(pressure.filteredPressure(), 210.0);

    pressure.setPressureOverride(500.0);
    pressure.step(4.0, 0.002);
    EXPECT_DOUBLE_EQ(pressure.filteredPressure(), 500.0);
}

TEST_F(PressureModelTest, StepDecaysToBaselineWhenFlowStops) {
    // Drive to steady state
    for (int i = 0; i < 2000; ++i) {
        pressure.step(4.0, 0.002);
    }
    EXPECT_NEAR(pressure.filteredPressure(), 210.0, 2.0);

    // Remove flow; pressure should decay back toward baseline
    for (int i = 0; i < 2000; ++i) {
        pressure.step(0.0, 0.002);
    }
    EXPECT_NEAR(pressure.filteredPressure(), 10.0, 2.0);
}

TEST_F(PressureModelTest, StepClearFaultsResetsFilteredPressure) {
    pressure.setPressureOverride(500.0);
    pressure.step(0.0, 0.002);
    EXPECT_DOUBLE_EQ(pressure.filteredPressure(), 500.0);

    pressure.clearFaults();
    // After clearFaults filteredPressure is reseeded to baseline (compute(0))
    EXPECT_NEAR(pressure.filteredPressure(), 10.0, 1e-9);
}
