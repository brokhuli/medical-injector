#include "state/StateMachine.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>

namespace injector::state {

namespace {

uint64_t nowUs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

const char* commandName(comms::InternalCommand::Type t) {
    using T = comms::InternalCommand::Type;
    switch (t) {
        case T::Arm:           return "ARM";
        case T::Disarm:        return "DISARM";
        case T::Start:         return "START";
        case T::Pause:         return "PAUSE";
        case T::Resume:        return "RESUME";
        case T::Reset:         return "RESET";
        case T::EmergencyStop: return "EMERGENCY_STOP";
        case T::LoadProtocol:  return "LOAD_PROTOCOL";
        case T::InjectFault:   return "INJECT_FAULT";
        case T::PhaseComplete: return "PHASE_COMPLETE";
    }
    return "UNKNOWN";
}

}  // namespace

StateMachine::StateMachine(ControlTargets* targets,
                           comms::CommandQueue* commandQueue,
                           comms::FaultQueue* faultQueue,
                           comms::EventBroadcast* events,
                           hal::IHalInterface* hal)
    : targets_(targets),
      commandQueue_(commandQueue),
      faultQueue_(faultQueue),
      events_(events),
      hal_(hal),
      startTime_(std::chrono::steady_clock::now()) {}

StateMachine::~StateMachine() {
    stop();
}

// ── Synchronous command processing ──────────────────────────────────────────

CommandResult StateMachine::processCommand(const comms::InternalCommand& cmd) {
    using T = comms::InternalCommand::Type;
    std::lock_guard<std::mutex> lock(stateMutex_);

    switch (cmd.type) {

        // ── ARM ──────────────────────────────────────────────────────────────
        case T::Arm: {
            if (state_ == InjectorState::Armed) {
                return {true, ""};  // idempotent
            }
            if (state_ != InjectorState::Idle) {
                std::string err =
                    "Cannot arm: current state is " + std::string(toString(state_));
                emitCommandRejectedEvent("ARM", "INVALID_STATE_TRANSITION");
                return {false, err};
            }
            if (loadedProtocol_.empty()) {
                emitCommandRejectedEvent("ARM", "No protocol loaded");
                return {false, "No protocol loaded"};
            }
            transitionTo(InjectorState::Armed, "ARM");
            return {true, ""};
        }

        // ── DISARM ───────────────────────────────────────────────────────────
        case T::Disarm: {
            if (state_ == InjectorState::Idle) {
                return {true, ""};  // idempotent
            }
            if (state_ != InjectorState::Armed) {
                std::string err =
                    "Cannot disarm: current state is " + std::string(toString(state_));
                emitCommandRejectedEvent("DISARM", "INVALID_STATE_TRANSITION");
                return {false, err};
            }
            transitionTo(InjectorState::Idle, "DISARM");
            return {true, ""};
        }

        // ── START ────────────────────────────────────────────────────────────
        case T::Start: {
            if (state_ == InjectorState::Injecting) {
                return {true, ""};  // idempotent
            }
            if (state_ != InjectorState::Armed) {
                std::string err =
                    "Cannot start: current state is " + std::string(toString(state_));
                emitCommandRejectedEvent("START", "INVALID_STATE_TRANSITION");
                return {false, err};
            }
            phaseIndex_ = 0;
            transitionTo(InjectorState::Injecting, "START");
            applyPhaseToTargets(phaseIndex_);
            return {true, ""};
        }

        // ── PAUSE ────────────────────────────────────────────────────────────
        case T::Pause: {
            if (state_ == InjectorState::Paused) {
                return {true, ""};  // idempotent
            }
            if (state_ != InjectorState::Injecting) {
                std::string err =
                    "Cannot pause: current state is " + std::string(toString(state_));
                emitCommandRejectedEvent("PAUSE", "INVALID_STATE_TRANSITION");
                return {false, err};
            }
            transitionTo(InjectorState::Paused, "PAUSE");
            clearTargets();
            return {true, ""};
        }

        // ── RESUME ───────────────────────────────────────────────────────────
        case T::Resume: {
            if (state_ == InjectorState::Injecting) {
                return {true, ""};  // idempotent
            }
            if (state_ != InjectorState::Paused) {
                std::string err =
                    "Cannot resume: current state is " + std::string(toString(state_));
                emitCommandRejectedEvent("RESUME", "INVALID_STATE_TRANSITION");
                return {false, err};
            }
            transitionTo(InjectorState::Injecting, "RESUME");
            applyPhaseToTargets(phaseIndex_);
            return {true, ""};
        }

        // ── RESET ────────────────────────────────────────────────────────────
        case T::Reset: {
            if (state_ == InjectorState::Idle) {
                return {true, ""};  // idempotent
            }
            if (state_ != InjectorState::Fault) {
                std::string err =
                    "Cannot reset: current state is " + std::string(toString(state_));
                emitCommandRejectedEvent("RESET", "INVALID_STATE_TRANSITION");
                return {false, err};
            }
            activeFaults_.clear();
            if (hal_) {
                hal_->clearFaults();
            }
            transitionTo(InjectorState::Idle, "RESET");
            return {true, ""};
        }

        // ── EMERGENCY_STOP ───────────────────────────────────────────────────
        case T::EmergencyStop: {
            if (state_ == InjectorState::Idle) {
                return {true, ""};  // already safe
            }
            if (hal_) {
                hal_->emergencyStop();
            }
            clearTargets();
            FaultEvent fe;
            fe.type = FaultType::Overpressure;  // placeholder; EMERGENCY_STOP is user-initiated
            fe.timestampUs = nowUs();
            activeFaults_.push_back(fe);
            transitionTo(InjectorState::Fault, "EMERGENCY_STOP");
            return {true, ""};
        }

        // ── LOAD_PROTOCOL ────────────────────────────────────────────────────
        case T::LoadProtocol: {
            if (state_ != InjectorState::Idle) {
                std::string err = "Protocol can only be loaded in Idle state";
                emitCommandRejectedEvent("LOAD_PROTOCOL", err);
                return {false, err};
            }
            // Validate
            const Protocol& proto = cmd.protocol;
            if (proto.empty()) {
                return {false, "Protocol must have at least one phase"};
            }
            if (proto.phaseCount() > 20) {
                return {false, "Protocol exceeds maximum of 20 phases"};
            }
            for (const auto& phase : proto.phases) {
                if (phase.flowRate < 0.1 || phase.flowRate > 10.0) {
                    return {false, "Flow rate must be between 0.1 and 10.0 mL/s"};
                }
                if (phase.volume < 1.0 || phase.volume > 200.0) {
                    return {false, "Volume must be between 1.0 and 200.0 mL"};
                }
                if (phase.pressureLimit < 50.0 || phase.pressureLimit > 400.0) {
                    return {false, "Pressure limit must be between 50 and 400 psi"};
                }
            }
            loadedProtocol_ = proto;
            volumeDeliveredPerPhase_.assign(proto.phaseCount(), 0.0);
            phaseIndex_ = 0;

            // Write protocol to ControlTargets under its mutex
            if (targets_) {
                std::lock_guard<std::mutex> pl(targets_->protocolMutex);
                targets_->loadedProtocol = proto;
            }

            emitProtocolLoadedEvent(proto);
            spdlog::info("Protocol loaded: {} phases", proto.phaseCount());
            return {true, ""};
        }

        // ── INJECT_FAULT ─────────────────────────────────────────────────────
        case T::InjectFault: {
            if (hal_) {
                hal_->injectFault(cmd.fault);
            }
            return {true, ""};
        }

        // ── PHASE_COMPLETE ───────────────────────────────────────────────────
        case T::PhaseComplete: {
            if (state_ != InjectorState::Injecting) {
                return {true, ""};  // ignore if not injecting (race with pause/stop)
            }
            if (cmd.phaseIndex < 0 ||
                static_cast<size_t>(cmd.phaseIndex) >= volumeDeliveredPerPhase_.size()) {
                return {true, ""};  // stale or out-of-range
            }
            volumeDeliveredPerPhase_[static_cast<size_t>(cmd.phaseIndex)] =
                cmd.volumeDelivered;

            int nextPhase = cmd.phaseIndex + 1;
            if (static_cast<size_t>(nextPhase) >= loadedProtocol_.phaseCount()) {
                // All phases done
                clearTargets();
                transitionTo(InjectorState::Completed, "AllPhasesDelivered");
            } else {
                // Emit phase transition event then advance
                emitPhaseTransitionEvent(cmd.phaseIndex, nextPhase, cmd.volumeDelivered);
                phaseIndex_ = nextPhase;
                applyPhaseToTargets(nextPhase);
            }
            return {true, ""};
        }
    }

    return {false, "Unknown command type"};
}

void StateMachine::processFaultEvent(const FaultEvent& fault) {
    std::lock_guard<std::mutex> lock(stateMutex_);

    if (state_ == InjectorState::Idle || state_ == InjectorState::Completed) {
        return;  // safety monitor shouldn't fire here; ignore gracefully
    }

    activeFaults_.push_back(fault);
    clearTargets();

    nlohmann::json d;
    d["fault_type"] = toString(fault.type);
    d["value"] = fault.measuredValue;
    d["threshold"] = fault.threshold;
    emitEvent(EventType::FaultDetected, d.dump());

    transitionTo(InjectorState::Fault, toString(fault.type));
}

void StateMachine::notifyPhaseComplete(int phaseIndex, double volumeDelivered) {
    comms::InternalCommand cmd;
    cmd.type = comms::InternalCommand::Type::PhaseComplete;
    cmd.phaseIndex = phaseIndex;
    cmd.volumeDelivered = volumeDelivered;

    if (commandQueue_) {
        commandQueue_->push(cmd);
    } else {
        // Direct call (test mode without queues)
        processCommand(cmd);
    }
}

// ── State accessors ──────────────────────────────────────────────────────────

InjectorState StateMachine::currentState() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

int StateMachine::currentPhaseIndex() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return phaseIndex_;
}

std::vector<double> StateMachine::volumeDeliveredPerPhase() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return volumeDeliveredPerPhase_;
}

std::vector<FaultEvent> StateMachine::activeFaults() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return activeFaults_;
}

bool StateMachine::hasProtocol() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return !loadedProtocol_.empty();
}

Protocol StateMachine::loadedProtocol() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return loadedProtocol_;
}

// ── Thread management ────────────────────────────────────────────────────────

void StateMachine::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return;
    }
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&StateMachine::run, this);
}

void StateMachine::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool StateMachine::running() const {
    return running_.load(std::memory_order_acquire);
}

void StateMachine::run() {
    while (running_.load(std::memory_order_acquire)) {
        // Drain fault queue first (higher priority)
        if (faultQueue_) {
            while (auto fault = faultQueue_->pop()) {
                processFaultEvent(*fault);
            }
        }

        // Drain command queue
        if (commandQueue_) {
            while (auto cmd = commandQueue_->pop()) {
                processCommand(*cmd);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// ── Private helpers ──────────────────────────────────────────────────────────

void StateMachine::transitionTo(InjectorState newState, const std::string& trigger) {
    InjectorState from = state_;
    state_ = newState;
    if (targets_) {
        targets_->currentState.store(static_cast<int>(newState), std::memory_order_release);
    }
    spdlog::info("State: {} → {} ({})", toString(from), toString(newState),
                 trigger);
    emitStateTransitionEvent(from, newState, trigger);
}

void StateMachine::applyPhaseToTargets(int phaseIndex) {
    if (!targets_ || loadedProtocol_.empty()) {
        return;
    }
    if (phaseIndex < 0 ||
        static_cast<size_t>(phaseIndex) >= loadedProtocol_.phaseCount()) {
        return;
    }
    const Phase& phase = loadedProtocol_.phases[static_cast<size_t>(phaseIndex)];
    targets_->targetFlowRate.store(phase.flowRate, std::memory_order_release);
    targets_->pressureLimit.store(phase.pressureLimit, std::memory_order_release);
    targets_->currentPhaseVolumeTarget.store(phase.volume, std::memory_order_release);
    targets_->activePhaseIndex.store(phaseIndex, std::memory_order_release);
    targets_->injecting.store(true, std::memory_order_release);
    targets_->activeFluidChannel.store(
        phase.fluidType == FluidType::Saline ? 1 : 0, std::memory_order_release);
}

void StateMachine::clearTargets() {
    if (!targets_) {
        return;
    }
    targets_->injecting.store(false, std::memory_order_release);
    targets_->targetFlowRate.store(0.0, std::memory_order_release);
    targets_->currentPhaseVolumeTarget.store(0.0, std::memory_order_release);
    targets_->activePhaseIndex.store(-1, std::memory_order_release);
}

void StateMachine::emitEvent(EventType type, std::string detailsJson) {
    if (!events_) {
        return;
    }
    InternalEvent ev;
    ev.timestampUs = nowUs();
    ev.type = type;
    ev.detailsJson = std::move(detailsJson);
    events_->publish(std::move(ev));
}

void StateMachine::emitStateTransitionEvent(InjectorState from, InjectorState to,
                                            const std::string& trigger) {
    nlohmann::json d;
    d["from"] = toString(from);
    d["to"] = toString(to);
    d["trigger"] = trigger;
    emitEvent(EventType::StateTransition, d.dump());
}

void StateMachine::emitPhaseTransitionEvent(int fromPhase, int toPhase,
                                            double volumeDelivered) {
    double programmedVolume = 0.0;
    if (fromPhase >= 0 &&
        static_cast<size_t>(fromPhase) < loadedProtocol_.phaseCount()) {
        programmedVolume = loadedProtocol_.phases[static_cast<size_t>(fromPhase)].volume;
    }
    nlohmann::json d;
    d["from_phase"] = fromPhase;
    d["to_phase"] = toPhase;
    d["volume_delivered"] = volumeDelivered;
    d["volume_programmed"] = programmedVolume;
    emitEvent(EventType::PhaseTransition, d.dump());
}

void StateMachine::emitProtocolLoadedEvent(const Protocol& proto) {
    double totalContrast = 0.0;
    double totalSaline = 0.0;
    double totalVolume = 0.0;
    for (const auto& phase : proto.phases) {
        totalVolume += phase.volume;
        if (phase.fluidType == FluidType::Contrast) {
            totalContrast += phase.volume;
        } else {
            totalSaline += phase.volume;
        }
    }
    nlohmann::json d;
    d["phase_count"] = proto.phaseCount();
    d["total_volume"] = totalVolume;
    d["total_contrast"] = totalContrast;
    d["total_saline"] = totalSaline;
    emitEvent(EventType::ProtocolLoaded, d.dump());
}

void StateMachine::emitCommandRejectedEvent(const std::string& command,
                                            const std::string& reason) {
    nlohmann::json d;
    d["command"] = command;
    d["current_state"] = toString(state_);
    d["reason"] = reason;
    emitEvent(EventType::CommandRejected, d.dump());
}

}  // namespace injector::state
