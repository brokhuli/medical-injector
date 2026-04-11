#include "hal/SecondOrderMotorModel.h"

#include <algorithm>
#include <cmath>

namespace injector::hal {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
}

SecondOrderMotorModel::SecondOrderMotorModel(double naturalFrequencyHz,
                                             double dampingRatio,
                                             double flowPerRpm,
                                             double maxRpm)
    : omegaN_(kTwoPi * naturalFrequencyHz),
      dampingRatio_(dampingRatio),
      flowPerRpm_(flowPerRpm),
      maxRpm_(maxRpm) {}

void SecondOrderMotorModel::setCommandedRpm(double rpm) {
    commandedRpm_ = std::clamp(rpm, 0.0, maxRpm_);
}

void SecondOrderMotorModel::tick(double dt) {
    // Second-order ODE integrated with semi-implicit Euler:
    //   accel = ωₙ²·(cmd - x1) - 2·ζ·ωₙ·x2
    //   x2 += accel · dt
    //   x1 += x2 · dt
    // Semi-implicit is stable for the frequencies we care about (<100 Hz).
    double accel = omegaN_ * omegaN_ * (commandedRpm_ - actualRpm_) -
                   2.0 * dampingRatio_ * omegaN_ * rpmVelocity_;
    rpmVelocity_ += accel * dt;
    actualRpm_ += rpmVelocity_ * dt;

    // Physical clamp: motor can't run backwards. If we hit the floor and
    // velocity is still negative, kill the velocity too (hard stop).
    if (actualRpm_ < 0.0) {
        actualRpm_ = 0.0;
        if (rpmVelocity_ < 0.0) rpmVelocity_ = 0.0;
    }

    // Motor stall fault: cap actual RPM
    if (hasRpmOverride_ && actualRpm_ > rpmOverride_) {
        actualRpm_ = rpmOverride_;
        if (rpmVelocity_ > 0.0) rpmVelocity_ = 0.0;
    }

    // Max RPM clamp: also kill upward velocity to avoid integrator wind-up
    if (actualRpm_ > maxRpm_) {
        actualRpm_ = maxRpm_;
        if (rpmVelocity_ > 0.0) rpmVelocity_ = 0.0;
    }
}

void SecondOrderMotorModel::emergencyStop() {
    commandedRpm_ = 0.0;
    actualRpm_ = 0.0;
    rpmVelocity_ = 0.0;
}

double SecondOrderMotorModel::predictDecelVolume(double commandDecelRate) const {
    // Numerical forward simulation of the motor's own dynamics under a
    // linearly-ramped command trajectory from the current commanded RPM
    // down to 0. There is no clean closed form for a second-order system
    // driven by a ramp input; a short forward sim is simple, flexible, and
    // fast (~1000 steps of trivial arithmetic).
    if (commandDecelRate <= 0.0) return 0.0;
    double v = flowRate();
    if (v <= 0.0 && commandedRpm_ <= 0.0) return 0.0;

    // Translate the flow-space decel rate (mL/s²) into RPM space (RPM/s²)
    const double cmdRpmDecelRate = commandDecelRate / flowPerRpm_;

    // Start the forward sim from the current motor state.
    double simX1 = actualRpm_;       // actual RPM
    double simX2 = rpmVelocity_;     // rpm velocity (state)
    double simCmd = commandedRpm_;   // commanded RPM (ramps to 0)

    constexpr double kDt = 0.001;     // 1 ms sim step
    constexpr int    kMaxSteps = 5000;  // 5 seconds hard cap
    constexpr double kStopRpm = 0.1;   // ≈ 0.001 mL/s at default flowPerRpm

    double volume = 0.0;
    for (int i = 0; i < kMaxSteps; ++i) {
        // Ramp commanded RPM down linearly
        simCmd = std::max(0.0, simCmd - cmdRpmDecelRate * kDt);

        // Integrate second-order dynamics (semi-implicit Euler, matches tick)
        double accel = omegaN_ * omegaN_ * (simCmd - simX1) -
                       2.0 * dampingRatio_ * omegaN_ * simX2;
        simX2 += accel * kDt;
        simX1 += simX2 * kDt;

        // Clamp to physical range
        if (simX1 < 0.0) {
            simX1 = 0.0;
            if (simX2 < 0.0) simX2 = 0.0;
        }

        // Accumulate volume delivered during this step
        volume += simX1 * flowPerRpm_ * kDt;

        // Termination: command fully ramped down and motor settled
        if (simCmd <= 0.0 && simX1 < kStopRpm) {
            break;
        }
    }
    return volume;
}

void SecondOrderMotorModel::setMaxRpmOverride(double maxRpm) {
    hasRpmOverride_ = true;
    rpmOverride_ = maxRpm;
}

void SecondOrderMotorModel::clearFaults() {
    hasRpmOverride_ = false;
    rpmOverride_ = 0.0;
}

}  // namespace injector::hal
