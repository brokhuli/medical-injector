#include "hal/PressureModel.h"

namespace injector::hal {

PressureModel::PressureModel(double tubingResistance, double baselinePressure)
    : tubingResistance_(tubingResistance), baselinePressure_(baselinePressure) {}

double PressureModel::compute(double flowRate) const {
    if (hasPressureOverride_) {
        return pressureOverride_;
    }
    return (flowRate * tubingResistance_ * resistanceMultiplier_) + baselinePressure_;
}

void PressureModel::setPressureOverride(double targetPsi) {
    hasPressureOverride_ = true;
    pressureOverride_ = targetPsi;
}

void PressureModel::setResistanceMultiplier(double multiplier) {
    resistanceMultiplier_ = multiplier;
}

void PressureModel::clearFaults() {
    hasPressureOverride_ = false;
    pressureOverride_ = 0.0;
    resistanceMultiplier_ = 1.0;
}

}  // namespace injector::hal
