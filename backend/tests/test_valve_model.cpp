#include "hal/ValveModel.h"

#include <gtest/gtest.h>

using namespace injector::hal;

class ValveModelTest : public ::testing::Test {
protected:
    ValveModel valves;
};

TEST_F(ValveModelTest, BothClosedByDefault) {
    EXPECT_FALSE(valves.isOpen(FluidChannel::Contrast));
    EXPECT_FALSE(valves.isOpen(FluidChannel::Saline));
    EXPECT_EQ(valves.contrastValve(), ValveState::Closed);
    EXPECT_EQ(valves.salineValve(), ValveState::Closed);
}

TEST_F(ValveModelTest, OpenContrastValve) {
    valves.set(FluidChannel::Contrast, ValveState::Open);
    EXPECT_TRUE(valves.isOpen(FluidChannel::Contrast));
    EXPECT_FALSE(valves.isOpen(FluidChannel::Saline));
}

TEST_F(ValveModelTest, OpenSalineValve) {
    valves.set(FluidChannel::Saline, ValveState::Open);
    EXPECT_FALSE(valves.isOpen(FluidChannel::Contrast));
    EXPECT_TRUE(valves.isOpen(FluidChannel::Saline));
}

TEST_F(ValveModelTest, CloseAll) {
    valves.set(FluidChannel::Contrast, ValveState::Open);
    valves.set(FluidChannel::Saline, ValveState::Open);
    valves.closeAll();
    EXPECT_FALSE(valves.isOpen(FluidChannel::Contrast));
    EXPECT_FALSE(valves.isOpen(FluidChannel::Saline));
}

TEST_F(ValveModelTest, IndependentControl) {
    valves.set(FluidChannel::Contrast, ValveState::Open);
    valves.set(FluidChannel::Saline, ValveState::Open);
    EXPECT_TRUE(valves.isOpen(FluidChannel::Contrast));
    EXPECT_TRUE(valves.isOpen(FluidChannel::Saline));

    valves.set(FluidChannel::Contrast, ValveState::Closed);
    EXPECT_FALSE(valves.isOpen(FluidChannel::Contrast));
    EXPECT_TRUE(valves.isOpen(FluidChannel::Saline));
}
