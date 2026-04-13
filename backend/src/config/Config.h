#pragma once

#include <string>

namespace injector::config {

struct ServerConfig {
    int port = 50051;
    int telemetryRateMs = 50;
};

struct ControlConfig {
    int tickRateMs = 2;
    bool pinCore = true;
};

struct PidConfig {
    double kp = 100.0;
    double ki = 50.0;
    double kd = 5.0;
    double iTermMax = 500.0;
    double maxRpm = 1500.0;
    double maxAcceleration = 10.0;
    // Pressure-aware decel: ratio of pressureLimit at which the end-of-phase
    // decel ramp begins scaling above maxAcceleration, and how aggressively.
    double decelPressureThreshold = 0.80;
    double decelPressureGain = 2.0;
};

struct SafetyConfig {
    double defaultPressureLimitPsi = 325.0;
    double motorDivergenceThreshold = 200.0;
    int motorDivergenceTicks = 25;
    double jitterToleranceMs = 30.0;   // Windows scheduler jitter can reach 15-20ms under load
    int tickRateMs = 1;               // safety monitor's own tick rate
    int controlLoopTickRateMs = 2;    // expected control loop tick rate (for timing check)
};

struct HalConfig {
    // Tubing resistance per fluid (psi per mL/s). Contrast media is more
    // viscous than saline, producing a higher steady-state pressure at the
    // same flow rate.
    double contrastResistance = 45.0;
    double salineResistance = 35.0;
    double baselinePressure = 10.0;
    /// First-order lag applied to the pressure model (tubing/syringe/patient
    /// compliance). Pressure rises/falls toward the instantaneous target with
    /// this time constant.
    double pressureTimeConstantMs = 400.0;

    // Motor model (first-order lag)
    double motorTimeConstantMs = 50.0;
    double flowPerRpm = 0.01;
    double maxRpm = 1500.0;
};

struct SyringeConfig {
    double contrastVolumeMl = 100.0;
    double salineVolumeMl = 50.0;
};

struct LoggingConfig {
    int ringBufferSize = 300000;
    int maxEvents = 10000;
};

struct Config {
    ServerConfig server;
    ControlConfig control;
    PidConfig pid;
    SafetyConfig safety;
    HalConfig hal;
    SyringeConfig syringe;
    LoggingConfig logging;

    static Config loadFromFile(const std::string& path);
    static Config loadFromString(const std::string& json);
    static Config defaults();
};

}  // namespace injector::config
