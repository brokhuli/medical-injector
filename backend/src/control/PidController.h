#pragma once

namespace injector::control {

struct PidConfig {
    double kp = 100.0;
    double ki = 50.0;
    double kd = 5.0;
    double iTermMax = 500.0;     // anti-windup clamp
    double maxRpm = 1500.0;      // output clamp
    double maxAcceleration = 10.0;  // mL/s² ramp limit
    /// Brief zero-target pause applied at phase transitions so the pressure
    /// lag model produces a visible dip between phases. Set to 0 to disable.
    double phaseTransitionPauseMs = 0.0;
};

class PidController {
public:
    explicit PidController(const PidConfig& config = {});

    /// Compute motor RPM output given target and actual flow rates.
    /// dt is the time step in seconds.
    [[nodiscard]] double compute(double targetFlowRate, double actualFlowRate,
                                 double dt);

    /// Reset all internal state (integral, previous error, ramped target).
    void reset();

    /// Reset only the acceleration-ramp state to `value`. Used at phase
    /// transitions so the controller re-ramps cleanly from a known point
    /// (e.g. from 0 up to the new phase's target). Also clears the integral
    /// term to prevent wind-up carrying across phases.
    void resetRampedTarget(double value);

    /// Current ramped target after acceleration limiting.
    [[nodiscard]] double rampedTarget() const { return rampedTarget_; }

    /// Last computed output (RPM).
    [[nodiscard]] double lastOutput() const { return lastOutput_; }

private:
    PidConfig config_;
    double iTerm_ = 0.0;
    double prevError_ = 0.0;
    double filteredDerivative_ = 0.0;
    double rampedTarget_ = 0.0;
    double lastOutput_ = 0.0;
    bool firstTick_ = true;
};

}  // namespace injector::control
