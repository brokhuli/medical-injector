#pragma once

namespace injector::control {

struct PidConfig {
    double kp = 100.0;
    double ki = 50.0;
    double kd = 5.0;
    double iTermMax = 500.0;     // anti-windup clamp
    double maxRpm = 1500.0;      // output clamp
    double maxAcceleration = 10.0;  // mL/s² ramp limit
    // Pressure-aware decel shaping. `decelPressureThreshold` is the
    // pressure/limit ratio at which the decel rate starts scaling above
    // `maxAcceleration`; `decelPressureGain` sets how aggressively it scales
    // (effective rate at the limit is `maxAcceleration * (1 + gain)`).
    // gain=0 reproduces the fixed-rate decel.
    double decelPressureThreshold = 0.80;
    double decelPressureGain = 2.0;
};

/// Scale a base decel rate by pressure headroom. Returns `baseRate` when
/// `pressure/pressureLimit <= threshold`, and `baseRate * (1 + gain)` when
/// pressure equals the limit, with linear interpolation above the threshold.
/// Returns `baseRate` unchanged if `pressureLimit <= 0` or `threshold >= 1`.
[[nodiscard]] double pressureAwareDecelRate(double baseRate, double pressure,
                                            double pressureLimit,
                                            double threshold, double gain);

class PidController {
public:
    explicit PidController(const PidConfig& config = {});

    /// Compute motor RPM output given target and actual flow rates.
    /// dt is the time step in seconds.
    /// `maxAccelOverride` > 0 replaces `config.maxAcceleration` for this tick's
    /// ramp limiter only (used by the pressure-aware decel path). A value <= 0
    /// means "use config".
    [[nodiscard]] double compute(double targetFlowRate, double actualFlowRate,
                                 double dt, double maxAccelOverride = -1.0);

    /// Reset all internal state (integral, previous error, ramped target).
    void reset();

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
