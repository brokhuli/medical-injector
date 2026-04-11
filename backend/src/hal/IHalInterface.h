#pragma once

#include <cstdint>

namespace injector::hal {

enum class Barrel { Contrast, Saline };
enum class FluidChannel { Contrast, Saline };
enum class ValveState { Open, Closed };

struct SimulatedFault {
    enum class Type {
        Overpressure,
        AirBubble,
        MotorStall,
        PartialOcclusion,
        TimingDelay
    };

    Type type;
    double targetPsi = 0.0;
    double maxRpm = 0.0;
    double resistanceMultiplier = 1.0;
    double delayMs = 0.0;
};

class IHalInterface {
public:
    virtual ~IHalInterface() = default;

    // Sensors (read)
    virtual double readPressure() const = 0;
    virtual double readMotorRpm() const = 0;
    virtual bool readAirDetector() const = 0;
    virtual double readSyringeVolume(Barrel barrel) const = 0;

    // Actuators (write)
    virtual void setMotorRpm(double rpm) = 0;
    virtual void setValve(FluidChannel channel, ValveState state) = 0;
    virtual void emergencyStop() = 0;

    // Simulation control
    virtual void tick(double dt) = 0;
    virtual void injectFault(const SimulatedFault& fault) = 0;
    virtual void clearFaults() = 0;

    // Prediction hooks (controller-facing, implementation-private)

    /// Predict the volume (mL) that will be delivered between now and when
    /// the motor fully stops, assuming the commanded flow begins decelerating
    /// linearly at `commandDecelRate` (mL/s²) from the current motor state.
    ///
    /// Used by the control loop to decide when to trigger end-of-phase
    /// deceleration. Encapsulates motor-specific dynamics so the control
    /// loop does not need to know whether the HAL drives a first-order
    /// motor, a second-order motor, or real hardware.
    virtual double predictDecelVolume(double commandDecelRate) const = 0;
};

}  // namespace injector::hal
