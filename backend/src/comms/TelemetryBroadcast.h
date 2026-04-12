#pragma once

#include "logging/RingBuffer.h"
#include "state/InjectorState.h"

#include <optional>
#include <vector>

namespace injector::comms {

/// Latest telemetry snapshot written by the Control Loop and read by gRPC StreamTelemetry.
struct TelemetrySnapshot {
    double timestampS = 0.0;                         // seconds since injection start
    state::InjectorState state = state::InjectorState::Idle;
    int phaseIndex = -1;                             // -1 when no active phase
    double targetFlowRate = 0.0;                     // mL/s
    double actualFlowRate = 0.0;                     // mL/s
    double pressure = 0.0;                           // psi
    double motorRpm = 0.0;                           // actual RPM
    std::vector<double> volumePerPhase;              // mL per phase
    double totalVolumeDelivered = 0.0;               // mL
    double totalProgrammedVolume = 0.0;              // mL
    double elapsedTime = 0.0;                        // seconds since INJECTING entry; frozen on PAUSED, 0 on IDLE/ARMED
    double contrastRemaining = 0.0;                  // mL
    double salineRemaining = 0.0;                    // mL
};

/// SPSC ring buffer wrapper for telemetry snapshots.
/// Producer: Control Loop thread (every tick).
/// Consumer: gRPC StreamTelemetry handler (coalesces to configured rate).
/// Overwrite-oldest when full — the consumer always sees the latest data.
class TelemetryBroadcast {
public:
    static constexpr size_t kCapacity = 128;

    TelemetryBroadcast() : buffer_(kCapacity) {}

    /// Push a snapshot. Overwrites oldest if full. Call from producer thread only.
    void push(TelemetrySnapshot snapshot);

    /// Pop the oldest unread snapshot, or nullopt if none available.
    /// Call from consumer thread only.
    [[nodiscard]] std::optional<TelemetrySnapshot> pop();

    /// Number of snapshots available to the consumer.
    [[nodiscard]] size_t available() const;

private:
    logging::RingBuffer<TelemetrySnapshot> buffer_;
};

}  // namespace injector::comms
