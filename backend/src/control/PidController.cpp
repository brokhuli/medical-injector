#include "control/PidController.h"

#include <algorithm>
#include <cmath>

namespace injector::control {

PidController::PidController(const PidConfig& config) : config_(config) {}

double PidController::compute(double targetFlowRate, double actualFlowRate,
                              double dt) {
    if (dt <= 0.0) {
        return lastOutput_;
    }

    // Acceleration ramp: rate-limit the target change
    double maxChange = config_.maxAcceleration * dt;
    double delta = targetFlowRate - rampedTarget_;
    delta = std::clamp(delta, -maxChange, maxChange);
    rampedTarget_ += delta;

    // PID error (flow-rate space)
    double error = rampedTarget_ - actualFlowRate;

    // P term
    double pTerm = config_.kp * error;

    // I term with anti-windup clamp
    iTerm_ += config_.ki * error * dt;
    iTerm_ = std::clamp(iTerm_, -config_.iTermMax, config_.iTermMax);

    // D term with low-pass filter to prevent high-frequency oscillation.
    // Filter: alpha = dt / (dt + Tf), where Tf ≈ dt * 10 provides smoothing.
    double dTerm = 0.0;
    if (!firstTick_) {
        double rawDerivative = (error - prevError_) / dt;
        constexpr double FILTER_COEFF = 0.1;  // alpha: dt / (dt + 10*dt)
        filteredDerivative_ =
            FILTER_COEFF * rawDerivative +
            (1.0 - FILTER_COEFF) * filteredDerivative_;
        dTerm = config_.kd * filteredDerivative_;
    }
    firstTick_ = false;
    prevError_ = error;

    // Sum and clamp output to [0, maxRpm]
    double output = pTerm + iTerm_ + dTerm;
    output = std::clamp(output, 0.0, config_.maxRpm);

    lastOutput_ = output;
    return output;
}

void PidController::reset() {
    iTerm_ = 0.0;
    prevError_ = 0.0;
    filteredDerivative_ = 0.0;
    rampedTarget_ = 0.0;
    lastOutput_ = 0.0;
    firstTick_ = true;
}

void PidController::resetRampedTarget(double value) {
    rampedTarget_ = value;
    iTerm_ = 0.0;
    filteredDerivative_ = 0.0;
    prevError_ = 0.0;
    firstTick_ = true;
}

}  // namespace injector::control
