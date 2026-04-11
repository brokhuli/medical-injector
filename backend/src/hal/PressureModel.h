#pragma once

namespace injector::hal {

class PressureModel {
public:
    explicit PressureModel(double tubingResistance = 50.0,
                           double baselinePressure = 10.0,
                           double timeConstantMs = 400.0);

    /// Instantaneous steady-state pressure for a given flow rate.
    /// Pure function of inputs — used by tests and as the target of `step()`.
    double compute(double flowRate) const;

    /// Advance the internal first-order lag one tick and return the filtered
    /// pressure. Models tubing / syringe / patient compliance so pressure
    /// lags behind flow changes. `dt` is in seconds.
    double step(double flowRate, double dt);

    /// Snapshot of the currently filtered (state-carrying) pressure.
    double filteredPressure() const { return filteredPressure_; }

    /// Reset the filtered pressure to the baseline (compute(0)).
    void resetFiltered();

    // Fault support
    void setPressureOverride(double targetPsi);
    void setResistanceMultiplier(double multiplier);
    void clearFaults();

    double tubingResistance() const { return tubingResistance_; }
    double baselinePressure() const { return baselinePressure_; }
    double timeConstantMs() const { return timeConstantMs_; }

private:
    double tubingResistance_;
    double baselinePressure_;
    double timeConstantMs_;

    bool hasPressureOverride_ = false;
    double pressureOverride_ = 0.0;
    double resistanceMultiplier_ = 1.0;

    // Must be declared AFTER the fault members so that the constructor's
    // `filteredPressure_(compute(0.0))` sees the default-initialized fault
    // state (override=false, multiplier=1.0).
    double filteredPressure_;  // state: first-order filtered pressure
};

}  // namespace injector::hal
