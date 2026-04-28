# Medical Contrast Power Injector — Architecture Summary

Educational/research simulator for a medical contrast power injector. **Not a medical device.**

---

## High-Level Architecture

Two-process system communicating over **gRPC** (localhost:50051):

```
┌─────────────────────┐        gRPC         ┌─────────────────────┐
│   Qt6/QML Frontend  │ ◄──────────────────► │   C++17 Backend     │
│   (Clinical UI)     │   localhost:50051    │   (Control Engine)  │
└─────────────────────┘                      └─────────────────────┘
```

---

## Backend — 7-Layer Architecture

The backend is a real-time control engine organized into seven layers:

```
 ┌──────────────────────────────────────────────────────┐
 │  Layer 7: gRPC Server (comms/)                       │
 │  7 RPCs: command, protocol, fault, state, telemetry  │
 ├──────────────────────────────────────────────────────┤
 │  Layer 6: Safety Monitor (safety/)                   │
 │  Independent 1ms thread, reads HAL directly          │
 │  Checks: overpressure, air, motor divergence, timing │
 ├──────────────────────────────────────────────────────┤
 │  Layer 5: State Machine (state/)                     │
 │  6 states: Idle→Armed→Injecting→Paused→Completed    │
 │            └──────────► Fault ◄──────────┘           │
 │  Multi-phase protocol progression                    │
 ├──────────────────────────────────────────────────────┤
 │  Layer 4: Data Logging (logging/)                    │
 │  Lock-free SPSC ring buffer, CSV/JSON export         │
 ├──────────────────────────────────────────────────────┤
 │  Layer 3: Control Loop (control/)                    │
 │  2ms tick, PID controller with anti-windup           │
 │  Acceleration ramp, filtered derivative term         │
 │  Pressure-aware end-of-phase deceleration            │
 ├──────────────────────────────────────────────────────┤
 │  Layer 2: Configuration (config/)                    │
 │  7-section JSON config with defaults + validation    │
 ├──────────────────────────────────────────────────────┤
 │  Layer 1: HAL — Hardware Abstraction (hal/)          │
 │  IHalInterface (pure virtual) + SimulatedHal         │
 │  Physics models: motor (1st-order lag),              │
 │    pressure (linear + compliance lag, per-fluid      │
 │    resistance), valve, syringe, air detector         │
 │  Fault injection for testing                         │
 └──────────────────────────────────────────────────────┘
```

### Key Design Decisions

- **SimulatedHal** provides physics-based models (first-order motor lag, linear steady-state pressure with a first-order compliance lag, per-fluid tubing resistance, binary valves, volume tracking) behind a pure virtual interface, making it swappable for real hardware.
- **End-of-phase deceleration** is driven by the motor model's predicted stopping volume and only engages on the final phase, so intermediate phase transitions hand flow off continuously. The decel rate scales with pressure headroom to stay under the pressure limit.
- **Safety Monitor** runs on its own 1ms thread, independent of the 2ms control loop, and reads HAL directly — it can halt injection even if the control loop hangs.
- **Lock-free SPSC ring buffer** passes telemetry from the control thread to logging/gRPC without blocking the real-time path.
- **Inter-thread communication** uses dedicated queues: `CommandQueue`, `FaultQueue`, `EventBroadcast`, `TelemetryBroadcast`.

---

## Frontend — Qt6/QML

The frontend is a Qt6/QML clinical UI that connects as a gRPC client.

### Core C++ Bridge

| Component             | Role                                                             |
| --------------------- | ---------------------------------------------------------------- |
| **GrpcClientService** | gRPC channel, stub, streaming threads, reconnection with backoff |
| **InjectorBridge**    | QObject bridge exposing Q_PROPERTY / Q_INVOKABLE to QML          |

### QML UI Components

| Component               | Purpose                                                          |
| ----------------------- | ---------------------------------------------------------------- |
| **main.qml**            | Application window and full layout                               |
| **Theme.qml**           | Singleton: dark theme colors, fonts, spacing                     |
| **ConnectionIndicator** | Green/red/yellow connection status dot                           |
| **StateIndicator**      | Color-coded injector state display                               |
| **ProtocolPanel**       | Phase list editor, syringe indicators, load button               |
| **PhaseRow**            | Single phase: fluid type, flow rate, volume, pressure limit      |
| **SyringeIndicator**    | Remaining volume bar                                             |
| **ValveIndicator**      | Contrast / saline valve open/closed state                        |
| **ControlPanel**        | Arm / Start / Pause / Resume / Reset / E-Stop buttons            |
| **FaultDetail**         | Active fault list with acknowledge → reset workflow and "Show Fault Report" button |
| **FaultReportDialog**   | Post-fault diagnostic dialog: snapshot at fault time, control-loop health, pre-fault telemetry trace, recent events |
| **Dashboard**           | Container for gauges, volume progress, timeline chart            |
| **FlowRateGauge**       | Target vs actual flow with deviation coloring                    |
| **PressureGauge**       | Pressure bar with green/yellow/red zones                         |
| **VolumeProgress**      | Per-phase and total volume progress bars                         |
| **TimelineChart**       | Canvas: dual Y-axis (flow/pressure), phase markers, 10Hz repaint |
| **EventLog**            | Scrollable event list with timestamps                            |
| **ElapsedTimer**        | MM:SS.s elapsed time display                                     |
| **LoopHealthBar**       | Live control-loop timing: mean/max tick duration and overrun count |
| **AboutDialog**         | Modal dialog for rendering markdown resources (about, architecture, ADR) |

---

## gRPC Interface

Defined in `proto/injector.proto`:

- **Unary RPCs**: `SendCommand`, `LoadProtocol`, `InjectFault`, `GetState`, `ExportData`
- **Server-streaming RPCs**: `StreamTelemetry` (coalesced to requested rate), `StreamEvents` (condition variable driven)

---

## Injection Protocol Model

A protocol consists of one or more **phases**, each specifying:

- Fluid type (Contrast or Saline)
- Flow rate (mL/s)
- Volume (mL)
- Pressure limit (psi)

The state machine progresses through phases automatically, switching valves between contrast/saline as needed.

---

## Thread Model

| Thread           | Rate         | Purpose                                            |
| ---------------- | ------------ | -------------------------------------------------- |
| Control loop     | 2ms          | PID computation, HAL read/write, phase tracking    |
| Safety monitor   | 1ms          | Independent overpressure / air / divergence checks |
| gRPC server      | on-demand    | RPC handling, streaming telemetry/events           |
| Telemetry stream | configurable | Coalesces tick data to client-requested rate       |
| Event stream     | event-driven | Pushes state/fault events via condition variable   |

---

## Build & Test

- **Build system**: CMake with vcpkg (dependencies) + Qt6
- **Test suites**: Unit tests (fast, no threading), Integration tests (real threads + timing), gRPC tests (full backend + client)
- **CI**: GitHub Actions — Ubuntu (Release + Debug/ASan/UBSan), Windows Release
