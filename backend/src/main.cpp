#include "config/Config.h"
#include "control/ControlLoop.h"
#include "hal/SimulatedHal.h"
#include "logging/DataLogger.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {
    spdlog::info("Medical Injector Simulator — backend starting");

    // Load config (optional file path as first argument)
    auto cfg = (argc > 1) ? injector::config::Config::loadFromFile(argv[1])
                          : injector::config::Config::defaults();

    // Create HAL from config
    injector::hal::HalConfig halCfg;
    halCfg.flowPerRpm = cfg.hal.flowPerRpm;
    halCfg.tubingResistance = cfg.hal.tubingResistance;
    halCfg.baselinePressure = cfg.hal.baselinePressure;
    halCfg.motorTimeConstantMs = cfg.hal.motorTimeConstantMs;
    halCfg.motorMaxRpm = cfg.hal.motorMaxRpm;
    halCfg.contrastVolumeMl = cfg.syringe.contrastVolumeMl;
    halCfg.salineVolumeMl = cfg.syringe.salineVolumeMl;

    auto hal = std::make_shared<injector::hal::SimulatedHal>(halCfg);

    // Log initial state
    spdlog::info("HAL initialized:");
    spdlog::info("  Pressure:  {:.1f} psi", hal->readPressure());
    spdlog::info("  Motor RPM: {:.1f}", hal->readMotorRpm());
    spdlog::info("  Contrast:  {:.1f} mL",
                 hal->readSyringeVolume(injector::hal::Barrel::Contrast));
    spdlog::info("  Saline:    {:.1f} mL",
                 hal->readSyringeVolume(injector::hal::Barrel::Saline));
    spdlog::info("  Air:       {}",
                 hal->readAirDetector() ? "DETECTED" : "clear");

    // Set up data logger
    injector::logging::DataLogger logger(
        static_cast<size_t>(cfg.logging.ringBufferSize),
        static_cast<size_t>(cfg.logging.maxEvents));

    // Open contrast valve for injection
    hal->setValve(injector::hal::FluidChannel::Contrast,
                  injector::hal::ValveState::Open);

    // Configure control loop
    injector::control::ControlLoopConfig loopCfg;
    loopCfg.tickRateMs = cfg.control.tickRateMs;
    loopCfg.pinCore = cfg.control.pinCore;
    loopCfg.flowPerRpm = cfg.hal.flowPerRpm;
    loopCfg.pid.kp = cfg.pid.kp;
    loopCfg.pid.ki = cfg.pid.ki;
    loopCfg.pid.kd = cfg.pid.kd;
    loopCfg.pid.iTermMax = cfg.pid.iTermMax;
    loopCfg.pid.maxRpm = cfg.pid.maxRpm;
    loopCfg.pid.maxAcceleration = cfg.pid.maxAcceleration;

    injector::control::ControlLoop controlLoop(hal, loopCfg,
                                               &logger.tickBuffer());

    // Run at 4.0 mL/s for 10 seconds
    constexpr double TARGET_FLOW = 4.0;
    constexpr int RUN_SECONDS = 10;

    spdlog::info("Starting control loop: target {:.1f} mL/s for {}s",
                 TARGET_FLOW, RUN_SECONDS);
    controlLoop.setTargetFlowRate(TARGET_FLOW);
    controlLoop.start();

    std::this_thread::sleep_for(std::chrono::seconds(RUN_SECONDS));

    controlLoop.stop();

    // Log results
    auto stats = controlLoop.timingStats();
    spdlog::info("Control loop stopped. Results:");
    spdlog::info("  Total ticks:  {}", stats.totalTicks);
    spdlog::info("  Tick timing:  mean {:.3f} ms, min {:.3f} ms, max {:.3f} ms",
                 stats.meanTickMs, stats.minTickMs, stats.maxTickMs);
    spdlog::info("  Overruns:     {} (>{} ms)", stats.overrunCount,
                 cfg.control.tickRateMs * 2);
    spdlog::info("  Volume:       {:.1f} mL", controlLoop.cumulativeVolume());
    spdlog::info("  Contrast rem: {:.1f} mL",
                 hal->readSyringeVolume(injector::hal::Barrel::Contrast));
    spdlog::info("  Tick data:    {} entries logged",
                 logger.tickBuffer().totalPushed());

    return 0;
}
