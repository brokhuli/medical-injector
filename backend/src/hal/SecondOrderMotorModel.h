#pragma once

#include "hal/IMotorModel.h"

namespace injector::hal {

/// Second-order motor model with inertia.
///
/// Dynamics (undamped natural frequency ωₙ, damping ratio ζ):
///
///     d²(rpm)/dt² + 2ζωₙ · d(rpm)/dt + ωₙ² · rpm = ωₙ² · commanded
///
/// Underdamped (ζ < 1) responses overshoot and ring; critically damped
/// (ζ = 1) is the fastest non-oscillatory response; overdamped (ζ > 1)
/// is sluggish.
///
/// `predictDecelVolume` runs a short numerical forward simulation of the
/// motor state under a linearly-ramped command trajectory to predict the
/// volume delivered until the motor fully stops. Unlike the first-order
/// case there is no convenient closed-form expression.
class SecondOrderMotorModel : public IMotorModel {
public:
    SecondOrderMotorModel(double naturalFrequencyHz,
                          double dampingRatio,
                          double flowPerRpm,
                          double maxRpm);

    // IMotorModel
    void tick(double dt) override;
    void setCommandedRpm(double rpm) override;
    double actualRpm() const override { return actualRpm_; }
    double commandedRpm() const override { return commandedRpm_; }
    double flowRate() const override { return actualRpm_ * flowPerRpm_; }
    void emergencyStop() override;
    double predictDecelVolume(double commandDecelRate) const override;
    void setMaxRpmOverride(double maxRpm) override;
    void clearFaults() override;

private:
    double commandedRpm_ = 0.0;
    double actualRpm_ = 0.0;
    double rpmVelocity_ = 0.0;  // rate of change of actualRpm_ (state)
    double omegaN_;              // natural frequency in rad/s
    double dampingRatio_;
    double flowPerRpm_;
    double maxRpm_;

    bool hasRpmOverride_ = false;
    double rpmOverride_ = 0.0;
};

}  // namespace injector::hal
