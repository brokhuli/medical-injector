# Implementation Plan: Medical Injector Simulator

## Context

This project builds an open-source, software-only simulation of a medical contrast power injector for education and research. Nine comprehensive spec files define the architecture, contracts, tests, and build plan. **No source code exists yet** — only specs and research docs. The goal is to implement the full system following the spec's 7-milestone vertical-slice strategy.

**Two-process architecture:**

- **Backend:** C++17 real-time control engine (4 threads + gRPC server). 7-layer design: HAL → Control Loop (2ms tick) → Safety Monitor (1ms tick) → State Machine → gRPC Server → Data Logging → Config
- **Frontend:** Qt6/QML clinical UI connecting to backend via gRPC on localhost:50051

---

## Session Breakdown

The spec defines 7 milestones (M1-M7) in a strict dependency chain. Each milestone is split into 1-3 Claude Code sessions of 3-6 tasks each for manageable scope. **14 sessions total** plus a pre-implementation setup.

### S0: Environment & Toolchain Verification

Verify everything compiles before writing project code.

- [x] Verify/install vcpkg; set `VCPKG_ROOT` env variable
- [x] Install dependencies: `vcpkg install grpc protobuf spdlog nlohmann-json gtest --triplet=x64-windows`
- [x] Verify Qt6 installation (6.5+ with Quick, QuickControls2); confirm `Qt6_DIR` is set
- [x] Build a trivial CMake+gRPC hello-world to prove toolchain works

**Windows-specific concerns:**

- gRPC first build via vcpkg takes 20-40 min (compiles abseil, re2, c-ares, zlib, openssl, protobuf, grpc)
- TSan/UBSan unavailable on MSVC — only ASan (`/fsanitize=address`). Full sanitizer coverage requires Linux (CI or WSL2)
- Qt6 may ship its own protobuf; if it conflicts with vcpkg's, isolate proto generation as a separate static library target
- Windows timing jitter will be 2-5x worse than Linux (spec acknowledges this)

**Gate:** CMake configure succeeds with vcpkg; gRPC hello-world compiles; Qt6 QML app launches a window.

---

### S1: Build Infrastructure (M1, Tasks 1.1-1.4)

Spec refs: `spec/06-repo-structure.md` Sections 5-8

- [x] Update `.gitignore` with full version from spec 06 Section 8
- [x] Create `.clang-format` from spec 06 Section 6 (Google style, 4-space indent, 100-col)
- [x] Create `.clang-tidy` from spec 06 Section 7
- [x] Create `vcpkg.json` manifest (grpc, protobuf, spdlog, nlohmann-json, gtest)
- [x] Create root `CMakeLists.txt` (C++17, find dependencies, NO frontend subdirectory yet)
- [x] Create `backend/CMakeLists.txt` with `injector-backend-lib` static library pattern (spec 07 Section 5) + `injector-backend` executable
- [x] Create `backend/tests/CMakeLists.txt` with placeholder test
- [x] Create placeholder `backend/src/main.cpp`
- [x] Set up GitHub Actions CI (ubuntu + windows matrix, vcpkg caching, build + test)

**Gate:** `cmake -B build && cmake --build build && ctest --test-dir build` succeeds. CI green on both platforms.

---

### S2: HAL Interface + Physics Models (M1, Tasks 1.5-1.10)

Spec refs: `spec/03c-architecture-simulated-devices.md`, `spec/04-schema-and-contracts.md` Section 4

Build interface first, then models in dependency order, tests alongside each. All in `namespace injector::hal`.

- [x] `backend/src/hal/IHalInterface.h` — pure abstract class with Barrel, FluidChannel, ValveState enums, SimulatedFault struct
- [x] `backend/src/hal/MotorModel.{h,cpp}` + `backend/tests/test_motor_model.cpp` — first-order lag (AC-1.2)
- [x] `backend/src/hal/PressureModel.{h,cpp}` + `backend/tests/test_pressure_model.cpp` — linear: pressure = flow \* resistance + baseline (AC-1.1)
- [x] `backend/src/hal/ValveModel.{h,cpp}` + `backend/tests/test_valve_model.cpp` — binary state (AC-1.3)
- [x] `backend/src/hal/SyringeModel.{h,cpp}` + `backend/tests/test_syringe_model.cpp` — volume drain tracking (AC-1.5)
- [x] `backend/src/hal/AirDetectorModel.h` + `backend/tests/test_air_detector.cpp` — atomic bool (AC-1.4)

**Gate:** All model unit tests pass. `clang-format --dry-run --Werror` passes.

---

### S3: SimulatedHal + Config + Main (M1, Tasks 1.11-1.15)

Spec refs: `spec/03c-architecture-simulated-devices.md`, `spec/04-schema-and-contracts.md` Section 5

- [x] `backend/src/hal/SimulatedHal.{h,cpp}` + `backend/tests/test_simulated_hal_integration.cpp` — assembles all models, tick sequencing (motor→flow→syringe→pressure→air), thread-safe with `std::mutex` + `std::atomic<bool>` for air
- [x] `backend/tests/mocks/MockHal.h` — gmock implementation from spec 07
- [x] `backend/src/config/Config.{h,cpp}` + `backend/tests/test_config.cpp` — 7 config sections, JSON parsing via nlohmann/json, defaults, range validation
- [x] `backend/src/main.cpp` — parse config, create HAL, log initial state, exit
- [x] Update `CLAUDE.md` with build commands and architecture summary

**Gate:** `injector-backend` starts, logs initial state (pressure: 10 psi, motor: 0 RPM, syringes: 100/50 mL), exits. All tests green. Commit to main.

---

### S4: Control Loop + PID (M2, Tasks 2.1-2.6)

Spec refs: `spec/03a-architecture-backend.md` (PID section, ControlLoop section)

- [x] `backend/src/logging/RingBuffer.h` + `backend/tests/test_ring_buffer.cpp` — header-only SPSC template, lock-free
- [x] `backend/src/control/PidController.{h,cpp}` + `backend/tests/test_pid_controller.cpp` — discrete PID with anti-windup + acceleration ramp (AC-3.1 through AC-3.4). Test deterministically with fixed dt=0.002
- [x] `backend/src/control/ControlLoop.{h,cpp}` + `backend/tests/test_control_loop.cpp` — dedicated thread, 2ms tick, CPU affinity (`#ifdef` for Win/Linux), reads HAL, runs PID, writes motor command
- [x] `backend/src/logging/DataLogger.{h,cpp}` + `backend/tests/test_data_logger.cpp` — ring buffer of TickData, event log
- [x] Update `main.cpp` — spawn control loop at 4.0 mL/s, run 10s, log timing stats + volume

**Gate:** `injector-backend` runs 10s, prints timing stats (mean ~2.0ms, max <5ms) and volume (~40 mL). All tests green.

---

### S5: Inter-Thread Data Structures (M3, Tasks 3.1-3.4)

Spec refs: `spec/04-schema-and-contracts.md` Sections 1, 3

- [x] `backend/src/state/InjectorState.h` — 6-state enum class
- [x] `backend/src/state/Protocol.h` — Protocol + Phase structs
- [x] `backend/src/comms/CommandQueue.h` + `backend/tests/test_command_queue.cpp` — mutex-protected queue
- [x] `backend/src/comms/EventBroadcast.h` — mutex + condition_variable
- [x] `backend/src/comms/TelemetryBroadcast.h` — SPSC ring buffer wrapper (128 entries)

**Gate:** All data structure tests pass.

---

### S6: StateMachine + SafetyMonitor (M3, Tasks 3.5-3.8)

Spec refs: `spec/03a-architecture-backend.md` (state machine, safety monitor sections), `spec/04-schema-and-contracts.md` Section 1

- [x] `backend/src/state/StateMachine.{h,cpp}` + `backend/tests/test_state_machine.cpp` — all transitions from spec 04 table, command processing, multi-phase progression (AC-4.1 through AC-4.4). Pure logic tests, no threads.
- [x] `backend/src/safety/SafetyMonitor.{h,cpp}` + `backend/tests/test_safety_monitor.cpp` — independent 1ms thread, reads HAL directly, checks overpressure/air/motor divergence/timing (AC-5.1 through AC-5.4). Unit test with MockHal.
- [x] Wire ControlLoop ↔ StateMachine via `ControlTargets` atomics
- [x] Wire SafetyMonitor → StateMachine via fault queue

**Gate:** All state machine and safety monitor tests pass.

---

### S7: Fault Injection + Integration Tests (M3, Tasks 3.9-3.13)

Spec refs: `spec/03c-architecture-simulated-devices.md` (fault mechanism), `spec/07-test-strategy.md` Section 2

- [x] Fault injection in SimulatedHal — `injectFault()` / `clearFaults()` per spec 03c fault table
- [x] `backend/tests/test_injection_lifecycle.cpp` — full 2-phase happy path, volume ±2% per phase
- [x] `backend/tests/test_fault_end_to_end.cpp` — overpressure mid-injection → fault within 10ms
- [x] `backend/tests/test_safety_integration.cpp` — safety independence (AC-5.5)
- [x] Update `main.cpp` — spawn all 3 threads, run hardcoded 2-phase protocol, log transitions

**Gate:** All integration tests pass. `injector-backend` completes 2-phase injection with state transitions logged.

---

### S8: Proto + gRPC Server Core (M4, Tasks 4.1-4.5)

Spec refs: `spec/04-schema-and-contracts.md` Section 2

- [x] `proto/injector.proto` — complete definition from spec 04 Section 2
- [x] CMake proto generation in `backend/CMakeLists.txt`
- [x] `backend/src/comms/GrpcServer.{h,cpp}` — SendCommand, LoadProtocol, InjectFault, GetState (unary RPCs)
- [x] StreamTelemetry (server-streaming, coalesced to requested rate)
- [x] StreamEvents (server-streaming, condition variable driven)

**Gate:** Backend compiles with gRPC. Unary RPCs work via grpcurl.

---

### S9: Export + gRPC Integration Tests (M4, Tasks 4.6-4.11)

- [x] CSV + JSON export in DataLogger + ExportData RPC
- [x] Update `main.cpp` — start gRPC server, block on `server->Wait()`, signal handling for shutdown
- [x] `backend/tests/test_grpc_integration.cpp` — command round-trip (AC-6.1), telemetry streaming (AC-6.2), invalid rejection (AC-6.3), fault injection (AC-6.4), export
- [x] `backend/tests/test_config_integration.cpp` — config propagation verification

**Gate:** All gRPC integration tests pass. Backend runs as persistent server, controllable via grpcurl. Backend is feature-complete.

---

### S10: Frontend Shell + Connection (M5, Tasks 5.1-5.11)

Spec refs: `spec/03b-architecture-frontend.md`

**Highest toolchain risk** — Qt6 + gRPC + vcpkg CMake integration.

- [x] `frontend/CMakeLists.txt` — Qt6, gRPC, proto generation
- [x] Root `CMakeLists.txt` — add `add_subdirectory(frontend)`
- [x] `frontend/src/grpc/GrpcClientService.{h,cpp}` — channel management, reconnection with exponential backoff
- [x] `frontend/src/bridge/InjectorBridge.{h,cpp}` — minimal QObject with Q_PROPERTY for connection status, state, basic telemetry
- [x] `frontend/qml/Theme.qml` + `qmldir` — color/font/spacing singleton
- [x] `frontend/qml/components/ConnectionIndicator.qml` + `StateIndicator.qml`
- [x] `frontend/qml/main.qml` — shell layout
- [x] `frontend/src/main.cpp` — QGuiApplication + QQmlEngine setup
- [x] `frontend/resources.qrc`
- [x] Add Qt6 to CI matrix

**Gate:** Start backend then frontend → green dot "Connected", state "IDLE". Kill backend → red dot. Restart → reconnects. No crashes.

---

### S11: InjectorBridge Full (M6, Tasks 6.1-6.5)

- [x] Protocol editing methods (addPhase, removePhase, reorderPhases, clearProtocol, loadProtocol)
- [x] Command methods (arm, disarm, start, pause, resume, reset, emergencyStop)
- [x] Telemetry history (rolling QVariantList, 6000 frames / 5 min)
- [x] Fault info + event log properties
- [x] Export data method

**Gate:** All bridge methods functional, emitting proper signals.

---

### S12: Protocol + Control Panels (M6, Tasks 6.6-6.10)

- [x] `PhaseRow.qml` — fluid type, flow rate, volume, pressure limit inputs with validation
- [x] `SyringeIndicator.qml` — remaining volume display
- [x] `ProtocolPanel.qml` — phase list, add/remove/reorder, load button
- [x] `ControlPanel.qml` — state badge, buttons with enable/disable logic per spec 03b state table
- [x] `FaultDetail.qml` — fault info, acknowledge, reset

**Gate:** Can configure protocol and control injection lifecycle from UI.

---

### S13: Dashboard + Event Log (M6, Tasks 6.11-6.18)

- [x] `FlowRateGauge.qml`, `PressureGauge.qml`, `VolumeProgress.qml`, `ElapsedTimer.qml`
- [x] `TimelineChart.qml` — Canvas, dual Y-axis, phase markers, 10 Hz repaint
- [x] `Dashboard.qml` — container for all gauges + chart
- [x] `EventLog.qml` — scrollable ListView
- [x] `main.qml` update — full layout (ProtocolPanel left, ControlPanel + Dashboard right, EventLog bottom)

**Gate:** Complete user flow: configure → arm → start → observe dashboard → phase transition → completion. Fault injection → fault display → acknowledge → reset. All AC-8/9/10 criteria met.

---

### S14: Integration + Polish (M7, Tasks 7.1-7.9)

- [x] Run full 17-item E2E checklist (spec 07 Section 3)
- [x] Fix all bugs found
- [x] Timing verification: 10,000+ ticks, verify NFR 1.1 (mean 2.0ms ±0.1ms, max <5ms)
- [x] Performance verification: telemetry 20 fps (NFR 1.4), UI 30+ fps (NFR 1.5)
- [x] Export verification: CSV opens in spreadsheet, JSON valid
- [x] Sanitizer pass: ASan on Windows, full TSan+ASan+UBSan on Linux
- [x] Cross-platform build verification (Linux GCC)
- [x] `CLAUDE.md` final update
- [x] `config.json` default file at repo root

**Gate:** All 17 E2E items pass. Zero sanitizer findings. Builds on Windows + Linux. New developer can clone, build, and run in <15 minutes.

---

## Key Spec File References

| Session | Primary Spec References                                                                                                          |
| ------- | -------------------------------------------------------------------------------------------------------------------------------- |
| S1      | `spec/06-repo-structure.md` Sections 5-8                                                                                         |
| S2      | `spec/03c-architecture-simulated-devices.md`, `spec/04-schema-and-contracts.md` Section 4                                        |
| S3      | `spec/03c-architecture-simulated-devices.md`, `spec/04-schema-and-contracts.md` Section 5, `spec/06-repo-structure.md` Section 4 |
| S4      | `spec/03a-architecture-backend.md` (PID + ControlLoop sections)                                                                  |
| S5-S7   | `spec/03a-architecture-backend.md` (StateMachine + Safety), `spec/04-schema-and-contracts.md` Sections 1, 3                      |
| S8-S9   | `spec/04-schema-and-contracts.md` Section 2 (proto), `spec/07-test-strategy.md` Section 2                                        |
| S10-S13 | `spec/03b-architecture-frontend.md`                                                                                              |
| S14     | `spec/07-test-strategy.md` Section 3, `spec/05-non-functional-requirements.md`                                                   |

## CLAUDE.md Update Strategy

Update incrementally at milestone boundaries (not just at the end):

- **S1:** Build commands (cmake configure/build/test)
- **S3 (end M1):** Architecture summary, conventions, key files per spec 06 Section 4
- **S4 (end M2):** Control loop run instructions
- **S9 (end M4):** gRPC server run instructions
- **S10 (end M5):** Frontend build/run instructions
- **S14 (M7):** Final comprehensive update

## Verification

After each session, run:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

After M7 completion, run the full 17-item E2E checklist from spec 07 Section 3.
