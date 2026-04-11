#include "hal/PressureModel.h"

#include <algorithm>

namespace injector::hal {

PressureModel::PressureModel(double tubingResistance, double baselinePressure,
                             double timeConstantMs)
    : tubingResistance_(tubingResistance),
      baselinePressure_(baselinePressure),
      timeConstantMs_(timeConstantMs),
      filteredPressure_(compute(0.0)) {}

double PressureModel::compute(double flowRate) const {
    if (hasPressureOverride_) {
        return pressureOverride_;
    }
    return (flowRate * tubingResistance_ * resistanceMultiplier_) + baselinePressure_;
}

double PressureModel::step(double flowRate, double dt) {
    // Fault overrides jump instantly to the commanded pressure so safety tests
    // and operators see the effect without waiting for the lag.
    if (hasPressureOverride_) {
        filteredPressure_ = pressureOverride_;
        return filteredPressure_;
    }

    const double target = compute(flowRate);
    const double tauS = std::max(1e-4, timeConstantMs_ * 1e-3);
    filteredPressure_ += (target - filteredPressure_) * (dt / tauS);
    return filteredPressure_;
}

void PressureModel::resetFiltered() {
    filteredPressure_ = compute(0.0);
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
    // Re-seed filtered pressure to baseline so post-fault state is coherent.
    filteredPressure_ = compute(0.0);
}

}  // namespace injector::hal
