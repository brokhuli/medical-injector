#include "hal/PressureModel.h"

#include <gtest/gtest.h>

using namespace injector::hal;

class PressureModelTest : public ::testing::Test {
protected:
    // Default: 50 psi/(mL/s) resistance, 10 psi baseline
    PressureModel pressure{50.0, 10.0};
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
