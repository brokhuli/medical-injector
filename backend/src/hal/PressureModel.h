#pragma once

namespace injector::hal {

/// Tubing + compliance pressure model.
///
/// Steady-state pressure is a linear function of flow rate and the tubing
/// resistance of whichever fluid is currently flowing:
///
///     P_ss = flowRate · resistance · multiplier  +  baseline
///
/// A first-order lag (time constant = tubing/syringe/patient compliance)
/// filters the steady-state value so pressure rises and decays smoothly
/// rather than jumping. The active resistance is passed in at each step
/// because it changes when the active valve switches between contrast and
/// saline.
class PressureModel {
public:
    explicit PressureModel(double baselinePressure = 10.0,
                           double timeConstantMs = 400.0);

    /// Instantaneous steady-state pressure for a given flow rate and
    /// tubing resistance. Pure function of inputs.
    double compute(double flowRate, double resistance) const;

    /// Advance the internal first-order lag one tick and return the filtered
    /// pressure. `dt` is in seconds.
    double step(double flowRate, double resistance, double dt);

    /// Snapshot of the currently filtered (state-carrying) pressure.
    double filteredPressure() const { return filteredPressure_; }

    /// Reset the filtered pressure to the baseline.
    void resetFiltered();

    // Fault support
    void setPressureOverride(double targetPsi);
    void setResistanceMultiplier(double multiplier);
    void clearFaults();

    double baselinePressure() const { return baselinePressure_; }
    double timeConstantMs() const { return timeConstantMs_; }

private:
    double baselinePressure_;
    double timeConstantMs_;

    bool hasPressureOverride_ = false;
    double pressureOverride_ = 0.0;
    double resistanceMultiplier_ = 1.0;

    double filteredPressure_;  // state: first-order filtered pressure
};

}  // namespace injector::hal
