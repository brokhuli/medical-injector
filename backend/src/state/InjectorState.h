#pragma once

#include <cstdint>
#include <string>

namespace injector::state {

/// Six-state injector state machine enum. Values match the proto InjectorState enum.
enum class InjectorState {
    Idle = 0,
    Armed = 1,
    Injecting = 2,
    Paused = 3,
    Completed = 4,
    Fault = 5
};

inline const char* toString(InjectorState s) {
    switch (s) {
        case InjectorState::Idle:      return "Idle";
        case InjectorState::Armed:     return "Armed";
        case InjectorState::Injecting: return "Injecting";
        case InjectorState::Paused:    return "Paused";
        case InjectorState::Completed: return "Completed";
        case InjectorState::Fault:     return "Fault";
    }
    return "Unknown";
}

/// Fault types that the safety monitor or fault injection can raise.
enum class FaultType {
    Overpressure = 0,
    AirBubble = 1,
    MotorStall = 2,
    PartialOcclusion = 3,
    TimingDelay = 4
};

inline const char* toString(FaultType f) {
    switch (f) {
        case FaultType::Overpressure:     return "OVERPRESSURE";
        case FaultType::AirBubble:        return "AIR_BUBBLE";
        case FaultType::MotorStall:       return "MOTOR_STALL";
        case FaultType::PartialOcclusion: return "PARTIAL_OCCLUSION";
        case FaultType::TimingDelay:      return "TIMING_DELAY";
    }
    return "UNKNOWN";
}

/// Fault event sent from the Safety Monitor to the State Machine.
struct FaultEvent {
    FaultType type = FaultType::Overpressure;
    double measuredValue = 0.0;   // sensor reading that triggered the fault
    double threshold = 0.0;       // configured limit that was exceeded
    uint64_t timestampUs = 0;     // microseconds since process start (steady_clock)
};

/// Event types broadcast from the State Machine / Safety Monitor to gRPC streaming.
enum class EventType {
    StateTransition = 0,
    FaultDetected = 1,
    PhaseTransition = 2,
    ProtocolLoaded = 3,
    CommandRejected = 4
};

/// Internal event sent to the gRPC StreamEvents handler.
struct InternalEvent {
    uint64_t timestampUs = 0;      // microseconds since process start
    EventType type = EventType::StateTransition;
    std::string detailsJson;       // JSON string matching the schemas in spec 04
};

}  // namespace injector::state
