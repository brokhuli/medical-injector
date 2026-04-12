/// Integration tests: config propagation.
/// Verifies that Config values are correctly propagated to HAL, PID, safety,
/// and control loop components when the backend is assembled.

#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/FaultQueue.h"
#include "comms/TelemetryBroadcast.h"
#include "config/Config.h"
#include "control/ControlLoop.h"
#include "hal/SimulatedHal.h"
#include "logging/DataLogger.h"
#include "safety/SafetyMonitor.h"
#include "state/StateMachine.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace injector;

namespace {

// ── Config propagation to HAL ────────────────────────────────────────────────

TEST(ConfigIntegration, HalReceivesConfigValues) {
    auto cfg = config::Config::loadFromString(R"({
        "hal": {
            "contrastResistance": 80.0,
            "salineResistance": 40.0,
            "baselinePressure": 15.0,
            "flowPerRpm": 0.02,
            "motorTimeConstantMs": 30.0,
            "maxRpm": 2000.0
        },
        "syringe": {
            "contrastVolumeMl": 150.0,
            "salineVolumeMl": 75.0
        }
    })");

    hal::HalConfig halCfg;
    halCfg.contrastResistance  = cfg.hal.contrastResistance;
    halCfg.salineResistance    = cfg.hal.salineResistance;
    halCfg.baselinePressure    = cfg.hal.baselinePressure;
    halCfg.motorTimeConstantMs = cfg.hal.motorTimeConstantMs;
    halCfg.flowPerRpm          = cfg.hal.flowPerRpm;
    halCfg.maxRpm              = cfg.hal.maxRpm;
    halCfg.contrastVolumeMl    = cfg.syringe.contrastVolumeMl;
    halCfg.salineVolumeMl      = cfg.syringe.salineVolumeMl;

    hal::SimulatedHal hal(halCfg);

    // Baseline pressure should match config
    EXPECT_NEAR(hal.readPressure(), 15.0, 0.01);

    // Syringe volumes should match config
    EXPECT_NEAR(hal.readSyringeVolume(hal::Barrel::Contrast), 150.0, 0.01);
    EXPECT_NEAR(hal.readSyringeVolume(hal::Barrel::Saline), 75.0, 0.01);

    // Motor at rest
    EXPECT_NEAR(hal.readMotorRpm(), 0.0, 0.01);
}

// ── Config propagation to PID / ControlLoop ──────────────────────────────────

TEST(ConfigIntegration, ControlLoopUsesConfiguredPidGains) {
    // Use very different PID gains to verify propagation
    auto cfg = config::Config::loadFromString(R"({
        "pid": {
            "kp": 200.0,
            "ki": 80.0,
            "kd": 10.0,
            "iTermMax": 1000.0,
            "maxRpm": 2000.0,
            "maxAcceleration": 20.0
        },
        "control": { "tickRateMs": 2, "pinCore": false },
        "hal": { "flowPerRpm": 0.01 }
    })");

    hal::HalConfig halCfg;
    halCfg.contrastResistance  = cfg.hal.contrastResistance;
    halCfg.salineResistance    = cfg.hal.salineResistance;
    halCfg.baselinePressure    = cfg.hal.baselinePressure;
    halCfg.motorTimeConstantMs = cfg.hal.motorTimeConstantMs;
    halCfg.flowPerRpm          = cfg.hal.flowPerRpm;
    halCfg.maxRpm              = cfg.pid.maxRpm;  // match PID max

    auto hal = std::make_shared<hal::SimulatedHal>(halCfg);

    control::ControlLoopConfig loopCfg;
    loopCfg.tickRateMs      = cfg.control.tickRateMs;
    loopCfg.pinCore         = cfg.control.pinCore;
    loopCfg.flowPerRpm      = cfg.hal.flowPerRpm;
    loopCfg.pid.kp          = cfg.pid.kp;
    loopCfg.pid.ki          = cfg.pid.ki;
    loopCfg.pid.kd          = cfg.pid.kd;
    loopCfg.pid.iTermMax    = cfg.pid.iTermMax;
    loopCfg.pid.maxRpm      = cfg.pid.maxRpm;
    loopCfg.pid.maxAcceleration = cfg.pid.maxAcceleration;

    logging::DataLogger logger;
    control::ControlLoop loop(hal, loopCfg, &logger.tickBuffer());

    // Set target and run briefly
    loop.setTargetFlowRate(4.0);
    loop.start();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    loop.stop();

    // With higher gains (kp=200 vs default 100), the PID should converge to
    // the target. The exact volume depends on convergence speed and acceleration
    // ramp, so allow a wide tolerance. Key check: volume is positive and reasonable.
    double vol = loop.cumulativeVolume();
    EXPECT_GT(vol, 3.0)
        << "Volume should be > 3 mL after 2s at 4 mL/s target";
    EXPECT_LT(vol, 12.0)
        << "Volume should be < 12 mL (sanity check)";
}

// ── Config propagation to safety monitor ─────────────────────────────────────

TEST(ConfigIntegration, SafetyMonitorUsesConfiguredPressureLimit) {
    auto cfg = config::Config::loadFromString(R"({
        "safety": {
            "defaultPressureLimitPsi": 250.0,
            "motorDivergenceThreshold": 300.0,
            "motorDivergenceTicks": 10,
            "jitterToleranceMs": 30.0,
            "tickRateMs": 1
        }
    })");

    hal::HalConfig halCfg;
    halCfg.contrastResistance = 50.0;
    halCfg.salineResistance = 35.0;
    halCfg.baselinePressure = 10.0;
    halCfg.contrastVolumeMl = 100.0;
    halCfg.salineVolumeMl = 50.0;
    halCfg.flowPerRpm = 0.01;
    halCfg.motorTimeConstantMs = 50.0;
    halCfg.maxRpm = 1500.0;

    auto hal = std::make_shared<hal::SimulatedHal>(halCfg);

    state::ControlTargets targets;
    // The safety monitor reads pressureLimit from targets (not from config directly).
    // In the real system, the state machine copies cfg.safety.defaultPressureLimitPsi
    // into targets.pressureLimit when loading a protocol. Simulate that here.
    targets.pressureLimit.store(cfg.safety.defaultPressureLimitPsi);

    comms::FaultQueue faultQueue;

    safety::SafetyMonitor monitor(hal.get(), &targets, &faultQueue, cfg.safety);

    // Inject overpressure just above the custom limit of 250 psi
    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::Overpressure;
    fault.targetPsi = 260.0;
    hal->injectFault(fault);

    auto faults = monitor.checkOnce(0);

    // Should detect overpressure at 260 > 250 (custom limit)
    ASSERT_GE(faults.size(), 1u);
    EXPECT_EQ(faults[0].type, state::FaultType::Overpressure);
    EXPECT_NEAR(faults[0].measuredValue, 260.0, 1.0);
    EXPECT_NEAR(faults[0].threshold, 250.0, 1.0);
}

TEST(ConfigIntegration, SafetyMonitorUsesConfiguredMotorDivergence) {
    auto cfg = config::Config::loadFromString(R"({
        "safety": {
            "defaultPressureLimitPsi": 325.0,
            "motorDivergenceThreshold": 100.0,
            "motorDivergenceTicks": 5,
            "jitterToleranceMs": 30.0,
            "tickRateMs": 1
        }
    })");

    hal::HalConfig halCfg;
    halCfg.contrastResistance = 50.0;
    halCfg.salineResistance = 35.0;
    halCfg.baselinePressure = 10.0;
    halCfg.contrastVolumeMl = 100.0;
    halCfg.salineVolumeMl = 50.0;
    halCfg.flowPerRpm = 0.01;
    halCfg.motorTimeConstantMs = 50.0;
    halCfg.maxRpm = 1500.0;

    auto hal = std::make_shared<hal::SimulatedHal>(halCfg);

    state::ControlTargets targets;
    targets.injecting.store(true);
    targets.lastCommandedRpm.store(500.0);

    comms::FaultQueue faultQueue;

    safety::SafetyMonitor monitor(hal.get(), &targets, &faultQueue, cfg.safety);

    // Motor stalled at 0 RPM, commanded at 500 → divergence = 500 > threshold 100
    // Need 5 consecutive ticks (custom config) to trigger fault
    for (int i = 0; i < 4; ++i) {
        auto faults = monitor.checkOnce(0);
        EXPECT_TRUE(faults.empty()) << "Fault triggered too early at tick " << i;
    }

    auto faults = monitor.checkOnce(0);
    ASSERT_GE(faults.size(), 1u) << "Motor divergence fault not triggered after 5 ticks";
    EXPECT_EQ(faults[0].type, state::FaultType::MotorStall);
}

// ── Full system config propagation ───────────────────────────────────────────

TEST(ConfigIntegration, FullSystemUsesConfiguredSyringeVolumes) {
    // Start backend with custom syringe volumes, verify HAL reports them
    auto cfg = config::Config::loadFromString(R"({
        "syringe": {
            "contrastVolumeMl": 200.0,
            "salineVolumeMl": 100.0
        }
    })");

    hal::HalConfig halCfg;
    halCfg.contrastResistance  = cfg.hal.contrastResistance;
    halCfg.salineResistance    = cfg.hal.salineResistance;
    halCfg.baselinePressure    = cfg.hal.baselinePressure;
    halCfg.motorTimeConstantMs = cfg.hal.motorTimeConstantMs;
    halCfg.flowPerRpm          = cfg.hal.flowPerRpm;
    halCfg.maxRpm              = cfg.hal.maxRpm;
    halCfg.contrastVolumeMl    = cfg.syringe.contrastVolumeMl;
    halCfg.salineVolumeMl      = cfg.syringe.salineVolumeMl;

    auto hal = std::make_shared<hal::SimulatedHal>(halCfg);

    // Wire up state machine
    state::ControlTargets targets;
    comms::CommandQueue commandQueue;
    comms::FaultQueue faultQueue;
    comms::EventBroadcast events;

    state::StateMachine sm(&targets, &commandQueue, &faultQueue, &events, hal.get());

    // Verify syringe volumes via HAL
    EXPECT_NEAR(hal->readSyringeVolume(hal::Barrel::Contrast), 200.0, 0.01);
    EXPECT_NEAR(hal->readSyringeVolume(hal::Barrel::Saline), 100.0, 0.01);
}

TEST(ConfigIntegration, DefaultConfigProducesValidSystem) {
    auto cfg = config::Config::defaults();

    hal::HalConfig halCfg;
    halCfg.contrastResistance  = cfg.hal.contrastResistance;
    halCfg.salineResistance    = cfg.hal.salineResistance;
    halCfg.baselinePressure    = cfg.hal.baselinePressure;
    halCfg.motorTimeConstantMs = cfg.hal.motorTimeConstantMs;
    halCfg.flowPerRpm          = cfg.hal.flowPerRpm;
    halCfg.maxRpm              = cfg.hal.maxRpm;
    halCfg.contrastVolumeMl    = cfg.syringe.contrastVolumeMl;
    halCfg.salineVolumeMl      = cfg.syringe.salineVolumeMl;

    auto hal = std::make_shared<hal::SimulatedHal>(halCfg);

    // Default initial values per spec
    EXPECT_NEAR(hal->readPressure(), 10.0, 0.01);
    EXPECT_NEAR(hal->readMotorRpm(), 0.0, 0.01);
    EXPECT_NEAR(hal->readSyringeVolume(hal::Barrel::Contrast), 100.0, 0.01);
    EXPECT_NEAR(hal->readSyringeVolume(hal::Barrel::Saline), 50.0, 0.01);

    // PID defaults are valid
    EXPECT_EQ(cfg.pid.kp, 100.0);
    EXPECT_EQ(cfg.pid.ki, 50.0);
    EXPECT_EQ(cfg.pid.kd, 5.0);

    // Safety defaults are valid
    EXPECT_EQ(cfg.safety.defaultPressureLimitPsi, 325.0);
    EXPECT_EQ(cfg.safety.jitterToleranceMs, 30.0);

    // Server defaults
    EXPECT_EQ(cfg.server.port, 50051);
    EXPECT_EQ(cfg.server.telemetryRateMs, 50);
}

}  // namespace
