#pragma once

#include <atomic>

namespace injector::hal {

class AirDetectorModel {
public:
    bool airPresent() const { return airPresent_.load(std::memory_order_acquire); }
    void setAirPresent(bool present) { airPresent_.store(present, std::memory_order_release); }

private:
    std::atomic<bool> airPresent_{false};
};

}  // namespace injector::hal
