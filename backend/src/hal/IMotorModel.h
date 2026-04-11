#pragma once

namespace injector::hal {

/// Abstract motor model interface.
///
/// Concrete implementations encapsulate the motor's dynamic behaviour
/// (first-order lag, second-order with inertia, stiction, backlash,
/// calibration tables, etc.). `SimulatedHal` holds one of these and routes
/// RPM commands and sensor reads through it.
///
/// The `predictDecelVolume` method lets the control loop ask "how much
/// more fluid will be delivered if I start decelerating right now?" without
/// needing to know anything about the motor's dynamics.
class IMotorModel {
public:
    virtual ~IMotorModel() = default;

    // Dynamics
    virtual void tick(double dt) = 0;
    virtual void setCommandedRpm(double rpm) = 0;
    virtual double actualRpm() const = 0;
    virtual double commandedRpm() const = 0;
    virtual double flowRate() const = 0;  // mL/s, derived from internal state
    virtual void emergencyStop() = 0;

    /// Predict the volume (mL) that will be delivered between now and when
    /// the motor fully stops, assuming the commanded flow begins decelerating
    /// linearly at `commandDecelRate` (mL/s²) from the current state.
    ///
    /// `commandDecelRate` is the controller's rate limit (mL/s²), passed
    /// explicitly so the motor model doesn't need to know about controller
    /// internals.
    virtual double predictDecelVolume(double commandDecelRate) const = 0;

    // Fault hooks (shared by all concrete models)
    virtual void setMaxRpmOverride(double maxRpm) = 0;
    virtual void clearFaults() = 0;
};

}  // namespace injector::hal
