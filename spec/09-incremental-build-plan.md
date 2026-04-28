# Incremental Build Plan

Vertical slices, not horizontal layers. Each milestone produces a demoable, testable result that exercises the architecture end-to-end. Earlier milestones constrain later ones — if something doesn't work in Milestone 1, we find out before investing in Milestones 2-7.

**Folder structure:** See **06-repo-structure.md, Section 1** for the complete directory layout referenced by task file paths below.

---

## Milestone Overview

```
M1  Scaffold + HAL Physics       ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
M2  Control Loop + PID           ████░░░░░░░░░░░░░░░░░░░░░░░░░░
M3  State Machine + Safety       ████████░░░░░░░░░░░░░░░░░░░░░░
M4  gRPC Server + Telemetry      ████████████░░░░░░░░░░░░░░░░░░
M5  Frontend Shell + Connection  ████████████████░░░░░░░░░░░░░░
M6  Clinical UI Panels           ████████████████████░░░░░░░░░░
M7  Integration + Polish         ████████████████████████░░░░░░
```

**Dependency chain:** M1 → M2 → M3 → M4 → M5 → M6 → M7. Each milestone builds on the previous. No parallel tracks.

---

## Milestone 1: Scaffold + HAL Physics

**Goal:** Repository compiles, tests run, and the simulated hardware produces physically correct values.

**Why first:** Everything depends on the HAL. If the physics model is wrong, the PID can't tune, the safety monitor can't detect faults, and the dashboard shows garbage. Prove the foundation works before building on it.

### Tasks

| # | Task | Files Created/Modified | Depends On |
|---|------|----------------------|------------|
| 1.1 | Root CMake scaffold | `CMakeLists.txt`, `backend/CMakeLists.txt`, `backend/tests/CMakeLists.txt` | — |
| 1.2 | vcpkg integration + dependency manifest | `vcpkg.json`, CMake toolchain config | 1.1 |
| 1.3 | clang-format + clang-tidy configs | `.clang-format`, `.clang-tidy` | — |
| 1.4 | .gitignore update | `.gitignore` | — |
| 1.5 | HAL interface | `backend/src/hal/IHalInterface.h` | 1.1 |
| 1.6 | Motor model + tests | `backend/src/hal/MotorModel.{h,cpp}`, `backend/tests/test_motor_model.cpp` | 1.5 |
| 1.7 | Pressure model + tests | `backend/src/hal/PressureModel.{h,cpp}`, `backend/tests/test_pressure_model.cpp` | 1.5 |
| 1.8 | Valve model + tests | `backend/src/hal/ValveModel.{h,cpp}`, `backend/tests/test_valve_model.cpp` | 1.5 |
| 1.9 | Syringe model + tests | `backend/src/hal/SyringeModel.{h,cpp}`, `backend/tests/test_syringe_model.cpp` | 1.5 |
| 1.10 | Air detector model | `backend/src/hal/AirDetectorModel.h`, `backend/tests/test_air_detector.cpp` | 1.5 |
| 1.11 | SimulatedHal (assembles models) + integration test | `backend/src/hal/SimulatedHal.{h,cpp}`, `backend/tests/test_simulated_hal_integration.cpp` | 1.6-1.10 |
| 1.12 | MockHal for future tests | `backend/tests/mocks/MockHal.h` | 1.5 |
| 1.13 | Config struct + JSON parsing + tests | `backend/src/config/Config.{h,cpp}`, `backend/tests/test_config.cpp` | 1.1 |
| 1.14 | Backend main.cpp (minimal — parse config, create HAL, exit) | `backend/src/main.cpp` | 1.11, 1.13 |
| 1.15 | Update CLAUDE.md with build commands | `CLAUDE.md` | 1.14 |

### Definition of Done

- [ ] `cmake --build build` succeeds on Windows (MSVC) with zero warnings
- [ ] `ctest --test-dir build --output-on-failure` — all tests pass
- [ ] Motor model: step response follows first-order lag (AC-1.2)
- [ ] Pressure model: `compute(4.0)` returns 210.0 psi (AC-1.1)
- [ ] Valve model: open/close/read works (AC-1.3)
- [ ] Syringe model: drain tracking accurate to ± 0.1 mL (AC-1.5)
- [ ] Air detector: default false (AC-1.4)
- [ ] SimulatedHal: tick sequencing produces consistent physics (motor → flow → pressure → syringe)
- [ ] Config: loads JSON, applies defaults for missing fields, rejects invalid values
- [ ] `./injector-backend` starts, logs "Backend ready", and exits cleanly

### Demo

Run `injector-backend` — it loads config, initializes the HAL, logs the initial state (pressure: 10 psi, motor: 0 RPM, syringes: 100/50 mL), and exits. Run the tests — all green. Not exciting to watch, but the foundation is proven.

---

## Milestone 2: Control Loop + PID

**Goal:** A dedicated thread runs the control loop at 2 ms ticks, driving the HAL to a target flow rate via PID. No state machine yet — the target is hardcoded for this milestone.

**Why second:** The control loop is the heartbeat of the system. If it can't maintain timing or the PID can't converge, nothing else matters. Prove real-time control works before adding state management.

### Tasks

| # | Task | Files Created/Modified | Depends On |
|---|------|----------------------|------------|
| 2.1 | RingBuffer (lock-free SPSC) + tests | `backend/src/logging/RingBuffer.h`, `backend/tests/test_ring_buffer.cpp` | M1 |
| 2.2 | PID controller + tests | `backend/src/control/PidController.{h,cpp}`, `backend/tests/test_pid_controller.cpp` | M1 |
| 2.3 | Control loop thread (tick driver + PID + HAL) | `backend/src/control/ControlLoop.{h,cpp}` | 2.1, 2.2 |
| 2.4 | Control loop integration test (timing + flow accuracy) | `backend/tests/test_control_loop.cpp` | 2.3 |
| 2.5 | Data logger (ring buffer for tick data) + tests | `backend/src/logging/DataLogger.{h,cpp}`, `backend/tests/test_data_logger.cpp` | 2.1 |
| 2.6 | Backend main.cpp update (spawn control loop, run for N seconds, log results) | `backend/src/main.cpp` | 2.3, 2.5 |

### Definition of Done

- [ ] Control loop runs at 2 ms ± 0.1 ms mean, no tick > 5 ms (AC-2.1)
- [ ] Jitter stats available after 100+ ticks (AC-2.2)
- [ ] PID drives flow rate to 4.0 mL/s ± 2% at steady state (AC-3.1)
- [ ] Acceleration ramp respects 10 mL/s² limit (AC-3.3)
- [ ] PID settles within 1 second with ≤ 2 overshoot cycles (AC-3.4)
- [ ] Volume delivered over 10 seconds matches expected ± 2% (AC-3.2)
- [ ] Ring buffer handles push/pop, overwrite-oldest, concurrent access
- [ ] Data logger captures tick data to ring buffer without blocking control loop (AC-7.2)
- [ ] `./injector-backend` runs for 10 seconds, injects at 4 mL/s, logs timing stats and final volume, exits

### Demo

Run `injector-backend` — it starts the control loop targeting 4.0 mL/s, runs for 10 seconds, and prints:
```
Control loop: 5000 ticks, mean 2.001 ms, max 2.8 ms, jitter stddev 0.12 ms
PID: target 4.0 mL/s, actual mean 3.98 mL/s, volume delivered 39.8 mL
```
The numbers prove the real-time architecture works.

---

## Milestone 3: State Machine + Safety Monitor

**Goal:** The state machine manages the full injection lifecycle (Idle → Armed → Injecting → Completed/Fault). The safety monitor runs independently and can halt injection on fault conditions. Multi-phase protocol execution works.

**Why third:** With the control loop proven, add orchestration (state machine) and the safety net (safety monitor). These are the two components that make it an injector, not just a PID loop.

### Tasks

| # | Task | Files Created/Modified | Depends On |
|---|------|----------------------|------------|
| 3.1 | State enum + protocol structs | `backend/src/state/InjectorState.h`, `backend/src/state/Protocol.h` | M2 |
| 3.2 | Command queue (thread-safe) + tests | `backend/src/comms/CommandQueue.h`, `backend/tests/test_command_queue.cpp` | M2 |
| 3.3 | Event broadcast (mutex + condvar) | `backend/src/comms/EventBroadcast.h` | M2 |
| 3.4 | Telemetry broadcast (SPSC ring buffer) | `backend/src/comms/TelemetryBroadcast.h` | 2.1 |
| 3.5 | State machine (transitions, command processing, protocol phases) + tests | `backend/src/state/StateMachine.{h,cpp}`, `backend/tests/test_state_machine.cpp` | 3.1, 3.2, 3.3 |
| 3.6 | Safety monitor thread + tests | `backend/src/safety/SafetyMonitor.{h,cpp}`, `backend/tests/test_safety_monitor.cpp` | M2, 3.1 |
| 3.7 | Wire control loop to state machine (control targets, phase progression) | Modify `ControlLoop.{h,cpp}`, `StateMachine.{h,cpp}` | 3.5, 2.3 |
| 3.8 | Wire safety monitor to state machine (fault events → state transition) | Modify `SafetyMonitor.{h,cpp}`, `StateMachine.{h,cpp}` | 3.5, 3.6 |
| 3.9 | Fault injection in SimulatedHal + tests | Modify `SimulatedHal.{h,cpp}` | M1 |
| 3.10 | Injection lifecycle integration test | `backend/tests/test_injection_lifecycle.cpp` | 3.7, 3.8 |
| 3.11 | Fault end-to-end integration test | `backend/tests/test_fault_end_to_end.cpp` | 3.8, 3.9 |
| 3.12 | Safety monitor integration test (independence from state machine) | `backend/tests/test_safety_integration.cpp` | 3.8 |
| 3.13 | Backend main.cpp update (spawn all threads, accept hardcoded protocol, run lifecycle) | `backend/src/main.cpp` | 3.7, 3.8 |

### Definition of Done

- [ ] All valid state transitions work (AC-4.1)
- [ ] All invalid transitions rejected with error (AC-4.2)
- [ ] Multi-phase protocol executes: 3 phases, each ± 2% volume (AC-4.3)
- [ ] Phase boundary: flow rate ramps between phases (AC-4.4)
- [ ] Overpressure: fault within 10 ms, motor stops (AC-5.1)
- [ ] Air detection: fault triggers (AC-5.2)
- [ ] Motor fault: triggers after 25 ticks of sustained divergence (AC-5.3)
- [ ] Timing violation: triggers on > 5 ms tick (AC-5.4)
- [ ] Safety monitor works when state machine is hung (AC-5.5)
- [ ] Fault during pause transitions Paused → Fault (AC-5.6)
- [ ] Fault injection (overpressure, air, motor stall, occlusion, timing delay) all work
- [ ] Event log captures all transitions with monotonic timestamps (AC-7.3)
- [ ] `./injector-backend` runs a complete 2-phase injection, logs state transitions, exits

### Demo

Run `injector-backend` — it loads a 2-phase protocol, arms, injects (Phase 1: contrast 4 mL/s for 80 mL, Phase 2: saline 2 mL/s for 30 mL), and completes:
```
State: Idle → Armed → Injecting
Phase 1: Contrast 80.1 mL delivered in 20.03s
Phase 2: Saline 30.0 mL delivered in 15.01s
State: Injecting → Completed
Total: 110.1 mL in 35.04s
```
Then run with a fault injection — overpressure mid-injection → Fault state, motor stopped.

---

## Milestone 4: gRPC Server + Telemetry

**Goal:** The backend exposes its full API via gRPC. A test client can send commands, receive telemetry streams, and export data. The backend is now controllable externally — no more hardcoded protocols.

**Why fourth:** The backend is feature-complete internally (M1-M3). Now expose it to the outside world. The gRPC layer is the last backend component — after this, all backend work is done.

### Tasks

| # | Task | Files Created/Modified | Depends On |
|---|------|----------------------|------------|
| 4.1 | Proto file | `proto/injector.proto` | M3 |
| 4.2 | Proto CMake generation (backend) | Modify `backend/CMakeLists.txt` | 4.1 |
| 4.3 | gRPC server: SendCommand + LoadProtocol + InjectFault + GetState | `backend/src/comms/GrpcServer.{h,cpp}` | 4.1, 3.5 |
| 4.4 | gRPC server: StreamTelemetry (coalesced from telemetry broadcast) | Modify `GrpcServer.cpp` | 4.3, 3.4 |
| 4.5 | gRPC server: StreamEvents (from event broadcast) | Modify `GrpcServer.cpp` | 4.3, 3.3 |
| 4.6 | gRPC server: ExportData (CSV + JSON from data logger) | Modify `GrpcServer.cpp` | 4.3, 2.5 |
| 4.7 | CSV export format + tests | Modify `DataLogger.{h,cpp}`, `test_data_logger.cpp` | 2.5 |
| 4.8 | JSON export format + tests | Modify `DataLogger.{h,cpp}`, `test_data_logger.cpp` | 2.5 |
| 4.9 | Backend main.cpp update (start gRPC server, block on server->Wait()) | `backend/src/main.cpp` | 4.3 |
| 4.10 | gRPC integration tests (command round-trip, telemetry streaming, export) | `backend/tests/test_grpc_integration.cpp` | 4.3-4.8 |
| 4.11 | Config integration test (config propagates to all components) | `backend/tests/test_config_integration.cpp` | 4.9 |

### Definition of Done

- [ ] Proto file matches spec 04 exactly
- [ ] `SendCommand` round-trip: Arm → Armed event within 100 ms (AC-6.1)
- [ ] `StreamTelemetry`: ~100 frames over 5 seconds at 50 ms rate (AC-6.2)
- [ ] Invalid command rejection via gRPC (AC-6.3)
- [ ] `InjectFault` triggers safety response (AC-6.4)
- [ ] `ExportData("csv")`: correct header, ≥ 4900 rows for 10s injection (AC-7.1, AC-7.4)
- [ ] `ExportData("json")`: valid JSON array with expected fields
- [ ] `GetState`: returns full snapshot (state, protocol, telemetry, faults, syringe levels)
- [ ] Backend starts, listens on port 50051, accepts gRPC connections
- [ ] Backend handles frontend disconnect gracefully (no crash, injection continues)
- [ ] Sanitizers (TSan) clean on all gRPC integration tests

### Demo

Start `injector-backend`. In another terminal, use `grpcurl` or the integration test client to:
```bash
# Load protocol
grpcurl -plaintext -d '{"phases":[{"fluidType":"CONTRAST","flowRate":4.0,"volume":80,"pressureLimit":325}]}' \
  localhost:50051 injector.InjectorService/LoadProtocol

# Arm + Start
grpcurl -plaintext -d '{"command":"ARM"}' localhost:50051 injector.InjectorService/SendCommand
grpcurl -plaintext -d '{"command":"START"}' localhost:50051 injector.InjectorService/SendCommand

# Stream telemetry (watch it flow)
grpcurl -plaintext -d '{"rateMs":100}' localhost:50051 injector.InjectorService/StreamTelemetry
```
The backend is now a fully functional gRPC service. The frontend can connect.

---

## Milestone 5: Frontend Shell + Connection

**Goal:** The frontend process starts, connects to the backend via gRPC, and displays live connection status and injector state. No interactive controls yet — just proof that the two processes communicate.

**Why fifth:** The frontend is meaningless without a backend to talk to. The backend is complete (M4). Now prove the process boundary works: gRPC client connects, streams flow, reconnection works.

### Tasks

| # | Task | Files Created/Modified | Depends On |
|---|------|----------------------|------------|
| 5.1 | Frontend CMake scaffold | `frontend/CMakeLists.txt` | M4 |
| 5.2 | Proto CMake generation (frontend) | Modify `frontend/CMakeLists.txt` | 4.1 |
| 5.3 | gRPC client service (connect, reconnect, stream management) | `frontend/src/grpc/GrpcClientService.{h,cpp}` | 5.2 |
| 5.4 | InjectorBridge (minimal: connectionStatus, injectorState, telemetry properties) | `frontend/src/bridge/InjectorBridge.{h,cpp}` | 5.3 |
| 5.5 | Theme.qml singleton | `frontend/qml/Theme.qml`, `frontend/qml/qmldir` | 5.1 |
| 5.6 | ConnectionIndicator.qml | `frontend/qml/components/ConnectionIndicator.qml` | 5.5 |
| 5.7 | StateIndicator.qml | `frontend/qml/components/StateIndicator.qml` | 5.5 |
| 5.8 | main.qml (shell layout with connection + state indicators) | `frontend/qml/main.qml` | 5.6, 5.7 |
| 5.9 | Frontend main.cpp | `frontend/src/main.cpp` | 5.4, 5.8 |
| 5.10 | resources.qrc | `frontend/resources.qrc` | 5.8 |
| 5.11 | Root CMakeLists.txt update (add_subdirectory frontend) | `CMakeLists.txt` | 5.1 |

### Definition of Done

- [ ] Frontend compiles and launches as a separate process
- [ ] Shows "Connecting..." on startup, then "Connected" when backend is running
- [ ] State indicator shows current state (Idle) with correct color (green)
- [ ] Start backend after frontend — frontend auto-connects within 2 seconds
- [ ] Kill backend — frontend shows "Disconnected" (red), controls disabled
- [ ] Restart backend — frontend reconnects, shows "Connected", state refreshed via GetState
- [ ] Exponential backoff visible in logs (1s, 2s, 4s, max 10s)
- [ ] No crashes in either process during connect/disconnect cycles

### Demo

Start backend, then frontend. Green dot appears — "Connected". State shows "IDLE" in green. Kill the backend — red dot, "Disconnected". Restart backend — green dot returns. The two-process architecture works.

---

## Milestone 6: Clinical UI Panels

**Goal:** The full clinical UI is functional: protocol configuration, injection control (arm/start/pause/stop), real-time dashboard with gauges and timeline chart. A user can run a complete injection from the UI.

**Why sixth:** The connection is proven (M5). Now build the UI panels that make it usable. This is the largest frontend milestone — it turns the shell into a real application.

### Tasks

| # | Task | Files Created/Modified | Depends On |
|---|------|----------------------|------------|
| 6.1 | InjectorBridge: protocol editing (addPhase, removePhase, reorderPhases, clearProtocol) | Modify `InjectorBridge.{h,cpp}` | M5 |
| 6.2 | InjectorBridge: commands (arm, disarm, start, pause, resume, reset, emergencyStop) | Modify `InjectorBridge.{h,cpp}` | M5 |
| 6.3 | InjectorBridge: telemetry history (rolling QVariantList for chart) | Modify `InjectorBridge.{h,cpp}` | M5 |
| 6.4 | InjectorBridge: fault info + event log properties | Modify `InjectorBridge.{h,cpp}` | M5 |
| 6.5 | InjectorBridge: export data | Modify `InjectorBridge.{h,cpp}` | M5 |
| 6.6 | PhaseRow.qml (single phase inputs with validation) | `frontend/qml/components/PhaseRow.qml` | 5.5 |
| 6.7 | SyringeIndicator.qml | `frontend/qml/components/SyringeIndicator.qml` | 5.5 |
| 6.8 | ProtocolPanel.qml (phase list + add/remove/reorder + load button) | `frontend/qml/components/ProtocolPanel.qml` | 6.6, 6.7 |
| 6.9 | ControlPanel.qml (state badge + all buttons with enable/disable logic) | `frontend/qml/components/ControlPanel.qml` | 5.7 |
| 6.10 | FaultDetail.qml (fault info + acknowledge/reset) | `frontend/qml/components/FaultDetail.qml` | 5.5 |
| 6.11 | FlowRateGauge.qml | `frontend/qml/components/FlowRateGauge.qml` | 5.5 |
| 6.12 | PressureGauge.qml | `frontend/qml/components/PressureGauge.qml` | 5.5 |
| 6.13 | VolumeProgress.qml (per-phase bars + total) | `frontend/qml/components/VolumeProgress.qml` | 5.5 |
| 6.14 | ElapsedTimer.qml | `frontend/qml/components/ElapsedTimer.qml` | 5.5 |
| 6.15 | TimelineChart.qml (Canvas, dual Y-axis, phase markers) | `frontend/qml/components/TimelineChart.qml` | 6.3 |
| 6.16 | Dashboard.qml (container for gauges + chart) | `frontend/qml/components/Dashboard.qml` | 6.11-6.15 |
| 6.17 | EventLog.qml | `frontend/qml/components/EventLog.qml` | 6.4 |
| 6.18 | main.qml update (full layout: protocol panel + control panel + dashboard + event log) | Modify `frontend/qml/main.qml` | 6.8-6.17 |

### Definition of Done

- [ ] Protocol config: add/remove/reorder phases, input validation (AC-8.1, AC-8.2, AC-8.4)
- [ ] Syringe volume check rejects over-capacity protocols (AC-8.3)
- [ ] Button enable/disable matches state table exactly (AC-9.1)
- [ ] State colors correct: green/yellow/blue/orange/red/grey (AC-9.2)
- [ ] Emergency stop works from any state (AC-9.3)
- [ ] Dashboard updates within 500 ms of backend state change (AC-10.1)
- [ ] Flow rate display shows target vs actual (AC-10.2)
- [ ] Volume progress bars per phase + total (AC-10.3)
- [ ] Timeline chart shows flow rate + pressure over time with phase boundaries (AC-10.4)
- [ ] Dashboard shows baseline in idle (AC-10.5)
- [ ] Complete injection from UI: configure → arm → start → observe → complete
- [ ] Fault injection via backend → UI shows fault state, acknowledge, reset works

### Demo

The full user flow from spec 02, Flow 1: configure a 2-phase protocol (contrast + saline), load it, arm, start, watch the injection on the dashboard, see phase transition, see completion. Then Flow 2: inject an overpressure fault, see the fault display, acknowledge, reset. This is the moment the project becomes a usable application.

---

## Milestone 7: Integration + Polish

**Goal:** End-to-end testing, bug fixes, performance verification, and documentation. No new features — just making everything solid.

**Why last:** All functionality exists (M1-M6). Now verify it works together, fix what doesn't, and measure the NFRs.

### Tasks

| # | Task | Files Created/Modified | Depends On |
|---|------|----------------------|------------|
| 7.1 | Run full end-to-end checklist (spec 07, Section 3) | — | M6 |
| 7.2 | Fix bugs found during E2E testing | Various | 7.1 |
| 7.3 | Timing verification: control loop jitter analysis (NFR 1.1) | — | M6 |
| 7.4 | Performance verification: telemetry throughput (NFR 1.4), UI frame rate (NFR 1.5) | — | M6 |
| 7.5 | Export verification: CSV opens in spreadsheet, JSON is valid | — | M6 |
| 7.6 | Sanitizer pass: TSan + ASan + UBSan on full integration test suite | — | M6 |
| 7.7 | Cross-platform build verification (Linux GCC) | — | M6 |
| 7.8 | CLAUDE.md final update (all build commands, architecture summary, conventions) | `CLAUDE.md` | 7.1-7.7 |
| 7.9 | Default config.json with comments | `config.json` | M4 |

### Definition of Done

- [ ] All 17 items on the E2E checklist pass (spec 07, Section 3)
- [ ] Zero sanitizer findings (TSan, ASan, UBSan)
- [ ] Control loop timing meets NFR 1.1 (mean 2.0 ms ± 0.1 ms, max < 5 ms)
- [ ] Telemetry throughput meets NFR 1.4 (20 frames/sec sustained)
- [ ] UI frame rate ≥ 30 fps during injection (NFR 1.5)
- [ ] Builds and tests pass on Windows (MSVC) and Linux (GCC)
- [ ] `CLAUDE.md` accurately reflects the final project state
- [ ] A new developer can clone, build, and run a demo injection in < 15 minutes

### Demo

The full application, polished. Walk through the E2E checklist live: start both processes, configure a protocol, run an injection, inject a fault, recover, export data. Show the timing analysis. Show the architecture — 7 layers, all exercised.

---

## Milestone Dependencies (What Flows Where)

```
M1: HAL + Config
 │
 ├─ MotorModel ──────────┐
 ├─ PressureModel ────────┤
 ├─ ValveModel ───────────┤
 ├─ SyringeModel ─────────┼──▶ SimulatedHal ──▶ IHalInterface
 ├─ AirDetectorModel ─────┤
 └─ Config ───────────────┘
                           │
M2: Control + PID          │
 │                         │
 ├─ PidController          │
 ├─ ControlLoop ◀──────────┘ (uses IHalInterface)
 ├─ RingBuffer
 └─ DataLogger
       │
M3: State + Safety
 │
 ├─ StateMachine ◀──── CommandQueue
 ├─ SafetyMonitor ◀─── IHalInterface (independent reads)
 ├─ EventBroadcast
 └─ TelemetryBroadcast
       │
M4: gRPC
 │
 ├─ injector.proto
 └─ GrpcServer ◀──── all queues + broadcasts
       │
M5: Frontend Shell
 │
 ├─ GrpcClientService
 ├─ InjectorBridge (minimal)
 └─ main.qml (shell)
       │
M6: Clinical UI
 │
 └─ All QML components + full InjectorBridge
       │
M7: Polish
 │
 └─ E2E testing + bug fixes + verification
```

---

## Risk Mitigation Per Milestone

| Milestone | Primary Risk | Mitigation |
|-----------|-------------|------------|
| M1 | Physics model too simplistic or incorrect | Unit test every model against hand-calculated expected values. Linear model is intentionally simple — complexity comes in Phase 2. |
| M2 | Control loop can't maintain 2 ms timing on Windows | Accept slightly worse jitter on Windows (NFR allows it). If truly unworkable, increase tick rate to 5 ms with adjusted PID gains. Verify early. |
| M3 | Thread safety bugs between state machine, control loop, and safety monitor | TSan from day one. Keep shared state minimal. Follow the locking patterns in spec 03a exactly. |
| M4 | gRPC streaming introduces unexpected latency or back-pressure issues | Use lock-free ring buffers for telemetry (never block the control loop). Coalesce at the gRPC layer. Test with a slow consumer. |
| M5 | Qt6 + gRPC build integration on Windows | Solve this in M5 task 5.1 before writing any UI code. If Qt's CMake integration fights with gRPC's, isolate the proto generation. |
| M6 | QML Canvas performance insufficient for real-time chart | Timeline chart repaints at 10 Hz max (not 20 Hz). If Canvas is too slow, switch to Qt Charts `ChartView`. The component is isolated — easy swap. |
| M7 | Cross-platform issues on Linux | Build on Linux in CI from M1 onward, not just at the end. Platform-specific code (`#ifdef`) is concentrated in two files (thread pinning, thread priority). |
