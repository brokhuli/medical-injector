#include "hal/SyringeModel.h"

#include <gtest/gtest.h>

using namespace injector::hal;

class SyringeModelTest : public ::testing::Test {
protected:
    SyringeModel syringe{100.0, 50.0};
    static constexpr double kDt = 0.002;  // 2ms tick
};

TEST_F(SyringeModelTest, StartsAtFullCapacity) {
    EXPECT_DOUBLE_EQ(syringe.contrastRemaining(), 100.0);
    EXPECT_DOUBLE_EQ(syringe.salineRemaining(), 50.0);
}

TEST_F(SyringeModelTest, DrainContrast) {
    // 4 mL/s * 0.002s = 0.008 mL per tick
    syringe.drain(FluidChannel::Contrast, 4.0, kDt);
    EXPECT_NEAR(syringe.contrastRemaining(), 100.0 - 0.008, 1e-10);
    EXPECT_DOUBLE_EQ(syringe.salineRemaining(), 50.0);
}

TEST_F(SyringeModelTest, DrainSaline) {
    syringe.drain(FluidChannel::Saline, 2.0, kDt);
    EXPECT_NEAR(syringe.salineRemaining(), 50.0 - 0.004, 1e-10);
    EXPECT_DOUBLE_EQ(syringe.contrastRemaining(), 100.0);
}

TEST_F(SyringeModelTest, VolumeTracksOverTime) {
    // Drain 4 mL/s for 10 seconds = 40 mL
    for (int i = 0; i < 5000; ++i) {
        syringe.drain(FluidChannel::Contrast, 4.0, kDt);
    }
    EXPECT_NEAR(syringe.contrastRemaining(), 60.0, 0.01);
}

TEST_F(SyringeModelTest, DoesNotGoBelowZero) {
    // Drain 100 mL/s for 2 seconds (way more than 100 mL capacity)
    for (int i = 0; i < 1000; ++i) {
        syringe.drain(FluidChannel::Contrast, 100.0, kDt);
    }
    EXPECT_DOUBLE_EQ(syringe.contrastRemaining(), 0.0);
}

TEST_F(SyringeModelTest, ResetToFull) {
    syringe.drain(FluidChannel::Contrast, 4.0, 10.0);  // drain a lot
    syringe.drain(FluidChannel::Saline, 4.0, 10.0);
    syringe.resetToFull();
    EXPECT_DOUBLE_EQ(syringe.contrastRemaining(), 100.0);
    EXPECT_DOUBLE_EQ(syringe.salineRemaining(), 50.0);
}

TEST_F(SyringeModelTest, CustomCapacities) {
    SyringeModel custom{200.0, 75.0};
    EXPECT_DOUBLE_EQ(custom.contrastRemaining(), 200.0);
    EXPECT_DOUBLE_EQ(custom.salineRemaining(), 75.0);
    EXPECT_DOUBLE_EQ(custom.contrastCapacity(), 200.0);
    EXPECT_DOUBLE_EQ(custom.salineCapacity(), 75.0);
}
