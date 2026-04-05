#include "comms/TelemetryBroadcast.h"

namespace injector::comms {

void TelemetryBroadcast::push(TelemetrySnapshot snapshot) {
    buffer_.push(std::move(snapshot));
}

std::optional<TelemetrySnapshot> TelemetryBroadcast::pop() {
    return buffer_.pop();
}

size_t TelemetryBroadcast::available() const {
    return buffer_.available();
}

}  // namespace injector::comms
