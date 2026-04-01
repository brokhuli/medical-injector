#include "hal/AirDetectorModel.h"

#include <gtest/gtest.h>

using namespace injector::hal;

TEST(AirDetectorModelTest, DefaultNoAir) {
    AirDetectorModel detector;
    EXPECT_FALSE(detector.airPresent());
}

TEST(AirDetectorModelTest, SetAirPresent) {
    AirDetectorModel detector;
    detector.setAirPresent(true);
    EXPECT_TRUE(detector.airPresent());
}

TEST(AirDetectorModelTest, ClearAir) {
    AirDetectorModel detector;
    detector.setAirPresent(true);
    EXPECT_TRUE(detector.airPresent());
    detector.setAirPresent(false);
    EXPECT_FALSE(detector.airPresent());
}

TEST(AirDetectorModelTest, PersistsUntilCleared) {
    AirDetectorModel detector;
    detector.setAirPresent(true);

    // Multiple reads should all return true
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(detector.airPresent());
    }
}
