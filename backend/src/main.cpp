#include "config/Config.h"
#include "hal/SimulatedHal.h"

#include <spdlog/spdlog.h>

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
    spdlog::info("  Contrast:  {:.1f} mL", hal->readSyringeVolume(injector::hal::Barrel::Contrast));
    spdlog::info("  Saline:    {:.1f} mL", hal->readSyringeVolume(injector::hal::Barrel::Saline));
    spdlog::info("  Air:       {}", hal->readAirDetector() ? "DETECTED" : "clear");

    spdlog::info("No control loop or gRPC server configured yet. Exiting.");
    return 0;
}
