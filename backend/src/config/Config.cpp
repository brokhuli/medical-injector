#include "config/Config.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace injector::config {

namespace {

template <typename T>
T clamp(T value, T low, T high, const char* name) {
    if (value < low || value > high) {
        spdlog::warn("Config field '{}' value {} out of range [{}, {}], clamping",
                     name, value, low, high);
        return std::clamp(value, low, high);
    }
    return value;
}

template <typename T>
T requirePositive(T value, const char* name) {
    if (value <= 0) {
        throw std::invalid_argument(
            std::string("Config field '") + name + "' must be positive, got " +
            std::to_string(value));
    }
    return value;
}

template <typename T>
T requireNonNegative(T value, const char* name) {
    if (value < 0) {
        throw std::invalid_argument(
            std::string("Config field '") + name + "' must be non-negative, got " +
            std::to_string(value));
    }
    return value;
}

void validate(Config& cfg) {
    // Server
    cfg.server.port = clamp(cfg.server.port, 1024, 65535, "server.port");
    cfg.server.telemetryRateMs = clamp(cfg.server.telemetryRateMs, 20, 1000,
                                       "server.telemetryRateMs");

    // Control
    cfg.control.tickRateMs = clamp(cfg.control.tickRateMs, 1, 10, "control.tickRateMs");

    // PID
    cfg.pid.kp = requirePositive(cfg.pid.kp, "pid.kp");
    cfg.pid.ki = requireNonNegative(cfg.pid.ki, "pid.ki");
    cfg.pid.kd = requireNonNegative(cfg.pid.kd, "pid.kd");
    cfg.pid.iTermMax = requirePositive(cfg.pid.iTermMax, "pid.iTermMax");
    cfg.pid.maxRpm = requirePositive(cfg.pid.maxRpm, "pid.maxRpm");
    cfg.pid.maxAcceleration = requirePositive(cfg.pid.maxAcceleration, "pid.maxAcceleration");
    cfg.pid.decelPressureThreshold = clamp(cfg.pid.decelPressureThreshold, 0.0, 1.0,
                                           "pid.decelPressureThreshold");
    cfg.pid.decelPressureGain = requireNonNegative(cfg.pid.decelPressureGain,
                                                    "pid.decelPressureGain");

    // Safety
    cfg.safety.defaultPressureLimitPsi = clamp(cfg.safety.defaultPressureLimitPsi,
                                               50.0, 400.0,
                                               "safety.defaultPressureLimitPsi");
    cfg.safety.motorDivergenceThreshold = requirePositive(
        cfg.safety.motorDivergenceThreshold, "safety.motorDivergenceThreshold");
    if (cfg.safety.motorDivergenceTicks < 1) {
        throw std::invalid_argument(
            "Config field 'safety.motorDivergenceTicks' must be >= 1");
    }
    cfg.safety.jitterToleranceMs = requirePositive(cfg.safety.jitterToleranceMs,
                                                   "safety.jitterToleranceMs");
    cfg.safety.tickRateMs = clamp(cfg.safety.tickRateMs, 1, 10, "safety.tickRateMs");

    // HAL
    cfg.hal.contrastResistance = requirePositive(cfg.hal.contrastResistance,
                                                 "hal.contrastResistance");
    cfg.hal.salineResistance = requirePositive(cfg.hal.salineResistance,
                                               "hal.salineResistance");
    cfg.hal.baselinePressure = requireNonNegative(cfg.hal.baselinePressure,
                                                   "hal.baselinePressure");
    cfg.hal.pressureTimeConstantMs = requireNonNegative(
        cfg.hal.pressureTimeConstantMs, "hal.pressureTimeConstantMs");
    cfg.hal.motorTimeConstantMs = requirePositive(
        cfg.hal.motorTimeConstantMs, "hal.motorTimeConstantMs");
    cfg.hal.flowPerRpm = requirePositive(cfg.hal.flowPerRpm, "hal.flowPerRpm");
    cfg.hal.maxRpm = requirePositive(cfg.hal.maxRpm, "hal.maxRpm");

    // Syringe
    cfg.syringe.contrastVolumeMl = clamp(cfg.syringe.contrastVolumeMl, 1.0, 500.0,
                                          "syringe.contrastVolumeMl");
    cfg.syringe.salineVolumeMl = clamp(cfg.syringe.salineVolumeMl, 1.0, 500.0,
                                        "syringe.salineVolumeMl");

    // Logging
    cfg.logging.ringBufferSize = clamp(cfg.logging.ringBufferSize, 1000, 1000000,
                                        "logging.ringBufferSize");
    cfg.logging.maxEvents = clamp(cfg.logging.maxEvents, 100, 100000,
                                   "logging.maxEvents");
}

Config parseJson(const nlohmann::json& j) {
    Config cfg;

    if (j.contains("server")) {
        auto& s = j["server"];
        if (s.contains("port")) cfg.server.port = s["port"].get<int>();
        if (s.contains("telemetryRateMs"))
            cfg.server.telemetryRateMs = s["telemetryRateMs"].get<int>();
    }

    if (j.contains("control")) {
        auto& c = j["control"];
        if (c.contains("tickRateMs")) cfg.control.tickRateMs = c["tickRateMs"].get<int>();
        if (c.contains("pinCore")) cfg.control.pinCore = c["pinCore"].get<bool>();
    }

    if (j.contains("pid")) {
        auto& p = j["pid"];
        if (p.contains("kp")) cfg.pid.kp = p["kp"].get<double>();
        if (p.contains("ki")) cfg.pid.ki = p["ki"].get<double>();
        if (p.contains("kd")) cfg.pid.kd = p["kd"].get<double>();
        if (p.contains("iTermMax")) cfg.pid.iTermMax = p["iTermMax"].get<double>();
        if (p.contains("maxRpm")) cfg.pid.maxRpm = p["maxRpm"].get<double>();
        if (p.contains("maxAcceleration"))
            cfg.pid.maxAcceleration = p["maxAcceleration"].get<double>();
        if (p.contains("decelPressureThreshold"))
            cfg.pid.decelPressureThreshold = p["decelPressureThreshold"].get<double>();
        if (p.contains("decelPressureGain"))
            cfg.pid.decelPressureGain = p["decelPressureGain"].get<double>();
    }

    if (j.contains("safety")) {
        auto& s = j["safety"];
        if (s.contains("defaultPressureLimitPsi"))
            cfg.safety.defaultPressureLimitPsi =
                s["defaultPressureLimitPsi"].get<double>();
        if (s.contains("motorDivergenceThreshold"))
            cfg.safety.motorDivergenceThreshold =
                s["motorDivergenceThreshold"].get<double>();
        if (s.contains("motorDivergenceTicks"))
            cfg.safety.motorDivergenceTicks = s["motorDivergenceTicks"].get<int>();
        if (s.contains("jitterToleranceMs"))
            cfg.safety.jitterToleranceMs = s["jitterToleranceMs"].get<double>();
        if (s.contains("tickRateMs"))
            cfg.safety.tickRateMs = s["tickRateMs"].get<int>();
    }

    if (j.contains("hal")) {
        auto& h = j["hal"];
        if (h.contains("contrastResistance"))
            cfg.hal.contrastResistance = h["contrastResistance"].get<double>();
        if (h.contains("salineResistance"))
            cfg.hal.salineResistance = h["salineResistance"].get<double>();
        if (h.contains("baselinePressure"))
            cfg.hal.baselinePressure = h["baselinePressure"].get<double>();
        if (h.contains("pressureTimeConstantMs"))
            cfg.hal.pressureTimeConstantMs =
                h["pressureTimeConstantMs"].get<double>();
        if (h.contains("motorTimeConstantMs"))
            cfg.hal.motorTimeConstantMs =
                h["motorTimeConstantMs"].get<double>();
        if (h.contains("flowPerRpm"))
            cfg.hal.flowPerRpm = h["flowPerRpm"].get<double>();
        if (h.contains("maxRpm"))
            cfg.hal.maxRpm = h["maxRpm"].get<double>();
    }

    if (j.contains("syringe")) {
        auto& s = j["syringe"];
        if (s.contains("contrastVolumeMl"))
            cfg.syringe.contrastVolumeMl = s["contrastVolumeMl"].get<double>();
        if (s.contains("salineVolumeMl"))
            cfg.syringe.salineVolumeMl = s["salineVolumeMl"].get<double>();
    }

    if (j.contains("logging")) {
        auto& l = j["logging"];
        if (l.contains("ringBufferSize"))
            cfg.logging.ringBufferSize = l["ringBufferSize"].get<int>();
        if (l.contains("maxEvents"))
            cfg.logging.maxEvents = l["maxEvents"].get<int>();
    }

    validate(cfg);
    return cfg;
}

}  // namespace

Config Config::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::warn("Config file '{}' not found, using defaults", path);
        return defaults();
    }

    spdlog::info("Loading config from '{}'", path);
    nlohmann::json j = nlohmann::json::parse(file);
    return parseJson(j);
}

Config Config::loadFromString(const std::string& json) {
    nlohmann::json j = nlohmann::json::parse(json);
    return parseJson(j);
}

Config Config::defaults() {
    return Config{};
}

}  // namespace injector::config
