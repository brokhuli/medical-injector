#include "hal/MotorModelFactory.h"

#include "hal/FirstOrderMotorModel.h"
#include "hal/SecondOrderMotorModel.h"

#include <stdexcept>

namespace injector::hal {

std::unique_ptr<IMotorModel> createMotorModel(const MotorModelConfig& cfg) {
    if (cfg.type == "first_order") {
        return std::make_unique<FirstOrderMotorModel>(
            cfg.timeConstantMs, cfg.flowPerRpm, cfg.maxRpm);
    }
    if (cfg.type == "second_order") {
        return std::make_unique<SecondOrderMotorModel>(
            cfg.naturalFrequencyHz, cfg.dampingRatio, cfg.flowPerRpm, cfg.maxRpm);
    }
    throw std::invalid_argument("Unknown motor model type: " + cfg.type);
}

}  // namespace injector::hal
