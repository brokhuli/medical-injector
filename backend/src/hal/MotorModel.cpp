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

double MotorModel::predictDecelVolume(double commandDecelRate) const {
    // Derivation: for a first-order motor tracking a linearly-ramped command
    // `cmd(t) = v - a·t` (where a = commandDecelRate), the actual flow v_a
    // satisfies v_a' = (cmd - v_a)/τ. Solving and integrating from t=0 to ∞:
    //
    //   total = v²/(2a)  +  v·τ  +  a·τ² · e^(-v/(a·τ))
    //
    // The exponential term is negligible when ramp time (v/a) is many
    // multiples of τ, which is the normal operating regime. Dropping it:
    //
    //   decelVolume(v) ≈ v²/(2a) + v·τ
    //
    // Term 1 is the triangle area of the linear ramp, term 2 is the motor's
    // exponential lag tail.
    double v = flowRate();
    if (commandDecelRate <= 0.0 || v <= 0.0) {
        return 0.0;
    }
    return (v * v) / (2.0 * commandDecelRate) + v * timeConstantS_;
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
