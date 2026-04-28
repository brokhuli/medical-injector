# Schema & Contracts

This document defines the core entities, their relationships, all message contracts (API and internal), validation rules, and versioning strategy for the medical injector simulator. It is the single source of truth for data shapes — implementation code should conform to these definitions.

---

## 1. Core Entities

### Entity Relationship Diagram

```
┌──────────────┐        ┌──────────────────┐
│   Protocol   │───1:N──│      Phase       │
│              │        │                  │
│  name?       │        │  fluid_type      │
│  phases[]    │        │  flow_rate       │
│              │        │  volume          │
│              │        │  pressure_limit  │
└──────┬───────┘        └────────┬─────────┘
       │                         │
       │ loaded into             │ executed by
       ▼                         ▼
┌──────────────────────────────────────────┐
│            Injection Session             │
│                                          │
│  state (Idle→Armed→Injecting→...)        │
│  current_phase_index                     │
│  volume_delivered_per_phase[]            │
│  total_volume_delivered                  │
│  elapsed_time                            │
│  active_faults[]                         │
└──────────────────┬───────────────────────┘
                   │
        produces   │
                   ▼
┌─────────────────────┐     ┌──────────────────┐
│   TelemetryFrame    │     │   SystemEvent    │
│   (every 50 ms)     │     │   (on change)    │
│                     │     │                  │
│  timestamp          │     │  timestamp       │
│  state              │     │  type            │
│  phase_index        │     │  details (JSON)  │
│  target_flow_rate   │     └──────────────────┘
│  actual_flow_rate   │
│  pressure           │     ┌──────────────────┐
│  motor_rpm          │     │   FaultInfo      │
│  volumes[]          │     │                  │
│  syringe levels     │     │  fault_type      │
└─────────────────────┘     │  value           │
                            │  threshold       │
┌─────────────────────┐     │  timestamp       │
│     TickData        │     └──────────────────┘
│  (every 2 ms, ring  │
│   buffer, internal)  │
│                     │
│  timestamp_us       │
│  target_flow_rate   │
│  actual_flow_rate   │
│  pressure           │
│  motor_rpm_cmd      │
│  motor_rpm_actual   │
│  contrast_valve     │
│  saline_valve       │
│  state              │
│  phase_index        │
└─────────────────────┘
```

### Entity Definitions

#### Protocol

A user-defined injection plan consisting of one or more sequential phases.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `name` | `string` | Optional, max 100 chars | Human-readable label |
| `phases` | `Phase[]` | 1-20 elements | Ordered list of injection phases |

#### Phase

A single segment of an injection protocol specifying fluid, flow rate, volume, and pressure limit.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `fluid_type` | `enum FluidType` | `CONTRAST` or `SALINE` | Which syringe barrel to draw from |
| `flow_rate` | `double` | 0.1 - 10.0 mL/s | Target flow rate for this phase |
| `volume` | `double` | 1.0 - 200.0 mL | Volume to deliver before advancing |
| `pressure_limit` | `double` | 50.0 - 400.0 psi | Safety threshold for this phase |

#### Injector State

The state machine has exactly 6 states.

```
enum InjectorState {
    Idle       = 0,
    Armed      = 1,
    Injecting  = 2,
    Paused     = 3,
    Completed  = 4,
    Fault      = 5
}
```

**Transition table:**

| From | To | Trigger | Preconditions |
|------|----|---------|---------------|
| Idle | Armed | `ARM` command | Protocol loaded, syringe volumes sufficient |
| Armed | Injecting | `START` command | - |
| Armed | Idle | `DISARM` command | - |
| Injecting | Paused | `PAUSE` command | - |
| Injecting | Completed | All phases delivered | Auto-transition |
| Injecting | Fault | Safety trigger | Any fault condition |
| Paused | Injecting | `RESUME` command | - |
| Paused | Fault | Safety trigger | Any fault condition |
| Fault | Idle | `RESET` command | Fault acknowledged |
| Any non-Idle | Fault | `EMERGENCY_STOP` | Always allowed |

Invalid transitions are rejected with `INVALID_STATE_TRANSITION` error.

#### Fault Types

```
enum FaultType {
    OVERPRESSURE      = 0,
    AIR_BUBBLE        = 1,
    MOTOR_STALL       = 2,
    PARTIAL_OCCLUSION = 3,
    TIMING_DELAY      = 4
}
```

#### Fluid Types

```
enum FluidType {
    CONTRAST = 0,
    SALINE   = 1
}
```

---

## 2. API Contract — gRPC Service (Frontend <-> Backend)

The `.proto` file is the authoritative contract between the frontend and backend processes. Both sides generate code from it. This is the complete definition.

### Service Definition

```protobuf
syntax = "proto3";
package injector;

service InjectorService {
  // ── Unary RPCs (frontend → backend) ───────────────────────
  rpc SendCommand(CommandRequest)          returns (CommandResponse);
  rpc LoadProtocol(LoadProtocolRequest)    returns (CommandResponse);
  rpc InjectFault(InjectFaultRequest)      returns (CommandResponse);
  rpc ExportData(ExportRequest)            returns (ExportResponse);
  rpc GetState(Empty)                      returns (StateSnapshot);

  // ── Server-streaming RPCs (backend → frontend) ────────────
  rpc StreamTelemetry(StreamConfig)        returns (stream TelemetryFrame);
  rpc StreamEvents(Empty)                  returns (stream SystemEvent);
}
```

### Message Schemas

#### Commands (Frontend -> Backend)

```protobuf
message Empty {}

message CommandRequest {
  CommandType command = 1;
}

enum CommandType {
  ARM            = 0;
  DISARM         = 1;
  START          = 2;
  PAUSE          = 3;
  RESUME         = 4;
  RESET          = 5;
  EMERGENCY_STOP = 6;
}

message CommandResponse {
  bool success   = 1;
  string error   = 2;   // empty if success == true
}
```

**Semantics:**
- Commands are fire-and-forget from the caller's perspective — the `CommandResponse` confirms receipt and validation, not completion.
- State changes are confirmed asynchronously via `StreamEvents`.
- `EMERGENCY_STOP` is always accepted regardless of current state.
- Commands in invalid states return `success = false` with a descriptive `error` string.
- Duplicate/idempotent commands (e.g., `ARM` when already `Armed`) return `success = true` as a no-op.

#### Protocol Loading

```protobuf
message LoadProtocolRequest {
  repeated Phase phases = 1;
}

message Phase {
  FluidType fluid_type  = 1;
  double flow_rate      = 2;   // mL/s
  double volume         = 3;   // mL
  double pressure_limit = 4;   // psi
}

enum FluidType {
  CONTRAST = 0;
  SALINE   = 1;
}
```

**Semantics:**
- Only accepted in `Idle` state. Rejected in all other states.
- Backend validates all fields against constraints (see Validation Rules below).
- Backend validates total contrast/saline volume against syringe levels.
- On success, protocol is stored and ready for `ARM`.
- Loading a new protocol replaces any previously loaded protocol.

#### Fault Injection (Testing)

```protobuf
message InjectFaultRequest {
  FaultType fault_type           = 1;
  double target_psi              = 2;   // OVERPRESSURE: pressure jumps to this value
  double max_rpm                 = 3;   // MOTOR_STALL: actual RPM capped here
  double resistance_multiplier   = 4;   // PARTIAL_OCCLUSION: tubing_resistance *= this
  double delay_ms                = 5;   // TIMING_DELAY: sleep injected into control loop
}

enum FaultType {
  OVERPRESSURE      = 0;
  AIR_BUBBLE        = 1;
  MOTOR_STALL       = 2;
  PARTIAL_OCCLUSION = 3;
  TIMING_DELAY      = 4;
}
```

**Semantics:**
- Accepted in any state (faults can be pre-staged or triggered live).
- `AIR_BUBBLE` ignores all numeric parameters — it's a boolean trigger.
- `OVERPRESSURE` overrides computed pressure. `target_psi` must be > 0.
- `MOTOR_STALL` clamps motor RPM. `max_rpm` must be >= 0.
- `PARTIAL_OCCLUSION` multiplies resistance. `resistance_multiplier` must be > 0.
- `TIMING_DELAY` injects a sleep. `delay_ms` must be > 0.
- Faults persist until cleared by state machine `RESET` command.

#### Telemetry Streaming (Backend -> Frontend)

```protobuf
message StreamConfig {
  int32 rate_ms = 1;   // desired interval in ms (default: 50, min: 20, max: 1000)
}

message TelemetryFrame {
  double timestamp                            = 1;   // seconds since injection start (0.0 when idle)
  InjectorState state                         = 2;
  int32 phase_index                           = 3;   // -1 when no active phase
  double target_flow_rate                     = 4;   // mL/s
  double actual_flow_rate                     = 5;   // mL/s
  double pressure                             = 6;   // psi
  double motor_rpm                            = 7;   // actual RPM
  repeated double volume_delivered_per_phase  = 8;   // mL per phase
  double total_volume_delivered               = 9;   // mL
  double total_programmed_volume              = 10;  // mL (sum of all phase volumes)
  double elapsed_time                         = 11;  // seconds since ARM
  double contrast_remaining                   = 12;  // mL
  double saline_remaining                     = 13;  // mL
}

enum InjectorState {
  IDLE       = 0;
  ARMED      = 1;
  INJECTING  = 2;
  PAUSED     = 3;
  COMPLETED  = 4;
  FAULT      = 5;
}
```

**Semantics:**
- The backend coalesces control loop ticks (2 ms) down to the configured telemetry rate (default 50 ms). Each frame is the latest snapshot, not an average.
- `phase_index` is 0-based. Set to `-1` when not injecting.
- `volume_delivered_per_phase` has one entry per loaded phase. Empty if no protocol loaded.
- `timestamp` resets to 0.0 on each new injection start. Reports 0.0 in Idle/Armed.
- Stream continues even when idle (state and syringe levels are still useful).
- If the frontend is slow to consume, the backend drops the oldest undelivered frames (never blocks).

#### Event Streaming (Backend -> Frontend)

```protobuf
message SystemEvent {
  double timestamp  = 1;   // seconds since process start (monotonic)
  EventType type    = 2;
  string details    = 3;   // JSON-encoded, schema varies by EventType
}

enum EventType {
  STATE_TRANSITION  = 0;
  FAULT_DETECTED    = 1;
  PHASE_TRANSITION  = 2;
  PROTOCOL_LOADED   = 3;
  COMMAND_REJECTED  = 4;
}
```

**Event `details` JSON schemas:**

**STATE_TRANSITION:**
```json
{
  "from": "Idle",
  "to": "Armed",
  "trigger": "ARM"
}
```

**FAULT_DETECTED:**
```json
{
  "fault_type": "OVERPRESSURE",
  "value": 331.2,
  "threshold": 325.0,
  "halt_latency_ms": 4.2
}
```

**PHASE_TRANSITION:**
```json
{
  "from_phase": 0,
  "to_phase": 1,
  "volume_delivered": 80.1,
  "volume_programmed": 80.0
}
```

**PROTOCOL_LOADED:**
```json
{
  "phase_count": 2,
  "total_volume": 110.0,
  "total_contrast": 80.0,
  "total_saline": 30.0
}
```

**COMMAND_REJECTED:**
```json
{
  "command": "START",
  "current_state": "Idle",
  "reason": "INVALID_STATE_TRANSITION"
}
```

#### State Snapshot (Reconnection)

```protobuf
message StateSnapshot {
  InjectorState state                  = 1;
  repeated Phase loaded_protocol       = 2;
  TelemetryFrame latest_telemetry      = 3;
  repeated SystemEvent recent_events   = 4;   // last 100 events
  repeated FaultInfo active_faults     = 5;
  double contrast_remaining            = 6;   // mL
  double saline_remaining              = 7;   // mL
}

message FaultInfo {
  FaultType type   = 1;
  double value     = 2;      // measured value that triggered fault
  double threshold = 3;      // configured threshold that was exceeded
  double timestamp = 4;      // seconds since process start
}
```

**Semantics:**
- Used by the frontend on initial connect and reconnect to sync UI with backend state.
- `recent_events` contains the last 100 events (or fewer if less have occurred).
- `active_faults` is empty unless in `Fault` state.
- `loaded_protocol` is empty if no protocol is loaded.

#### Data Export

```protobuf
message ExportRequest {
  string format = 1;   // "csv" or "json"
}

message ExportResponse {
  string data = 1;     // serialized export content
}
```

**Semantics:**
- Exports the tick-level ring buffer data from the most recent injection.
- Returns empty string if no injection data exists.
- CSV format: header row + one row per tick (see TickData below).
- JSON format: array of tick objects.
- Export reads from the ring buffer without blocking the control loop.

---

## 3. Internal Contracts (Between Backend Threads)

These are not exposed via gRPC but define the data shapes passed between threads within the backend process.

### Command Queue (gRPC -> State Machine)

```cpp
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
        InjectFault
    };

    Type type;
    Protocol protocol;           // populated for LoadProtocol
    SimulatedFault fault;        // populated for InjectFault
};
```

Transport: `std::mutex`-protected `std::queue<InternalCommand>`.

### Fault Event (Safety Monitor -> State Machine)

```cpp
struct FaultEvent {
    FaultType type;
    double measuredValue;        // the sensor reading that triggered the fault
    double threshold;            // the configured limit that was exceeded
    uint64_t timestampUs;        // microseconds since process start (steady_clock)
};
```

Transport: `std::mutex`-protected `std::queue<FaultEvent>`.

### Control Targets (State Machine -> Control Loop)

```cpp
struct ControlTargets {
    std::atomic<double> targetFlowRate{0.0};    // mL/s
    std::atomic<double> pressureLimit{325.0};   // psi (for safety monitor)
    std::atomic<int> activePhaseIndex{-1};      // -1 = not injecting
    std::atomic<bool> injecting{false};         // control loop checks this
    std::atomic<int> activeFluidChannel{0};     // 0=contrast, 1=saline

    // Protocol data (read under mutex, written only during state transitions)
    std::mutex protocolMutex;
    Protocol loadedProtocol;
};
```

Atomics for hot-path reads by the control loop (every 2 ms). Protocol details read under mutex only on phase transitions.

### Telemetry Broadcast (Control Loop -> gRPC StreamTelemetry)

```cpp
struct TelemetrySnapshot {
    double timestampS;               // seconds since injection start
    InjectorState state;
    int phaseIndex;
    double targetFlowRate;           // mL/s
    double actualFlowRate;           // mL/s
    double pressure;                 // psi
    double motorRpm;                 // actual
    std::vector<double> volumePerPhase;  // mL per phase
    double totalVolumeDelivered;     // mL
    double totalProgrammedVolume;    // mL
    double elapsedTime;              // seconds
    double contrastRemaining;        // mL
    double salineRemaining;          // mL
};
```

Transport: Lock-free single-producer single-consumer ring buffer (control loop writes, gRPC handler reads). Size: 128 entries. Overwrite-oldest on full.

### Tick Data (Control Loop -> Data Logger)

```cpp
struct TickData {
    uint64_t timestampUs;            // microseconds since process start
    double targetFlowRate;           // mL/s
    double actualFlowRate;           // mL/s
    double pressure;                 // psi
    double motorRpmCommanded;        // RPM
    double motorRpmActual;           // RPM
    uint8_t contrastValve;           // 0=closed, 1=open
    uint8_t salineValve;             // 0=closed, 1=open
    uint8_t state;                   // InjectorState enum value
    uint8_t phaseIndex;              // 0-based, 255 = none
    double contrastRemaining;        // mL
    double salineRemaining;          // mL
};
// sizeof(TickData) ≈ 80 bytes
```

Transport: Lock-free SPSC ring buffer. Size: 300,000 entries (~24 MB, 10 minutes at 2 ms tick).

**CSV export column order:**

```
timestamp_us,target_flow_rate,actual_flow_rate,pressure,motor_rpm_commanded,motor_rpm_actual,contrast_valve,saline_valve,state,phase_index,contrast_remaining,saline_remaining
```

**JSON export format:**

```json
[
  {
    "timestamp_us": 1000000,
    "target_flow_rate": 4.0,
    "actual_flow_rate": 3.97,
    "pressure": 208.5,
    "motor_rpm_commanded": 400,
    "motor_rpm_actual": 397,
    "contrast_valve": 1,
    "saline_valve": 0,
    "state": "Injecting",
    "phase_index": 0,
    "contrast_remaining": 58.2,
    "saline_remaining": 50.0
  }
]
```

### Event Broadcast (State Machine / Safety Monitor -> gRPC StreamEvents)

```cpp
struct InternalEvent {
    uint64_t timestampUs;            // microseconds since process start
    EventType type;
    std::string detailsJson;         // JSON string matching the schemas in Section 2
};
```

Transport: `std::mutex`-protected `std::vector<InternalEvent>` + `std::condition_variable`. gRPC handler waits on the condition variable and sends events immediately on arrival.

---

## 4. HAL Interface Contract

The `IHalInterface` defines the boundary between the control/safety logic and the simulated (or future real) hardware. Both the control loop and safety monitor depend on this interface.

```cpp
enum class Barrel { Contrast, Saline };
enum class FluidChannel { Contrast, Saline };
enum class ValveState { Open, Closed };

struct SimulatedFault {
    enum class Type {
        Overpressure,
        AirBubble,
        MotorStall,
        PartialOcclusion,
        TimingDelay
    };

    Type type;
    double targetPsi = 0.0;
    double maxRpm = 0.0;
    double resistanceMultiplier = 1.0;
    double delayMs = 0.0;
};

class IHalInterface {
public:
    virtual ~IHalInterface() = default;

    // ── Sensors (read) ──────────────────────────────────────
    virtual double readPressure() const = 0;                          // psi
    virtual double readMotorRpm() const = 0;                          // actual RPM
    virtual bool readAirDetector() const = 0;                         // true = air present
    virtual double readSyringeVolume(Barrel barrel) const = 0;        // mL remaining

    // ── Actuators (write) ───────────────────────────────────
    virtual void setMotorRpm(double rpm) = 0;                         // commanded RPM
    virtual void setValve(FluidChannel channel, ValveState state) = 0;
    virtual void emergencyStop() = 0;                                 // motor=0, all valves closed, instant

    // ── Simulation control ──────────────────────────────────
    virtual void tick(double dt) = 0;                                 // advance physics by dt seconds
    virtual void injectFault(const SimulatedFault& fault) = 0;
    virtual void clearFaults() = 0;
};
```

**Thread safety contract:**
- All methods must be safe to call from any thread.
- `readPressure()`, `readMotorRpm()`, `readAirDetector()` may be called concurrently by the control loop and safety monitor.
- `tick()` is called only by the control loop thread (single writer).
- `emergencyStop()` may be called by the safety monitor at any time, including during a `tick()`.
- `injectFault()` and `clearFaults()` are called from the state machine thread.

---

## 5. Configuration Schema

The backend loads a single JSON configuration file at startup. All fields have defaults — the config file is optional.

```json
{
  "server": {
    "port": 50051,
    "telemetryRateMs": 50
  },
  "control": {
    "tickRateMs": 2,
    "pinCore": true
  },
  "pid": {
    "kp": 100.0,
    "ki": 50.0,
    "kd": 5.0,
    "iTermMax": 500.0,
    "maxRpm": 1500.0,
    "maxAcceleration": 10.0
  },
  "safety": {
    "defaultPressureLimitPsi": 325.0,
    "motorDivergenceThreshold": 200.0,
    "motorDivergenceTicks": 25,
    "jitterToleranceMs": 3.0,
    "tickRateMs": 1
  },
  "hal": {
    "flowPerRpm": 0.01,
    "tubingResistance": 50.0,
    "baselinePressure": 10.0,
    "motorTimeConstantMs": 50.0,
    "motorMaxRpm": 1500.0
  },
  "syringe": {
    "contrastVolumeMl": 100.0,
    "salineVolumeMl": 50.0
  },
  "logging": {
    "ringBufferSize": 300000,
    "maxEvents": 10000
  }
}
```

**Field constraints:**

| Section | Field | Type | Range | Default |
|---------|-------|------|-------|---------|
| server | port | int | 1024-65535 | 50051 |
| server | telemetryRateMs | int | 20-1000 | 50 |
| control | tickRateMs | int | 1-10 | 2 |
| control | pinCore | bool | - | true |
| pid | kp | double | > 0 | 100.0 |
| pid | ki | double | >= 0 | 50.0 |
| pid | kd | double | >= 0 | 5.0 |
| pid | iTermMax | double | > 0 | 500.0 |
| pid | maxRpm | double | > 0 | 1500.0 |
| pid | maxAcceleration | double | > 0 | 10.0 |
| safety | defaultPressureLimitPsi | double | 50-400 | 325.0 |
| safety | motorDivergenceThreshold | double | > 0 | 200.0 |
| safety | motorDivergenceTicks | int | >= 1 | 25 |
| safety | jitterToleranceMs | double | > 0 | 3.0 |
| safety | tickRateMs | int | 1-10 | 1 |
| hal | flowPerRpm | double | > 0 | 0.01 |
| hal | tubingResistance | double | > 0 | 50.0 |
| hal | baselinePressure | double | >= 0 | 10.0 |
| hal | motorTimeConstantMs | double | > 0 | 50.0 |
| hal | motorMaxRpm | double | > 0 | 1500.0 |
| syringe | contrastVolumeMl | double | 1-500 | 100.0 |
| syringe | salineVolumeMl | double | 1-500 | 50.0 |
| logging | ringBufferSize | int | 1000-1000000 | 300000 |
| logging | maxEvents | int | 100-100000 | 10000 |

---

## 6. Validation Rules

Validation happens at two boundaries: the frontend (client-side, for UX) and the backend (authoritative, for safety). The backend always validates — frontend validation is a convenience, not a guarantee.

### Protocol Validation (on `LoadProtocol`)

| Rule | Validated By | Error Message |
|------|-------------|---------------|
| At least 1 phase | Backend + Frontend | "Protocol must have at least one phase" |
| Maximum 20 phases | Backend + Frontend | "Protocol exceeds maximum of 20 phases" |
| Flow rate 0.1 - 10.0 mL/s per phase | Backend + Frontend | "Flow rate must be between 0.1 and 10.0 mL/s" |
| Volume 1.0 - 200.0 mL per phase | Backend + Frontend | "Volume must be between 1.0 and 200.0 mL" |
| Pressure limit 50.0 - 400.0 psi per phase | Backend + Frontend | "Pressure limit must be between 50 and 400 psi" |
| Total contrast volume <= contrast syringe | Backend + Frontend | "Insufficient contrast volume: {required} mL required, {available} mL available" |
| Total saline volume <= saline syringe | Backend + Frontend | "Insufficient saline volume: {required} mL required, {available} mL available" |
| Only accepted in Idle state | Backend | "Protocol can only be loaded in Idle state" |

### Command Validation (on `SendCommand`)

| Rule | Error Message |
|------|---------------|
| `ARM` requires protocol loaded | "No protocol loaded" |
| `ARM` requires Idle state | "Cannot arm: current state is {state}" |
| `START` requires Armed state | "Cannot start: current state is {state}" |
| `PAUSE` requires Injecting state | "Cannot pause: current state is {state}" |
| `RESUME` requires Paused state | "Cannot resume: current state is {state}" |
| `DISARM` requires Armed state | "Cannot disarm: current state is {state}" |
| `RESET` requires Fault state | "Cannot reset: current state is {state}" |
| `EMERGENCY_STOP` always accepted | (never rejected) |
| Duplicate command in target state | No-op, returns success |

### Fault Injection Validation (on `InjectFault`)

| Rule | Error Message |
|------|---------------|
| `OVERPRESSURE` requires `target_psi` > 0 | "target_psi must be positive" |
| `MOTOR_STALL` requires `max_rpm` >= 0 | "max_rpm must be non-negative" |
| `PARTIAL_OCCLUSION` requires `resistance_multiplier` > 0 | "resistance_multiplier must be positive" |
| `TIMING_DELAY` requires `delay_ms` > 0 | "delay_ms must be positive" |

### Stream Configuration Validation (on `StreamTelemetry`)

| Rule | Behavior |
|------|----------|
| `rate_ms` < 20 | Clamped to 20 |
| `rate_ms` > 1000 | Clamped to 1000 |
| `rate_ms` = 0 or missing | Uses default (50) |

### Export Validation (on `ExportData`)

| Rule | Error Message |
|------|---------------|
| `format` must be "csv" or "json" | "Unsupported format: {format}. Use 'csv' or 'json'" |
| No injection data available | Returns empty `data` field (not an error) |

---

## 7. Versioning Strategy

### gRPC / Protobuf Versioning

**MVP:** No versioning. The `.proto` file is the contract. Frontend and backend are built from the same proto file and deployed together.

**Future (if needed):**
- Proto3 is forward/backward compatible by default (unknown fields are preserved, missing fields get defaults).
- New fields can be added to existing messages without breaking existing clients.
- Enums can be extended — unknown enum values are preserved as integers.
- RPCs can be added to the service without breaking existing clients.
- **Never reuse field numbers.** Deprecated fields are reserved: `reserved 7;`
- **Never change field types.** Add a new field instead.

### Configuration Versioning

**MVP:** No version field. All fields have defaults, so missing fields are safe.

**Future (if needed):** Add a top-level `"version": 1` field. Migration logic maps old configs to new shapes.

### Export Format Versioning

**MVP:** No version field. CSV column order and JSON field names are fixed as defined in Section 3.

**Future (if needed):** Add a `"version"` field to JSON exports. CSV could include a comment header with version info.

---

## 8. Derived Constants & Relationships

These are values computed from the configuration, useful for understanding system behavior.

| Derived Value | Formula | Default Result |
|---------------|---------|----------------|
| Max flow rate | `hal.motorMaxRpm * hal.flowPerRpm` | 1500 * 0.01 = 15.0 mL/s |
| Pressure at max flow | `maxFlowRate * hal.tubingResistance + hal.baselinePressure` | 15.0 * 50 + 10 = 760 psi |
| Pressure at 4 mL/s | `4.0 * 50 + 10` | 210 psi |
| Headroom at 4 mL/s | `325 - 210` | 115 psi |
| Motor RPM for 4 mL/s | `4.0 / hal.flowPerRpm` | 400 RPM |
| Motor ramp to 63% | `hal.motorTimeConstantMs` | 50 ms |
| Motor ramp to ~95% | `3 * hal.motorTimeConstantMs` | 150 ms |
| Ticks per second | `1000 / control.tickRateMs` | 500 |
| Telemetry frames per second | `1000 / server.telemetryRateMs` | 20 |
| Ring buffer duration | `logging.ringBufferSize * control.tickRateMs / 1000` | 600 s (10 min) |
| Ring buffer memory | `logging.ringBufferSize * sizeof(TickData)` | ~24 MB |
| Safety ticks per control tick | `control.tickRateMs / safety.tickRateMs` | 2 |
| Motor fault detection time | `safety.motorDivergenceTicks * safety.tickRateMs` | 25 ms |
| Occlusion fault threshold | `resistance_multiplier` where pressure exceeds limit | At 4 mL/s: 325 = 4.0 * R + 10 → R = 78.75 → multiplier = 1.575x |
