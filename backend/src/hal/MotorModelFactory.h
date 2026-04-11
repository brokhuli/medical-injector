#pragma once

#include "hal/IMotorModel.h"
#include "hal/MotorModelConfig.h"

#include <memory>

namespace injector::hal {

/// Constructs an `IMotorModel` instance from a configuration struct.
///
/// Recognised `cfg.type` values:
///   - "first_order"   — `FirstOrderMotorModel`
///   - "second_order"  — `SecondOrderMotorModel`
///
/// Throws `std::invalid_argument` if `cfg.type` is not recognised.
std::unique_ptr<IMotorModel> createMotorModel(const MotorModelConfig& cfg);

}  // namespace injector::hal
