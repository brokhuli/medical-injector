#pragma once

#include "hal/IMotorModel.h"

namespace injector::hal {

/// First-order exponential lag motor model.
///
/// Dynamics: dRPM/dt = (commanded - actual) / τ
///
/// This is the model the simulator has used since inception. Flow is a
/// linear function of RPM. The `predictDecelVolume` method returns a
/// closed-form integral derived for a first-order motor tracking a
/// linearly-ramped command trajectory.
class FirstOrderMotorModel : public IMotorModel {
public:
    FirstOrderMotorModel(double timeConstantMs,
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
    double timeConstantS_;
    double flowPerRpm_;
    double maxRpm_;

    bool hasRpmOverride_ = false;
    double rpmOverride_ = 0.0;
};

}  // namespace injector::hal
