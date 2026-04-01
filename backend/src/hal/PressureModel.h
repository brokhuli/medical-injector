#pragma once

namespace injector::hal {

class PressureModel {
public:
    explicit PressureModel(double tubingResistance = 50.0,
                           double baselinePressure = 10.0);

    double compute(double flowRate) const;

    // Fault support
    void setPressureOverride(double targetPsi);
    void setResistanceMultiplier(double multiplier);
    void clearFaults();

    double tubingResistance() const { return tubingResistance_; }
    double baselinePressure() const { return baselinePressure_; }

private:
    double tubingResistance_;
    double baselinePressure_;

    bool hasPressureOverride_ = false;
    double pressureOverride_ = 0.0;
    double resistanceMultiplier_ = 1.0;
};

}  // namespace injector::hal
