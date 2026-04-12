#include "hal/SimulatedHal.h"

namespace injector::hal {

SimulatedHal::SimulatedHal(const HalConfig& config)
    : contrastResistance_(config.contrastResistance),
      salineResistance_(config.salineResistance),
      motor_(config.motorTimeConstantMs, config.flowPerRpm, config.maxRpm),
      pressure_(config.baselinePressure, config.pressureTimeConstantMs),
      syringes_(config.contrastVolumeMl, config.salineVolumeMl) {
    currentPressure_ = pressure_.filteredPressure();
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

    // 3. Active valve: determine effective flow and which barrel drains.
    //    Flow only occurs through an open valve. The active fluid also
    //    determines which tubing resistance the pressure model sees.
    bool contrastOpen = valves_.isOpen(FluidChannel::Contrast);
    bool salineOpen = valves_.isOpen(FluidChannel::Saline);

    double activeResistance = contrastResistance_;
    if (contrastOpen) {
        syringes_.drain(FluidChannel::Contrast, flowRate, dt);
        activeResistance = contrastResistance_;
    } else if (salineOpen) {
        syringes_.drain(FluidChannel::Saline, flowRate, dt);
        activeResistance = salineResistance_;
    }

    // Effective flow is zero if no valve is open. Flow drops immediately on
    // valve close; the pressure model's first-order lag produces the
    // characteristic slow pressure decay.
    double effectiveFlow = (contrastOpen || salineOpen) ? flowRate : 0.0;
    currentFlowRate_ = effectiveFlow;

    // 4. Pressure model: first-order lag toward compute(effectiveFlow, R)
    currentPressure_ = pressure_.step(effectiveFlow, activeResistance, dt);

    // 5. Air detector: state unchanged unless fault injected (handled externally)
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

double SimulatedHal::predictDecelVolume(double commandDecelRate) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return motor_.predictDecelVolume(commandDecelRate);
}

double SimulatedHal::currentFlowRate() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentFlowRate_;
}

}  // namespace injector::hal
