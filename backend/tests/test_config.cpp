#include "config/Config.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace injector::config;

TEST(ConfigTest, DefaultValues) {
    Config cfg = Config::defaults();

    EXPECT_EQ(cfg.server.port, 50051);
    EXPECT_EQ(cfg.server.telemetryRateMs, 50);
    EXPECT_EQ(cfg.control.tickRateMs, 2);
    EXPECT_TRUE(cfg.control.pinCore);
    EXPECT_DOUBLE_EQ(cfg.pid.kp, 100.0);
    EXPECT_DOUBLE_EQ(cfg.pid.ki, 50.0);
    EXPECT_DOUBLE_EQ(cfg.pid.kd, 5.0);
    EXPECT_DOUBLE_EQ(cfg.pid.iTermMax, 500.0);
    EXPECT_DOUBLE_EQ(cfg.pid.maxRpm, 1500.0);
    EXPECT_DOUBLE_EQ(cfg.pid.maxAcceleration, 10.0);
    EXPECT_DOUBLE_EQ(cfg.safety.defaultPressureLimitPsi, 325.0);
    EXPECT_DOUBLE_EQ(cfg.safety.motorDivergenceThreshold, 200.0);
    EXPECT_EQ(cfg.safety.motorDivergenceTicks, 25);
    EXPECT_DOUBLE_EQ(cfg.safety.jitterToleranceMs, 3.0);
    EXPECT_EQ(cfg.safety.tickRateMs, 1);
    EXPECT_DOUBLE_EQ(cfg.hal.flowPerRpm, 0.01);
    EXPECT_DOUBLE_EQ(cfg.hal.tubingResistance, 50.0);
    EXPECT_DOUBLE_EQ(cfg.hal.baselinePressure, 10.0);
    EXPECT_DOUBLE_EQ(cfg.hal.motorTimeConstantMs, 50.0);
    EXPECT_DOUBLE_EQ(cfg.hal.motorMaxRpm, 1500.0);
    EXPECT_DOUBLE_EQ(cfg.syringe.contrastVolumeMl, 100.0);
    EXPECT_DOUBLE_EQ(cfg.syringe.salineVolumeMl, 50.0);
    EXPECT_EQ(cfg.logging.ringBufferSize, 300000);
    EXPECT_EQ(cfg.logging.maxEvents, 10000);
}

TEST(ConfigTest, ParseEmptyJson) {
    Config cfg = Config::loadFromString("{}");
    Config def = Config::defaults();

    EXPECT_EQ(cfg.server.port, def.server.port);
    EXPECT_DOUBLE_EQ(cfg.pid.kp, def.pid.kp);
    EXPECT_DOUBLE_EQ(cfg.hal.flowPerRpm, def.hal.flowPerRpm);
}

TEST(ConfigTest, ParsePartialJson) {
    Config cfg = Config::loadFromString(R"({
        "server": { "port": 9090 },
        "hal": { "flowPerRpm": 0.02 }
    })");

    EXPECT_EQ(cfg.server.port, 9090);
    EXPECT_DOUBLE_EQ(cfg.hal.flowPerRpm, 0.02);
    // Unspecified fields keep defaults
    EXPECT_EQ(cfg.server.telemetryRateMs, 50);
    EXPECT_DOUBLE_EQ(cfg.hal.tubingResistance, 50.0);
}

TEST(ConfigTest, ParseFullJson) {
    Config cfg = Config::loadFromString(R"({
        "server": { "port": 8080, "telemetryRateMs": 100 },
        "control": { "tickRateMs": 5, "pinCore": false },
        "pid": { "kp": 200.0, "ki": 25.0, "kd": 10.0, "iTermMax": 1000.0, "maxRpm": 2000.0, "maxAcceleration": 20.0 },
        "safety": { "defaultPressureLimitPsi": 300.0, "motorDivergenceThreshold": 100.0, "motorDivergenceTicks": 50, "jitterToleranceMs": 5.0, "tickRateMs": 2 },
        "hal": { "flowPerRpm": 0.02, "tubingResistance": 75.0, "baselinePressure": 15.0, "motorTimeConstantMs": 100.0, "motorMaxRpm": 2000.0 },
        "syringe": { "contrastVolumeMl": 150.0, "salineVolumeMl": 75.0 },
        "logging": { "ringBufferSize": 500000, "maxEvents": 50000 }
    })");

    EXPECT_EQ(cfg.server.port, 8080);
    EXPECT_EQ(cfg.server.telemetryRateMs, 100);
    EXPECT_EQ(cfg.control.tickRateMs, 5);
    EXPECT_FALSE(cfg.control.pinCore);
    EXPECT_DOUBLE_EQ(cfg.pid.kp, 200.0);
    EXPECT_DOUBLE_EQ(cfg.pid.ki, 25.0);
    EXPECT_DOUBLE_EQ(cfg.pid.kd, 10.0);
    EXPECT_DOUBLE_EQ(cfg.syringe.contrastVolumeMl, 150.0);
    EXPECT_DOUBLE_EQ(cfg.hal.tubingResistance, 75.0);
    EXPECT_EQ(cfg.logging.ringBufferSize, 500000);
}

TEST(ConfigTest, ClampOutOfRangeValues) {
    // Port too low → clamped to 1024
    Config cfg = Config::loadFromString(R"({"server": {"port": 80}})");
    EXPECT_EQ(cfg.server.port, 1024);

    // Port too high → clamped to 65535
    cfg = Config::loadFromString(R"({"server": {"port": 70000}})");
    EXPECT_EQ(cfg.server.port, 65535);

    // Telemetry rate too low → clamped to 20
    cfg = Config::loadFromString(R"({"server": {"telemetryRateMs": 5}})");
    EXPECT_EQ(cfg.server.telemetryRateMs, 20);

    // Syringe volume too low → clamped to 1.0
    cfg = Config::loadFromString(R"({"syringe": {"contrastVolumeMl": 0.5}})");
    EXPECT_DOUBLE_EQ(cfg.syringe.contrastVolumeMl, 1.0);
}

TEST(ConfigTest, RejectInvalidValues) {
    // Negative kp
    EXPECT_THROW(Config::loadFromString(R"({"pid": {"kp": -1.0}})"),
                 std::invalid_argument);

    // Zero flowPerRpm
    EXPECT_THROW(Config::loadFromString(R"({"hal": {"flowPerRpm": 0.0}})"),
                 std::invalid_argument);

    // Negative ki is fine (requireNonNegative), but -1 is not
    EXPECT_THROW(Config::loadFromString(R"({"pid": {"ki": -1.0}})"),
                 std::invalid_argument);

    // Zero motorDivergenceTicks
    EXPECT_THROW(Config::loadFromString(R"({"safety": {"motorDivergenceTicks": 0}})"),
                 std::invalid_argument);
}

TEST(ConfigTest, MissingFileReturnsDefaults) {
    Config cfg = Config::loadFromFile("nonexistent_config_file.json");
    Config def = Config::defaults();

    EXPECT_EQ(cfg.server.port, def.server.port);
    EXPECT_DOUBLE_EQ(cfg.hal.flowPerRpm, def.hal.flowPerRpm);
}

TEST(ConfigTest, InvalidJsonThrows) {
    EXPECT_THROW(Config::loadFromString("not valid json"),
                 nlohmann::json::parse_error);
}
