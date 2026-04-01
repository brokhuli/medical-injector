#pragma once

#include "hal/AirDetectorModel.h"
#include "hal/IHalInterface.h"
#include "hal/MotorModel.h"
#include "hal/PressureModel.h"
#include "hal/SyringeModel.h"
#include "hal/ValveModel.h"

#include <atomic>
#include <mutex>

namespace injector::hal {

struct HalConfig {
    double flowPerRpm = 0.01;
    double tubingResistance = 50.0;
    double baselinePressure = 10.0;
    double motorTimeConstantMs = 50.0;
    double motorMaxRpm = 1500.0;
    double contrastVolumeMl = 100.0;
    double salineVolumeMl = 50.0;
};

class SimulatedHal : public IHalInterface {
public:
    explicit SimulatedHal(const HalConfig& config = {});

    // Sensors (read)
    double readPressure() const override;
    double readMotorRpm() const override;
    bool readAirDetector() const override;
    double readSyringeVolume(Barrel barrel) const override;

    // Actuators (write)
    void setMotorRpm(double rpm) override;
    void setValve(FluidChannel channel, ValveState state) override;
    void emergencyStop() override;

    // Simulation control
    void tick(double dt) override;
    void injectFault(const SimulatedFault& fault) override;
    void clearFaults() override;

    // Additional accessors for telemetry
    double currentFlowRate() const;

private:
    mutable std::mutex stateMutex_;
    MotorModel motor_;
    PressureModel pressure_;
    ValveModel valves_;
    SyringeModel syringes_;
    double currentFlowRate_ = 0.0;
    double currentPressure_ = 0.0;

    AirDetectorModel airDetector_;  // atomic internally, lock-free reads
};

}  // namespace injector::hal
