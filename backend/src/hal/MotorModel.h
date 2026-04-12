#pragma once

namespace injector::hal {

/// First-order exponential-lag motor model.
///
/// Dynamics: dRPM/dt = (commanded - actual) / τ
/// Flow is a linear function of RPM (`flowPerRpm * actualRpm`).
class MotorModel {
public:
    MotorModel(double timeConstantMs = 50.0,
               double flowPerRpm = 0.01,
               double maxRpm = 1500.0);

    void tick(double dt);
    void setCommandedRpm(double rpm);
    double actualRpm() const { return actualRpm_; }
    double commandedRpm() const { return commandedRpm_; }
    double flowRate() const { return actualRpm_ * flowPerRpm_; }
    void emergencyStop();

    /// Predict the volume (mL) that will be delivered between now and when
    /// the motor fully stops, assuming the commanded flow begins decelerating
    /// linearly at `commandDecelRate` (mL/s²) from the current state.
    double predictDecelVolume(double commandDecelRate) const;

    // Fault hooks
    void setMaxRpmOverride(double maxRpm);
    void clearFaults();

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
