#pragma once

#include "hal/IHalInterface.h"

#include <gmock/gmock.h>

namespace injector::testing {

class MockHal : public hal::IHalInterface {
public:
    MOCK_METHOD(double, readPressure, (), (const, override));
    MOCK_METHOD(double, readMotorRpm, (), (const, override));
    MOCK_METHOD(bool, readAirDetector, (), (const, override));
    MOCK_METHOD(double, readSyringeVolume, (hal::Barrel barrel), (const, override));
    MOCK_METHOD(void, setMotorRpm, (double rpm), (override));
    MOCK_METHOD(void, setValve, (hal::FluidChannel channel, hal::ValveState state), (override));
    MOCK_METHOD(void, emergencyStop, (), (override));
    MOCK_METHOD(void, tick, (double dt), (override));
    MOCK_METHOD(void, injectFault, (const hal::SimulatedFault& fault), (override));
    MOCK_METHOD(void, clearFaults, (), (override));
};

}  // namespace injector::testing
