#pragma once

#include <string>

namespace injector::hal {

/// Configuration for a motor model. Flat struct so it maps cleanly to JSON.
/// Each concrete model reads only the fields it cares about.
struct MotorModelConfig {
    /// Model type string. Recognised values:
    ///   "first_order"   — first-order exponential lag (default)
    ///   "second_order"  — underdamped second-order with inertia
    std::string type = "first_order";

    // Common parameters ----------------------------------------------------
    double flowPerRpm = 0.01;     // mL/s per RPM (linear conversion)
    double maxRpm = 1500.0;

    // first_order parameters -----------------------------------------------
    double timeConstantMs = 50.0; // first-order lag τ

    // second_order parameters ----------------------------------------------
    /// Undamped natural frequency (Hz). Controls how fast the motor responds.
    double naturalFrequencyHz = 8.0;
    /// Damping ratio. 1.0 = critically damped, <1.0 = underdamped (overshoot),
    /// >1.0 = overdamped (sluggish). Default 0.7 gives mild overshoot.
    double dampingRatio = 0.7;
};

}  // namespace injector::hal
