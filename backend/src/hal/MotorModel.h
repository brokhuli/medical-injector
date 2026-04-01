#pragma once

namespace injector::hal {

class MotorModel {
public:
    explicit MotorModel(double timeConstantMs = 50.0,
                        double flowPerRpm = 0.01,
                        double maxRpm = 1500.0);

    void setCommandedRpm(double rpm);
    void tick(double dt);
    void emergencyStop();

    double actualRpm() const { return actualRpm_; }
    double commandedRpm() const { return commandedRpm_; }
    double flowRate() const { return actualRpm_ * flowPerRpm_; }

    // Fault support
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
