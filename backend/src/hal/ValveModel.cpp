#include "hal/ValveModel.h"

namespace injector::hal {

bool ValveModel::isOpen(FluidChannel channel) const {
    if (channel == FluidChannel::Contrast) {
        return contrastValve_ == ValveState::Open;
    }
    return salineValve_ == ValveState::Open;
}

void ValveModel::set(FluidChannel channel, ValveState state) {
    if (channel == FluidChannel::Contrast) {
        contrastValve_ = state;
    } else {
        salineValve_ = state;
    }
}

void ValveModel::closeAll() {
    contrastValve_ = ValveState::Closed;
    salineValve_ = ValveState::Closed;
}

}  // namespace injector::hal
