#include "hal/FirstOrderMotorModel.h"

#include <algorithm>

namespace injector::hal {

FirstOrderMotorModel::FirstOrderMotorModel(double timeConstantMs,
                                           double flowPerRpm,
                                           double maxRpm)
    : timeConstantS_(timeConstantMs / 1000.0),
      flowPerRpm_(flowPerRpm),
      maxRpm_(maxRpm) {}

void FirstOrderMotorModel::setCommandedRpm(double rpm) {
    commandedRpm_ = std::clamp(rpm, 0.0, maxRpm_);
}

void FirstOrderMotorModel::tick(double dt) {
    // First-order exponential lag: approach commanded RPM
    actualRpm_ += (commandedRpm_ - actualRpm_) * (dt / timeConstantS_);

    // Apply motor stall fault override if active
    if (hasRpmOverride_) {
        actualRpm_ = std::min(actualRpm_, rpmOverride_);
    }

    actualRpm_ = std::clamp(actualRpm_, 0.0, maxRpm_);
}

void FirstOrderMotorModel::emergencyStop() {
    commandedRpm_ = 0.0;
    actualRpm_ = 0.0;
}

double FirstOrderMotorModel::predictDecelVolume(double commandDecelRate) const {
    // Derivation: for a first-order motor tracking a linearly-ramped command
    // `cmd(t) = v - a·t` (where a = commandDecelRate), the actual flow v_a
    // satisfies v_a' = (cmd - v_a)/τ. Solving and integrating from t=0 to ∞:
    //
    //   total = v²/(2a)  +  v·τ  +  a·τ² · e^(-v/(a·τ))
    //
    // The exponential term is negligible when ramp time (v/a) is many
    // multiples of τ, which is the normal operating regime (e.g. at v=4 mL/s,
    // a=10 mL/s², τ=0.05s → v/(a·τ) = 8 → e^(-8) ≈ 3e-4). Dropping it:
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

void FirstOrderMotorModel::setMaxRpmOverride(double maxRpm) {
    hasRpmOverride_ = true;
    rpmOverride_ = maxRpm;
}

void FirstOrderMotorModel::clearFaults() {
    hasRpmOverride_ = false;
    rpmOverride_ = 0.0;
}

}  // namespace injector::hal
