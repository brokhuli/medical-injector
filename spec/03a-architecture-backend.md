# Backend Architecture

The backend is the real-time controller — a standalone C++ process that contains all 7 architectural layers (HAL, scheduling, control loop, state machine, safety monitor, command interface, data logging). It exposes a gRPC service for the frontend to connect to. The frontend runs as a separate process.

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Backend Process                           │
│                                                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              gRPC Server (Command Interface)          │  │
│  │         Commands ↓               ↑ Telemetry/Events   │  │
│  └──────────────┬───────────────────┴────────────────────┘  │
│                 │                   │                        │
│         ┌───────▼───────┐   ┌──────┴───────┐               │
│         │ Command Queue │   │ Telemetry    │               │
│         │ (thread-safe) │   │ Broadcast    │               │
│         └───────┬───────┘   └──────▲───────┘               │
│                 │                  │                        │
│  ┌──────────────▼──────────────────┤────────────────────┐  │
│  │         State Machine           │                     │  │
│  │   Idle → Armed → Injecting →    │   Protocol Phases   │  │
│  │   Paused → Completed → Fault    │                     │  │
│  └──────────────┬──────────────────┤────────────────────┘  │
│                 │                  │                        │
│  ┌──────────────▼──────────────────┤────────────────────┐  │
│  │         Control Loop Thread     │    (pinned core)    │  │
│  │                                 │                     │  │
│  │   ┌─────────────────────┐       │                     │  │
│  │   │    PID Controller   │       │                     │  │
│  │   │  target → error →   │       │                     │  │
│  │   │  motor command      │       │                     │  │
│  │   └─────────┬───────────┘       │                     │  │
│  │             │                   │                     │  │
│  │   ┌─────────▼───────────┐       │                     │  │
│  │   │        HAL          │  ──────► Telemetry snapshot │  │
│  │   │  Motor  Valves      │       │                     │  │
│  │   │  Pressure  Air      │       │                     │  │
│  │   │  Syringes           │       │                     │  │
│  │   └─────────────────────┘       │                     │  │
│  └─────────────────────────────────┤────────────────────┘  │
│                                    │                        │
│  ┌─────────────────────────────────┤────────────────────┐  │
│  │     Safety Monitor Thread       │   (independent)     │  │
│  │  Reads HAL state directly       │                     │  │
│  │  Can override motor + valves    ──► Fault events      │  │
│  │  Can force state → Fault        │                     │  │
│  └─────────────────────────────────┤────────────────────┘  │
│                                    │                        │
│  ┌─────────────────────────────────┤────────────────────┐  │
│  │         Data Logger             │                     │  │
│  │  Ring buffer (tick data)   ◄────┘                     │  │
│  │  Event log (transitions, faults, commands)            │  │
│  │  CSV/JSON export on demand                            │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
          ▲                              │
          │ gRPC (commands, faults)      │ gRPC stream (telemetry, events)
          │                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Frontend Process                          │
│                 (QML application — see 03b)                  │
└─────────────────────────────────────────────────────────────┘
```

## Component Boundaries

The backend is a **standalone C++ process with 4 threads** plus gRPC server threads. Not microservices — inter-thread latency within the backend would violate the 2 ms tick requirement. Not a single process with the UI — process isolation means a UI crash cannot affect the control loop or safety monitor.

### Thread 1: Control Loop (highest priority, pinned core)

**Owns:** PID controller, HAL interactions, volume tracking, phase progression
**Tick rate:** 2 ms (configurable)
**Responsibilities per tick:**
1. Read all sensors via HAL (pressure, motor RPM, air detector, syringe levels)
2. If `Injecting`: compute PID output, apply acceleration ramp, write motor command via HAL
3. Integrate actual flow rate → update cumulative volume
4. Check phase completion (volume threshold) → advance to next phase or complete
5. Snapshot telemetry data → push to telemetry broadcast (non-blocking)
6. Push tick data to data logger ring buffer (non-blocking)

**Does NOT:** Process UI commands, make state transition decisions (except phase advancement), perform safety checks.

**Threading:** Uses `std::thread` with `pthread_setaffinity_np()` (Linux) or `SetThreadAffinityMask()` (Windows) for core pinning. Does NOT use a framework event loop — manual sleep timing with `std::chrono::steady_clock` for deterministic ticks.

### Thread 2: Safety Monitor (high priority, independent)

**Owns:** All safety checks, emergency halt capability
**Tick rate:** 1 ms (runs faster than control loop). See **05-non-functional-requirements.md, Section 1.2** for latency requirements.
**Responsibilities per tick:**
1. Read sensors directly via HAL (independent reads, not shared state from control loop)
2. Check overpressure: `pressure > phase_pressure_limit`
3. Check air detection: `air_detector == true`
4. Check motor divergence: `|commanded_rpm - actual_rpm| > threshold` for N consecutive ticks
5. Check control loop timing: did the control loop miss its last deadline?
6. On any fault: immediately write motor stop + close valves via HAL, send fault event to state machine

**Critical design constraint:** The safety monitor reads HAL directly — it does not depend on the control loop to relay sensor data. If the control loop hangs, the safety monitor still works.

**Threading:** Uses `std::thread` with elevated priority (`SCHED_FIFO` on Linux, `THREAD_PRIORITY_HIGHEST` on Windows).

### Thread 3: State Machine + Data Logger (normal priority)

**Owns:** State transitions, protocol storage, command validation, data persistence
**Driven by:** Command queue (incoming gRPC commands) + events from control loop and safety monitor
**Responsibilities:**
1. Dequeue commands from thread-safe command queue
2. Validate command against current state (reject invalid transitions)
3. Execute transition (update shared state, notify control loop of new targets)
4. Receive events from control loop (phase complete, injection complete) and safety monitor (fault)
5. Push state change events to event broadcast → gRPC streaming picks up
6. Maintain ring buffer of tick-level data (written by control loop, read here for export)
7. Handle export requests (CSV/JSON) by draining ring buffer to file

### Thread Pool: gRPC Server (managed by gRPC runtime)

**Owns:** External communication with the frontend process
**Responsibilities:**
1. Accept gRPC connections from frontend client(s)
2. Deserialize incoming command RPCs → push to command queue
3. Service `StreamTelemetry` — subscribe to telemetry broadcast, stream frames to client at configured rate (default 50 ms / 20 Hz)
4. Service `StreamEvents` — subscribe to event broadcast, stream events to client immediately
5. Handle `ExportData` requests by triggering the data logger and returning results

**Threading:** gRPC manages its own thread pool for handling RPCs. The server implementation marshals requests into the backend's thread-safe queues and reads from broadcast channels — it never accesses HAL or control logic directly.

## Data Flow

### Command flow (Frontend → Backend)
```
Frontend gRPC client
  → Calls unary RPC (e.g., SendCommand)
  → gRPC server handler deserializes protobuf to Command
  → Enqueues to thread-safe command queue
  → State Machine dequeues, validates, executes
  → State change event pushed to event broadcast
  → StreamEvents RPC picks up event, sends to frontend
```

### Telemetry flow (Backend → Frontend)
```
Control Loop (every tick, 2 ms)
  → Snapshots current state into TelemetryFrame struct
  → Pushes to telemetry broadcast (non-blocking)
  → gRPC StreamTelemetry handler subscribes to broadcast
  → Coalesces/throttles to configured rate (default 50 ms / 20 Hz = every 25th tick)
  → Streams protobuf TelemetryFrame to frontend
```

### Safety override flow
```
Safety Monitor
  → Detects fault condition
  → Directly calls HAL: emergency_stop()
  → Sends FaultEvent to State Machine via thread-safe queue
  → State Machine transitions to Fault
  → Event broadcast → StreamEvents → frontend notified
```

## Tech Decision: Language — C++17

**Chosen:** C++17 (minimum), C++20 preferred where compiler supports it

**Why C++:**
- **Deterministic performance** — No garbage collector. No GC pauses disrupting the 2 ms tick.
- **Industry standard for instrument control** — Commercial medical injectors use C/C++. This project mirrors the real technology stack.
- **`std::thread` + CPU affinity** — `pthread_setaffinity_np()` on Linux, `SetThreadAffinityMask()` on Windows. Direct OS-level thread control.
- **gRPC has first-class C++ support** — `grpc++` is the reference implementation. Protobuf is native C++. No FFI boundary.
- **Cross-platform** — Builds on Windows (user's dev machine) and Linux.

**Alternatives considered:**

| Alternative | Why not |
|-------------|---------|
| Rust | Excellent safety guarantees, but gRPC ecosystem (tonic) is less mature than C++ grpc. Would work but adds learning curve with no Qt dependency to justify it. |
| Go | GC pauses (1–5 ms) directly conflict with 2 ms tick / <1 ms jitter requirement. Cannot pin goroutines to OS threads reliably. |
| Python | Too slow for 2 ms tick. GIL prevents true parallelism. |
| C# / .NET | GC pauses. Less natural fit for low-level thread control. |

**Known risks:**
- No compile-time thread safety (unlike Rust). Mitigated by: disciplined use of `std::mutex`, `std::atomic`, code review, thread sanitizer (`-fsanitize=thread`).
- Manual memory management. Mitigated by: `std::unique_ptr` / `std::shared_ptr`, RAII throughout.
- Undefined behavior potential. Mitigated by: AddressSanitizer and UBSanitizer in debug builds, `clang-tidy` static analysis.

## Tech Decision: Communication — gRPC

**Chosen:** gRPC for all frontend ↔ backend communication. Protocol Buffers (proto3) for serialization.

**Why gRPC:**
- **Process isolation** — Frontend and backend run as separate processes. A UI crash cannot affect the control loop or safety monitor. Critical for a system simulating medical device behavior.
- **Bidirectional streaming** — `StreamTelemetry` sends a continuous stream of frames at 20 Hz. `StreamEvents` sends state transitions and faults immediately. Both are natural fits for gRPC server-streaming RPCs.
- **Strong contract** — The `.proto` file is the single source of truth for the API. Both sides generate code from it. No drift between frontend and backend message formats.
- **Deployment flexibility** — Start with both processes on localhost. Later, the frontend could run on a different machine (e.g., remote monitoring) with zero code changes — just change the channel address.
- **Performance** — HTTP/2 transport with protobuf serialization. At 20 Hz telemetry (~200 bytes per frame), overhead is negligible. Far more efficient than REST+JSON polling.
- **Language independence** — The frontend could be rewritten in any language with gRPC support (Python, Go, web via grpc-web) without touching the backend.

**Alternatives considered:**

| Alternative | Why not |
|-------------|---------|
| In-process Qt signals/slots | No process isolation. UI crash can take down the control loop. Tight coupling between UI framework and backend. |
| WebSocket + JSON | No schema enforcement. Manual serialization. No code generation. WebSocket framing is less efficient than HTTP/2 multiplexing. |
| REST + polling | Polling at 50 ms intervals wastes resources and adds latency. No server-push for events. |
| Shared memory / IPC | Platform-specific. No standard schema. Complex synchronization. gRPC gives the same localhost performance with a cleaner API. |
| MQTT | Designed for IoT pub/sub. Overkill message broker dependency. No request/response pattern for commands. |

**Wire format:**

Server listens on `localhost:50051` (configurable port). All messages are Protocol Buffers (proto3).

## gRPC Service Definition

The full proto definition — service RPCs, message schemas, field semantics, validation rules, and event detail JSON formats — is in **04-schema-and-contracts.md, Section 2**.

The service exposes unary RPCs for commands and server-streaming RPCs for telemetry and events. See **04-schema-and-contracts.md, Section 2** for the complete RPC list and message schemas.

## Tech Decision: Internal Communication — Thread-Safe Queues + Atomics

**Chosen:** `std::mutex`-protected queues and `std::atomic` for all inter-thread communication within the backend process. No framework dependency for internal threading.

| Channel | Mechanism | From | To | Purpose |
|---------|-----------|------|----|---------|
| Commands | `std::mutex`-protected `std::queue<Command>` | gRPC handlers | State Machine | Frontend commands |
| Telemetry broadcast | Lock-free ring buffer | Control Loop | gRPC StreamTelemetry handler | Periodic sensor/state snapshots |
| Event broadcast | `std::mutex`-protected `std::vector` + condition variable | State Machine, Safety Monitor | gRPC StreamEvents handler | State transitions, faults |
| Faults | `std::mutex`-protected queue | Safety Monitor | State Machine | Safety fault notifications |
| Control targets | `std::atomic<double>` + `std::mutex` | State Machine | Control Loop | Current phase targets (flow rate, pressure limit) |
| Tick data | Lock-free ring buffer | Control Loop | Data Logger | Per-tick telemetry for export |

**Why not gRPC internally:**
- gRPC is for the process boundary. Within the backend, threads share memory. A mutex-protected queue has <1 µs overhead vs. gRPC's serialization + HTTP/2 framing overhead. The control loop cannot afford that per-tick.

## Tech Decision: HAL Shared State — `std::atomic` + `std::mutex`

**Chosen:** The HAL is a shared struct accessed by both the control loop and safety monitor threads.

- **`std::atomic<bool>`** for air detector (lock-free reads by safety monitor)
- **`std::atomic<double>`** (or `std::atomic<uint64_t>` with bit-cast) for pressure — allows safety monitor to read without locking
- **`std::mutex`-protected struct** for compound state (motor commanded vs. actual RPM, valve states, syringe levels)
- The safety monitor's emergency halt uses the same `std::mutex` — this is the one place where the safety monitor *writes* to shared state

**Why not lock-free everywhere:**
- The `std::mutex` is held for <1 µs per access. At 2 ms tick intervals, contention is near zero.
- Lock-free structures add complexity with no measurable benefit at this contention level.
- If profiling shows contention, individual fields can be upgraded to atomics without changing the interface.

## Component Detail: HAL (Simulated Hardware)

The HAL simulates physical hardware with a simple linear physics model. All "hardware" is computed math — no real I/O. Motor RPM drives flow rate, flow rate drives pressure, flow rate drains syringes. Five device models: motor, pressure, valves, syringes, air detector.

**Physics model details:** See **03c-architecture-simulated-devices.md** for device model structs, physics equations, parameters, edge cases, and tick sequencing.

**HAL interface contract:** See **04-schema-and-contracts.md, Section 4** for the full `IHalInterface` abstract class with thread safety guarantees per method.

The abstract interface (`IHalInterface`) allows swapping in different physics models or (future) real hardware adapters. The control loop and safety monitor depend on this interface, not on `SimulatedHal` directly.

## Component Detail: PID Controller

Standard discrete PID with anti-windup and output clamping.

```
error = target_flow_rate - actual_flow_rate
P_term = Kp * error
I_term = I_term_prev + Ki * error * dt   (clamped to prevent windup)
D_term = Kd * (error - prev_error) / dt
output = clamp(P_term + I_term + D_term, 0, MAX_RPM)
```

The output is motor RPM, not flow rate — the HAL converts RPM to flow. The PID operates in flow-rate space (error is mL/s) and outputs in RPM space.

**Gains** are configurable and tuned for the linear physics model. See **04-schema-and-contracts.md, Section 5** (`pid` config section) for defaults, ranges, and field descriptions.

**Acceleration ramp:** Before PID, the target flow rate is rate-limited:
```
ramped_target += clamp(target - ramped_target, -max_accel * dt, max_accel * dt)
```
The PID sees `ramped_target`, not the raw step change. Default `max_accel`: 10 mL/s².

## Component Detail: State Machine

Implemented as an enum + switch statement. Not a framework — just a switch/case. Six states, transition table, and command definitions are in **04-schema-and-contracts.md, Section 1**.

The key architectural decision is that state and context are kept together — no separate "current phase index" field that could get out of sync:

```cpp
struct StateContext {
    Protocol protocol;
    int phaseIndex = 0;
    double volumeDelivered = 0.0;
    InjectionResults results;
    std::vector<FaultEvent> faults;
    InjectorState previousState = InjectorState::Idle;
};
```

## Component Detail: Safety Monitor

The safety monitor is intentionally simple — complexity is the enemy of safety.

```cpp
std::vector<FaultType> safetyCheck(const IHalInterface* hal, const SafetyConfig& config) {
    std::vector<FaultType> faults;

    if (hal->readPressure() > config.pressureLimit) {
        faults.push_back(FaultType::Overpressure{hal->readPressure(), config.pressureLimit});
    }
    if (hal->readAirDetector()) {
        faults.push_back(FaultType::AirDetected{});
    }
    double divergence = std::abs(hal->readMotorRpm() - lastCommandedRpm);
    if (divergence > config.motorDivergenceThreshold) {
        motorDivergenceCount++;
        if (motorDivergenceCount > config.motorDivergenceTicks) {
            faults.push_back(FaultType::MotorFault{lastCommandedRpm, hal->readMotorRpm()});
        }
    } else {
        motorDivergenceCount = 0;
    }

    return faults;
}
```

On any fault: call `hal->emergencyStop()` first, then notify the state machine. The HAL stops the motor before the state machine even knows there's a problem.

## Component Detail: Data Logger

Two data stores:

1. **Tick ring buffer** — Fixed-size (configurable, see `logging.ringBufferSize` in **04-schema-and-contracts.md, Section 5**). `TickData` struct per entry (~80 bytes). Written by control loop every tick via lock-free ring buffer. Overwrite-oldest when full. Exported to CSV/JSON on demand via `ExportData` RPC.

2. **Event log** — `std::vector<SystemEvent>` (unbounded for MVP, events are infrequent). State transitions, faults, commands, phase changes. Each with monotonic timestamp (`std::chrono::steady_clock`).

Export is triggered by the `ExportData` RPC from the frontend. The data logger serializes the ring buffer contents to the requested format and returns the data in the gRPC response.

## External Dependencies

| Library | Purpose | Version Strategy |
|---------|---------|-----------------|
| `grpc++` | gRPC server + generated service stubs | Latest stable (1.60+) |
| `protobuf` | Protocol Buffer serialization (bundled with grpc) | Matching grpc version |
| `spdlog` | Structured logging | Latest stable |
| `nlohmann/json` | JSON serialization for data export and event details | Latest stable (header-only) |
| CMake | Build system | 3.22+ |

No Qt dependency in the backend. No WebSocket library, no HTTP framework. The backend is a gRPC server with a real-time control engine — nothing more.

## Configuration

Single JSON config file or command-line args. Seven sections: `server`, `control`, `pid`, `safety`, `hal`, `syringe`, `logging`. All fields have defaults — the config file is optional.

**Full config schema with field constraints, ranges, and defaults:** See **04-schema-and-contracts.md, Section 5**.

## Startup Sequence

1. Parse config (file + CLI overrides)
2. Initialize HAL with physics model parameters (`std::shared_ptr<SimulatedHal>`)
3. Initialize data logger with ring buffer
4. Create all thread-safe queues (command, fault, telemetry broadcast, event broadcast)
5. Spawn safety monitor thread (`std::thread`, high priority)
6. Spawn control loop thread (`std::thread`, highest priority, pin to core)
7. Spawn state machine thread (`std::thread`, normal priority)
8. Start gRPC server on configured port (blocks on `server->Wait()`)
9. Log "Backend ready, listening on localhost:{port}"
10. State machine enters `Idle`

Shutdown: SIGINT/SIGTERM → signal all threads via `std::atomic<bool> running` → `server->Shutdown()` → join all threads → clean exit.

## Thread Model Summary

```
┌──────────────────────────────────────────────────────────┐
│ gRPC Server Threads (managed by gRPC runtime)           │
│  - Handles RPCs: SendCommand, StreamTelemetry, etc.      │
│  - Marshals commands into thread-safe queues              │
│  - Reads from telemetry/event broadcasts for streaming   │
├──────────────────────────────────────────────────────────┤
│ std::thread — Control Loop (pinned core, highest prio)  │
│  - 2 ms tick, HAL read/write, PID                        │
│  - Pushes TelemetryFrame to broadcast ring buffer        │
├──────────────────────────────────────────────────────────┤
│ std::thread — Safety Monitor (high priority)             │
│  - 1 ms tick, independent HAL reads                      │
│  - Direct HAL emergency_stop() on fault                  │
├──────────────────────────────────────────────────────────┤
│ std::thread — State Machine + Data Logger                │
│  - Command processing, state transitions                 │
│  - Ring buffer management, CSV/JSON export               │
└──────────────────────────────────────────────────────────┘
```

## Process Isolation Benefits

Two-process architecture ensures: (1) frontend crash cannot corrupt backend state or affect safety monitoring, (2) backend crash is detected by frontend which shows disconnect status and reconnects, (3) separate address spaces eliminate shared-heap corruption, (4) either process can be debugged independently. See **05-non-functional-requirements.md, Section 3.3** for the full reliability guarantees.
