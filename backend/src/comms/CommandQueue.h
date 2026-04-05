#pragma once

#include "hal/IHalInterface.h"
#include "state/Protocol.h"

#include <mutex>
#include <optional>
#include <queue>

namespace injector::comms {

/// Command sent from gRPC server handlers to the State Machine thread.
struct InternalCommand {
    enum class Type {
        Arm,
        Disarm,
        Start,
        Pause,
        Resume,
        Reset,
        EmergencyStop,
        LoadProtocol,
        InjectFault,
        PhaseComplete  // sent by ControlLoop when phase volume threshold is reached
    };

    Type type = Type::Arm;
    state::Protocol protocol;       // populated for LoadProtocol
    hal::SimulatedFault fault{};    // populated for InjectFault
    int phaseIndex = -1;            // populated for PhaseComplete
    double volumeDelivered = 0.0;   // populated for PhaseComplete
};

/// Thread-safe FIFO queue for InternalCommand.
/// Multiple producers (gRPC threads); single consumer (State Machine thread).
class CommandQueue {
public:
    /// Enqueue a command (thread-safe, non-blocking).
    void push(InternalCommand cmd);

    /// Dequeue the oldest command, or nullopt if empty (thread-safe, non-blocking).
    [[nodiscard]] std::optional<InternalCommand> pop();

    /// Returns true if no commands are pending (thread-safe).
    [[nodiscard]] bool empty() const;

    /// Number of commands pending (thread-safe).
    [[nodiscard]] size_t size() const;

private:
    mutable std::mutex mutex_;
    std::queue<InternalCommand> queue_;
};

}  // namespace injector::comms
