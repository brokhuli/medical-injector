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
    // Pressure model ------------------------------------------------------
    /// Tubing resistance while contrast is flowing (psi per mL/s).
    /// Contrast media is more viscous than saline → higher pressure for the
    /// same flow rate.
    double contrastResistance = 45.0;
    /// Tubing resistance while saline is flowing (psi per mL/s).
    double salineResistance = 35.0;
    double baselinePressure = 10.0;
    /// First-order lag applied to the pressure model (tubing/syringe/patient
    /// compliance). Pressure rises/falls toward the steady-state target with
    /// this time constant.
    double pressureTimeConstantMs = 400.0;

    // Motor model (first-order lag) --------------------------------------
    double motorTimeConstantMs = 50.0;
    double flowPerRpm = 0.01;     // mL/s per RPM
    double maxRpm = 1500.0;

    // Syringes ------------------------------------------------------------
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

    // Controller-facing prediction (delegates to motor model)
    double predictDecelVolume(double commandDecelRate) const override;

    // Additional accessors for telemetry
    double currentFlowRate() const;

private:
    mutable std::mutex stateMutex_;
    double contrastResistance_;
    double salineResistance_;
    MotorModel motor_;
    PressureModel pressure_;
    ValveModel valves_;
    SyringeModel syringes_;
    double currentFlowRate_ = 0.0;
    double currentPressure_ = 0.0;

    AirDetectorModel airDetector_;  // atomic internally, lock-free reads
};

}  // namespace injector::hal
