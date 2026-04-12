#include "hal/SimulatedHal.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace injector::hal;

class SimulatedHalTest : public ::testing::Test {
protected:
    HalConfig config;
    static constexpr double kDt = 0.002;  // 2ms tick

    std::unique_ptr<SimulatedHal> hal;

    void SetUp() override {
        // Use a short pressure time constant so existing integration checks
        // that run for ~1 s reach the instantaneous target. The user-facing
        // default (400 ms) is exercised separately in test_pressure_model.
        config.pressureTimeConstantMs = 10.0;
        // Pin the tubing resistance so the per-fluid steady-state pressure
        // numbers below are stable regardless of the HalConfig defaults.
        config.contrastResistance = 50.0;
        config.salineResistance = 50.0;
        hal = std::make_unique<SimulatedHal>(config);
    }
};

TEST_F(SimulatedHalTest, InitialState) {
    EXPECT_DOUBLE_EQ(hal->readMotorRpm(), 0.0);
    EXPECT_NEAR(hal->readPressure(), 10.0, 0.01);  // baseline
    EXPECT_FALSE(hal->readAirDetector());
    EXPECT_DOUBLE_EQ(hal->readSyringeVolume(Barrel::Contrast), 100.0);
    EXPECT_DOUBLE_EQ(hal->readSyringeVolume(Barrel::Saline), 50.0);
}

TEST_F(SimulatedHalTest, TickSequencing_MotorDrivesFlow) {
    hal->setMotorRpm(400.0);
    hal->setValve(FluidChannel::Contrast, ValveState::Open);

    // Run to steady state
    for (int i = 0; i < 500; ++i) {
        hal->tick(kDt);
    }

    EXPECT_NEAR(hal->readMotorRpm(), 400.0, 1.0);
    // 400 RPM * 0.01 = 4.0 mL/s flow → pressure = 4*50+10 = 210 psi
    EXPECT_NEAR(hal->readPressure(), 210.0, 1.0);
    EXPECT_NEAR(hal->currentFlowRate(), 4.0, 0.01);
}

TEST_F(SimulatedHalTest, TickSequencing_SyringeDrains) {
    hal->setMotorRpm(400.0);
    hal->setValve(FluidChannel::Contrast, ValveState::Open);

    // Run for 5 seconds = 2500 ticks, drain 4 mL/s * 5s = 20 mL
    for (int i = 0; i < 2500; ++i) {
        hal->tick(kDt);
    }

    // Motor ramp-up takes ~150ms, so slightly less than 20 mL drained
    double remaining = hal->readSyringeVolume(Barrel::Contrast);
    EXPECT_GT(remaining, 79.0);
    EXPECT_LT(remaining, 81.0);
    // Saline untouched
    EXPECT_DOUBLE_EQ(hal->readSyringeVolume(Barrel::Saline), 50.0);
}

TEST_F(SimulatedHalTest, NoFlowWithClosedValves) {
    hal->setMotorRpm(400.0);
    // No valve opened

    for (int i = 0; i < 500; ++i) {
        hal->tick(kDt);
    }

    // Motor spins but no flow
    EXPECT_NEAR(hal->readMotorRpm(), 400.0, 1.0);
    EXPECT_NEAR(hal->currentFlowRate(), 0.0, 0.01);
    // Pressure at zero effective flow = baseline
    EXPECT_NEAR(hal->readPressure(), 10.0, 0.01);
    // No volume drained
    EXPECT_DOUBLE_EQ(hal->readSyringeVolume(Barrel::Contrast), 100.0);
}

TEST_F(SimulatedHalTest, SalineChannel) {
    hal->setMotorRpm(200.0);
    hal->setValve(FluidChannel::Saline, ValveState::Open);

    for (int i = 0; i < 500; ++i) {
        hal->tick(kDt);
    }

    EXPECT_LT(hal->readSyringeVolume(Barrel::Saline), 50.0);
    EXPECT_DOUBLE_EQ(hal->readSyringeVolume(Barrel::Contrast), 100.0);
}

TEST_F(SimulatedHalTest, EmergencyStop) {
    hal->setMotorRpm(400.0);
    hal->setValve(FluidChannel::Contrast, ValveState::Open);

    for (int i = 0; i < 500; ++i) {
        hal->tick(kDt);
    }
    EXPECT_GT(hal->readMotorRpm(), 300.0);

    hal->emergencyStop();
    EXPECT_DOUBLE_EQ(hal->readMotorRpm(), 0.0);

    // After emergency stop, flow is instantly zero but pressure decays
    // through the compliance lag. Run long enough (~20 tau) to reach baseline.
    for (int i = 0; i < 100; ++i) {
        hal->tick(kDt);
    }
    EXPECT_NEAR(hal->readPressure(), 10.0, 0.1);
    EXPECT_NEAR(hal->currentFlowRate(), 0.0, 0.01);
}

// ── Fault injection tests ─────────────────────────────────

TEST_F(SimulatedHalTest, FaultOverpressure) {
    SimulatedFault fault;
    fault.type = SimulatedFault::Type::Overpressure;
    fault.targetPsi = 500.0;

    hal->injectFault(fault);
    hal->tick(kDt);

    EXPECT_DOUBLE_EQ(hal->readPressure(), 500.0);
}

TEST_F(SimulatedHalTest, FaultAirBubble) {
    EXPECT_FALSE(hal->readAirDetector());

    SimulatedFault fault;
    fault.type = SimulatedFault::Type::AirBubble;
    hal->injectFault(fault);

    EXPECT_TRUE(hal->readAirDetector());
}

TEST_F(SimulatedHalTest, FaultMotorStall) {
    SimulatedFault fault;
    fault.type = SimulatedFault::Type::MotorStall;
    fault.maxRpm = 100.0;
    hal->injectFault(fault);

    hal->setMotorRpm(400.0);
    for (int i = 0; i < 500; ++i) {
        hal->tick(kDt);
    }

    EXPECT_LE(hal->readMotorRpm(), 100.0);
}

TEST_F(SimulatedHalTest, FaultPartialOcclusion) {
    hal->setMotorRpm(400.0);
    hal->setValve(FluidChannel::Contrast, ValveState::Open);

    SimulatedFault fault;
    fault.type = SimulatedFault::Type::PartialOcclusion;
    fault.resistanceMultiplier = 3.0;
    hal->injectFault(fault);

    for (int i = 0; i < 500; ++i) {
        hal->tick(kDt);
    }

    // 4 mL/s * 50 * 3 + 10 = 610 psi
    EXPECT_NEAR(hal->readPressure(), 610.0, 2.0);
}

TEST_F(SimulatedHalTest, ClearFaultsRestoresNormal) {
    // Inject multiple faults
    SimulatedFault overp;
    overp.type = SimulatedFault::Type::Overpressure;
    overp.targetPsi = 500.0;
    hal->injectFault(overp);

    SimulatedFault air;
    air.type = SimulatedFault::Type::AirBubble;
    hal->injectFault(air);

    EXPECT_TRUE(hal->readAirDetector());

    hal->clearFaults();
    hal->tick(kDt);

    EXPECT_FALSE(hal->readAirDetector());
    EXPECT_NEAR(hal->readPressure(), 10.0, 0.01);  // back to baseline
}

TEST_F(SimulatedHalTest, CustomConfig) {
    HalConfig custom;
    custom.contrastVolumeMl = 200.0;
    custom.salineVolumeMl = 75.0;
    custom.baselinePressure = 5.0;

    SimulatedHal customHal(custom);
    EXPECT_DOUBLE_EQ(customHal.readSyringeVolume(Barrel::Contrast), 200.0);
    EXPECT_DOUBLE_EQ(customHal.readSyringeVolume(Barrel::Saline), 75.0);
    EXPECT_NEAR(customHal.readPressure(), 5.0, 0.01);
}
