#include "hal/MotorModel.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace injector::hal;

class MotorModelTest : public ::testing::Test {
protected:
    // Default params: 50ms time constant, 0.01 mL/s per RPM, 1500 max RPM
    MotorModel motor{50.0, 0.01, 1500.0};
    static constexpr double kDt = 0.002;  // 2ms tick
};

TEST_F(MotorModelTest, StartsAtZero) {
    EXPECT_DOUBLE_EQ(motor.actualRpm(), 0.0);
    EXPECT_DOUBLE_EQ(motor.commandedRpm(), 0.0);
    EXPECT_DOUBLE_EQ(motor.flowRate(), 0.0);
}

TEST_F(MotorModelTest, ApproachesCommandedRpm) {
    motor.setCommandedRpm(400.0);

    // Run for 150ms (3 time constants) — should reach ~95% of target
    for (int i = 0; i < 75; ++i) {
        motor.tick(kDt);
    }

    EXPECT_NEAR(motor.actualRpm(), 400.0, 400.0 * 0.06);  // within 6%
}

TEST_F(MotorModelTest, FirstOrderLagOneTimeConstant) {
    motor.setCommandedRpm(1000.0);

    // Run for exactly one time constant (50ms = 25 ticks at 2ms)
    for (int i = 0; i < 25; ++i) {
        motor.tick(kDt);
    }

    // Should reach ~63.2% of target
    double expected = 1000.0 * (1.0 - std::exp(-1.0));
    EXPECT_NEAR(motor.actualRpm(), expected, 1000.0 * 0.02);
}

TEST_F(MotorModelTest, FlowRateProportionalToRpm) {
    motor.setCommandedRpm(400.0);

    // Run to steady state
    for (int i = 0; i < 500; ++i) {
        motor.tick(kDt);
    }

    EXPECT_NEAR(motor.flowRate(), 4.0, 0.01);  // 400 RPM * 0.01 = 4.0 mL/s
}

TEST_F(MotorModelTest, DeceleratesToStop) {
    motor.setCommandedRpm(400.0);
    for (int i = 0; i < 500; ++i) {
        motor.tick(kDt);
    }
    EXPECT_NEAR(motor.actualRpm(), 400.0, 0.1);

    motor.setCommandedRpm(0.0);
    for (int i = 0; i < 500; ++i) {
        motor.tick(kDt);
    }
    EXPECT_NEAR(motor.actualRpm(), 0.0, 0.1);
}

TEST_F(MotorModelTest, EmergencyStopInstant) {
    motor.setCommandedRpm(400.0);
    for (int i = 0; i < 500; ++i) {
        motor.tick(kDt);
    }
    EXPECT_GT(motor.actualRpm(), 300.0);

    motor.emergencyStop();
    EXPECT_DOUBLE_EQ(motor.actualRpm(), 0.0);
    EXPECT_DOUBLE_EQ(motor.commandedRpm(), 0.0);
}

TEST_F(MotorModelTest, ClampsToMaxRpm) {
    motor.setCommandedRpm(2000.0);  // above 1500 max
    EXPECT_DOUBLE_EQ(motor.commandedRpm(), 1500.0);
}

TEST_F(MotorModelTest, ClampsToMinRpm) {
    motor.setCommandedRpm(-100.0);
    EXPECT_DOUBLE_EQ(motor.commandedRpm(), 0.0);
}

TEST_F(MotorModelTest, MotorStallFault) {
    motor.setCommandedRpm(400.0);
    motor.setMaxRpmOverride(100.0);

    for (int i = 0; i < 500; ++i) {
        motor.tick(kDt);
    }

    EXPECT_LE(motor.actualRpm(), 100.0);
}

TEST_F(MotorModelTest, ClearFaultsRestoresNormal) {
    motor.setCommandedRpm(400.0);
    motor.setMaxRpmOverride(100.0);

    for (int i = 0; i < 500; ++i) {
        motor.tick(kDt);
    }
    EXPECT_LE(motor.actualRpm(), 100.0);

    motor.clearFaults();
    for (int i = 0; i < 500; ++i) {
        motor.tick(kDt);
    }
    EXPECT_NEAR(motor.actualRpm(), 400.0, 0.1);
}
