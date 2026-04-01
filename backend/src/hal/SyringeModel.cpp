#include "hal/SyringeModel.h"

#include <algorithm>

namespace injector::hal {

SyringeModel::SyringeModel(double contrastCapacity, double salineCapacity)
    : contrastRemaining_(contrastCapacity),
      salineRemaining_(salineCapacity),
      contrastCapacity_(contrastCapacity),
      salineCapacity_(salineCapacity) {}

void SyringeModel::drain(FluidChannel channel, double flowRate, double dt) {
    if (channel == FluidChannel::Contrast) {
        contrastRemaining_ = std::max(contrastRemaining_ - flowRate * dt, 0.0);
    } else {
        salineRemaining_ = std::max(salineRemaining_ - flowRate * dt, 0.0);
    }
}

void SyringeModel::resetToFull() {
    contrastRemaining_ = contrastCapacity_;
    salineRemaining_ = salineCapacity_;
}

}  // namespace injector::hal
