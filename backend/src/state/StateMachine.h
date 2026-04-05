#pragma once

#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/FaultQueue.h"
#include "hal/IHalInterface.h"
#include "state/InjectorState.h"
#include "state/Protocol.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace injector::state {

struct CommandResult {
    bool success = false;
    std::string error;
};

/// State machine for the medical injector. Owns state transitions, protocol storage,
/// command validation, and event emission. Intentionally free of threading concerns in
/// its core logic — all public synchronous methods can be called directly from tests.
/// The optional thread (start/stop) drives the same logic from queues at runtime.
class StateMachine {
public:
    /// All pointer parameters are optional (may be null in tests).
    StateMachine(ControlTargets* targets,
                 comms::CommandQueue* commandQueue,
                 comms::FaultQueue* faultQueue,
                 comms::EventBroadcast* events,
                 hal::IHalInterface* hal = nullptr);

    ~StateMachine();

    // ── Synchronous processing (usable from tests without threads) ──────────

    /// Process a command. Validates the transition, updates state, writes to targets,
    /// emits events. Returns success/error result.
    CommandResult processCommand(const comms::InternalCommand& cmd);

    /// Process a fault event from the safety monitor (transitions to Fault state).
    void processFaultEvent(const FaultEvent& fault);

    /// Called when the control loop completes a phase. Posts a PhaseComplete command
    /// to the queue if running threaded; called directly in tests.
    void notifyPhaseComplete(int phaseIndex, double volumeDelivered);

    // ── State accessors (thread-safe) ────────────────────────────────────────

    [[nodiscard]] InjectorState currentState() const;
    [[nodiscard]] int currentPhaseIndex() const;
    [[nodiscard]] std::vector<double> volumeDeliveredPerPhase() const;
    [[nodiscard]] std::vector<FaultEvent> activeFaults() const;
    [[nodiscard]] bool hasProtocol() const;
    [[nodiscard]] Protocol loadedProtocol() const;

    // ── Thread management ────────────────────────────────────────────────────

    void start();
    void stop();
    [[nodiscard]] bool running() const;

private:
    void run();

    // Helpers (called under stateMutex_)
    void transitionTo(InjectorState newState, const std::string& trigger);
    void applyPhaseToTargets(int phaseIndex);
    void clearTargets();
    void emitEvent(EventType type, std::string detailsJson);
    void emitStateTransitionEvent(InjectorState from, InjectorState to,
                                  const std::string& trigger);
    void emitPhaseTransitionEvent(int fromPhase, int toPhase, double volumeDelivered);
    void emitProtocolLoadedEvent(const Protocol& proto);
    void emitCommandRejectedEvent(const std::string& command, const std::string& reason);

    // Wired dependencies (all optional / nullable)
    ControlTargets* targets_;
    comms::CommandQueue* commandQueue_;
    comms::FaultQueue* faultQueue_;
    comms::EventBroadcast* events_;
    hal::IHalInterface* hal_;

    // Core state (held under stateMutex_ for thread-safe reads by accessors)
    mutable std::mutex stateMutex_;
    InjectorState state_ = InjectorState::Idle;
    Protocol loadedProtocol_;
    int phaseIndex_ = 0;
    std::vector<double> volumeDeliveredPerPhase_;
    std::vector<FaultEvent> activeFaults_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    std::chrono::steady_clock::time_point startTime_;
};

}  // namespace injector::state
