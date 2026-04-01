#pragma once

#include "hal/IHalInterface.h"

namespace injector::hal {

class ValveModel {
public:
    bool isOpen(FluidChannel channel) const;
    void set(FluidChannel channel, ValveState state);
    void closeAll();

    ValveState contrastValve() const { return contrastValve_; }
    ValveState salineValve() const { return salineValve_; }

private:
    ValveState contrastValve_ = ValveState::Closed;
    ValveState salineValve_ = ValveState::Closed;
};

}  // namespace injector::hal
