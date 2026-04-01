#include "hal/MotorModel.h"

#include <algorithm>

namespace injector::hal {

MotorModel::MotorModel(double timeConstantMs, double flowPerRpm, double maxRpm)
    : timeConstantS_(timeConstantMs / 1000.0),
      flowPerRpm_(flowPerRpm),
      maxRpm_(maxRpm) {}

void MotorModel::setCommandedRpm(double rpm) {
    commandedRpm_ = std::clamp(rpm, 0.0, maxRpm_);
}

void MotorModel::tick(double dt) {
    // First-order exponential lag: approach commanded RPM
    actualRpm_ += (commandedRpm_ - actualRpm_) * (dt / timeConstantS_);

    // Apply motor stall fault override if active
    if (hasRpmOverride_) {
        actualRpm_ = std::min(actualRpm_, rpmOverride_);
    }

    actualRpm_ = std::clamp(actualRpm_, 0.0, maxRpm_);
}

void MotorModel::emergencyStop() {
    commandedRpm_ = 0.0;
    actualRpm_ = 0.0;
}

void MotorModel::setMaxRpmOverride(double maxRpm) {
    hasRpmOverride_ = true;
    rpmOverride_ = maxRpm;
}

void MotorModel::clearFaults() {
    hasRpmOverride_ = false;
    rpmOverride_ = 0.0;
}

}  // namespace injector::hal
