#pragma once

#include "hal/AirDetectorModel.h"
#include "hal/IHalInterface.h"
#include "hal/IMotorModel.h"
#include "hal/MotorModelConfig.h"
#include "hal/PressureModel.h"
#include "hal/SyringeModel.h"
#include "hal/ValveModel.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace injector::hal {

struct HalConfig {
    double tubingResistance = 50.0;
    double baselinePressure = 10.0;
    /// First-order lag applied to the pressure model (tubing/syringe/patient
    /// compliance). Pressure rises/falls toward the steady-state target with
    /// this time constant.
    double pressureTimeConstantMs = 400.0;
    double contrastVolumeMl = 100.0;
    double salineVolumeMl = 50.0;
    MotorModelConfig motorModel;
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

    // Controller-facing prediction (delegates to motor model)
    double predictDecelVolume(double commandDecelRate) const override;

    // Additional accessors for telemetry
    double currentFlowRate() const;

private:
    mutable std::mutex stateMutex_;
    std::unique_ptr<IMotorModel> motor_;
    PressureModel pressure_;
    ValveModel valves_;
    SyringeModel syringes_;
    double currentFlowRate_ = 0.0;
    double currentPressure_ = 0.0;

    AirDetectorModel airDetector_;  // atomic internally, lock-free reads
};

}  // namespace injector::hal
