#pragma once

#include "state/Protocol.h"
#include "hal/SimulatedHal.h"
#include "control/ControlLoop.h"
#include "config/Config.h"

namespace injector::testing {

// ── HAL configs ──────────────────────────────────────────────────────────────

/// Default HAL — pinned to values that keep the test suite's hard-coded
/// pressure numbers (4 mL/s → 210 psi) stable. Use this fixture — not the
/// production config defaults — when tests assert specific pressures.
inline hal::HalConfig defaultHalConfig() {
    hal::HalConfig cfg;
    cfg.contrastResistance = 50.0;
    cfg.salineResistance = 50.0;
    cfg.baselinePressure = 10.0;
    cfg.pressureTimeConstantMs = 400.0;
    cfg.contrastVolumeMl = 100.0;
    cfg.salineVolumeMl = 50.0;
    cfg.flowPerRpm = 0.01;
    cfg.maxRpm = 1500.0;
    cfg.motorTimeConstantMs = 50.0;
    return cfg;
}

/// Fast-convergence HAL — motor and pressure reach target in a few tens of
/// milliseconds instead of hundreds. Use for integration tests that want to
/// skip long ramp-up windows.
inline hal::HalConfig fastHalConfig() {
    hal::HalConfig cfg = defaultHalConfig();
    cfg.motorTimeConstantMs = 10.0;
    cfg.pressureTimeConstantMs = 10.0;
    return cfg;
}

/// Near-limit HAL — at 4 mL/s: pressure = 4.0*80+10 = 330 psi > 325 limit.
/// Uses fastHalConfig (10ms time constant) so the motor converges quickly.
inline hal::HalConfig nearLimitHalConfig() {
    hal::HalConfig cfg = fastHalConfig();
    cfg.contrastResistance = 80.0;
    cfg.salineResistance = 80.0;
    return cfg;
}

// ── Control loop configs ─────────────────────────────────────────────────────

inline control::ControlLoopConfig defaultLoopConfig(const hal::HalConfig& hal = defaultHalConfig()) {
    control::ControlLoopConfig cfg;
    cfg.tickRateMs = 2;
    cfg.pinCore = false;
    cfg.flowPerRpm = hal.flowPerRpm;
    cfg.pid.kp = 100.0;
    cfg.pid.ki = 50.0;
    cfg.pid.kd = 5.0;
    cfg.pid.iTermMax = 500.0;
    cfg.pid.maxRpm = 1500.0;
    cfg.pid.maxAcceleration = 10.0;
    return cfg;
}

// ── Safety configs ────────────────────────────────────────────────────────────

inline config::SafetyConfig defaultSafetyConfig() {
    config::SafetyConfig cfg;
    cfg.defaultPressureLimitPsi = 325.0;
    cfg.motorDivergenceThreshold = 200.0;
    cfg.motorDivergenceTicks = 25;
    // Use a large jitter tolerance for integration tests: Windows scheduler jitter
    // can reach 15-25 ms under load, so use the same 30 ms default as Config.h.
    cfg.jitterToleranceMs = 30.0;
    cfg.tickRateMs = 1;
    cfg.controlLoopTickRateMs = 2;
    return cfg;
}

// ── Protocol fixtures ─────────────────────────────────────────────────────────

/// single_phase_contrast: Contrast 4.0 mL/s, 80 mL, 325 psi
inline state::Protocol singlePhaseContrast() {
    state::Phase ph;
    ph.fluidType = state::FluidType::Contrast;
    ph.flowRate = 4.0;
    ph.volume = 80.0;
    ph.pressureLimit = 325.0;
    state::Protocol p;
    p.name = "single_phase_contrast";
    p.phases.push_back(ph);
    return p;
}

/// two_phase_standard: Contrast 4.0/80/325, Saline 2.0/30/200
inline state::Protocol twoPhaseStandard() {
    state::Phase ph1;
    ph1.fluidType = state::FluidType::Contrast;
    ph1.flowRate = 4.0;
    ph1.volume = 80.0;
    ph1.pressureLimit = 325.0;

    state::Phase ph2;
    ph2.fluidType = state::FluidType::Saline;
    ph2.flowRate = 2.0;
    ph2.volume = 30.0;
    ph2.pressureLimit = 200.0;

    state::Protocol p;
    p.name = "two_phase_standard";
    p.phases.push_back(ph1);
    p.phases.push_back(ph2);
    return p;
}

/// small_volume_fast: Contrast 4.0 mL/s, 8 mL — completes in ~2s.
inline state::Protocol smallVolumeFast() {
    state::Phase ph;
    ph.fluidType = state::FluidType::Contrast;
    ph.flowRate = 4.0;
    ph.volume = 8.0;
    ph.pressureLimit = 325.0;
    state::Protocol p;
    p.name = "small_volume_fast";
    p.phases.push_back(ph);
    return p;
}

}  // namespace injector::testing
