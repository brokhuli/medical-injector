#include "hal/SimulatedHal.h"

namespace injector::hal {

SimulatedHal::SimulatedHal(const HalConfig& config)
    : motor_(config.motorTimeConstantMs, config.flowPerRpm, config.motorMaxRpm),
      pressure_(config.tubingResistance, config.baselinePressure),
      syringes_(config.contrastVolumeMl, config.salineVolumeMl) {
    currentPressure_ = pressure_.compute(0.0);
}

double SimulatedHal::readPressure() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentPressure_;
}

double SimulatedHal::readMotorRpm() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return motor_.actualRpm();
}

bool SimulatedHal::readAirDetector() const {
    return airDetector_.airPresent();  // atomic, no mutex needed
}

double SimulatedHal::readSyringeVolume(Barrel barrel) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (barrel == Barrel::Contrast) {
        return syringes_.contrastRemaining();
    }
    return syringes_.salineRemaining();
}

void SimulatedHal::setMotorRpm(double rpm) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    motor_.setCommandedRpm(rpm);
}

void SimulatedHal::setValve(FluidChannel channel, ValveState state) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    valves_.set(channel, state);
}

void SimulatedHal::emergencyStop() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    motor_.emergencyStop();
    valves_.closeAll();
}

void SimulatedHal::tick(double dt) {
    std::lock_guard<std::mutex> lock(stateMutex_);

    // 1. Motor model: update actual RPM
    motor_.tick(dt);

    // 2. Flow rate: compute from motor
    double flowRate = motor_.flowRate();

    // 3. Active valve: determine effective flow and which barrel drains
    //    Flow only occurs through an open valve
    bool contrastOpen = valves_.isOpen(FluidChannel::Contrast);
    bool salineOpen = valves_.isOpen(FluidChannel::Saline);

    if (contrastOpen) {
        syringes_.drain(FluidChannel::Contrast, flowRate, dt);
    } else if (salineOpen) {
        syringes_.drain(FluidChannel::Saline, flowRate, dt);
    }

    // Effective flow is zero if no valve is open
    double effectiveFlow = (contrastOpen || salineOpen) ? flowRate : 0.0;
    currentFlowRate_ = effectiveFlow;

    // 5. Pressure model: compute from effective flow rate
    currentPressure_ = pressure_.compute(effectiveFlow);

    // 6. Air detector: state unchanged unless fault injected (handled externally)
}

void SimulatedHal::injectFault(const SimulatedFault& fault) {
    switch (fault.type) {
        case SimulatedFault::Type::Overpressure:
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                pressure_.setPressureOverride(fault.targetPsi);
                currentPressure_ = fault.targetPsi;  // immediately visible to readPressure()
            }
            break;
        case SimulatedFault::Type::AirBubble:
            airDetector_.setAirPresent(true);  // atomic, no mutex needed
            break;
        case SimulatedFault::Type::MotorStall:
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                motor_.setMaxRpmOverride(fault.maxRpm);
            }
            break;
        case SimulatedFault::Type::PartialOcclusion:
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                pressure_.setResistanceMultiplier(fault.resistanceMultiplier);
            }
            break;
        case SimulatedFault::Type::TimingDelay:
            // TimingDelay is handled by the control loop, not the HAL
            break;
    }
}

void SimulatedHal::clearFaults() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        motor_.clearFaults();
        pressure_.clearFaults();
        syringes_.resetToFull();
    }
    airDetector_.setAirPresent(false);  // atomic
}

double SimulatedHal::currentFlowRate() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentFlowRate_;
}

}  // namespace injector::hal
