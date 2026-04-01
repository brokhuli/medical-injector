#pragma once

#include "hal/IHalInterface.h"

namespace injector::hal {

class SyringeModel {
public:
    explicit SyringeModel(double contrastCapacity = 100.0,
                          double salineCapacity = 50.0);

    void drain(FluidChannel channel, double flowRate, double dt);
    void resetToFull();

    double contrastRemaining() const { return contrastRemaining_; }
    double salineRemaining() const { return salineRemaining_; }
    double contrastCapacity() const { return contrastCapacity_; }
    double salineCapacity() const { return salineCapacity_; }

private:
    double contrastRemaining_;
    double salineRemaining_;
    double contrastCapacity_;
    double salineCapacity_;
};

}  // namespace injector::hal
