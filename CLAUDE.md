# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Medical contrast power injector simulator. Two-process architecture: C++ backend (real-time control engine + gRPC server) and Qt6/QML frontend (clinical UI + gRPC client). Educational/research tool, not a medical device.

## Build & Development Commands

- Configure: `cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2019_64`
- Build all: `cmake --build build --config Release`
- Build backend only: `cmake --build build --config Release --target injector-backend`
- Build frontend only: `cmake --build build --config Release --target injector-frontend`
- Run tests: `ctest --test-dir build -C Release --output-on-failure`
- Run unit tests only: `ctest --test-dir build -C Release -R unit-tests`
- Run integration tests: `ctest --test-dir build -C Release -R integration-tests`
- Run gRPC tests: `ctest --test-dir build -C Release -R grpc-tests`
- Run specific test: `build/backend/tests/Release/injector-unit-tests.exe --gtest_filter="StateMachine.*"`
- Run backend: `./build/backend/Release/injector-backend.exe` (or with config: `./build/backend/Release/injector-backend.exe config.json`)
- Run frontend: `./build/frontend/Release/injector-frontend.exe` (start backend first)
- Frontend CLI: `./build/frontend/Release/injector-frontend.exe --backend=localhost:50051`

Note: On Windows, commands must be run from a VS Developer Shell (`Launch-VsDevShell.ps1 -Arch amd64`).
Note: Frontend requires Qt6 6.5+. If Qt6 is not found, only the backend is built.

## Architecture

Two-process, 7-layer backend architecture. See `spec/` for full details.

### Backend Layers

1. **HAL (Hardware Abstraction Layer)** — `backend/src/hal/`
   - `IHalInterface` — pure virtual interface for all hardware access
   - `SimulatedHal` — implements IHalInterface with physics models (motor→flow→syringe→pressure→air tick sequencing)
   - Models: `MotorModel` (first-order lag), `PressureModel` (linear), `ValveModel` (binary), `SyringeModel` (volume tracking), `AirDetectorModel` (atomic bool)
   - Fault injection: `injectFault()` / `clearFaults()` for overpressure, air bubble, motor stall
   - Thread-safe: `std::mutex` for state, `std::atomic<bool>` for air detector

2. **Config** — `backend/src/config/`
   - `Config` — 7-section JSON configuration with defaults and validation
   - Sections: server, control, pid, safety, hal, syringe, logging
   - Default config file: `config.json` at repo root

3. **Control Loop** — `backend/src/control/`
   - `PidController` — discrete PID with anti-windup, filtered D term, acceleration ramp
   - `ControlLoop` — dedicated 2ms tick thread, CPU affinity, reads HAL, runs PID, writes motor command
   - `TickData` — per-tick telemetry snapshot struct (~80 bytes)
   - Phase volume tracking: detects phase completion and posts PhaseComplete command

4. **Data Logging** — `backend/src/logging/`
   - `RingBuffer<T>` — header-only lock-free SPSC ring buffer (overwrite-oldest)
   - `DataLogger` — tick ring buffer + event log, CSV/JSON export

5. **State Machine** — `backend/src/state/`
   - `StateMachine` — 6 states (Idle, Armed, Injecting, Paused, Fault, Completed)
   - `InjectorState` enum, `Protocol`/`Phase` structs
   - Multi-phase progression with valve switching
   - Command processing via `CommandQueue`

6. **Safety Monitor** — `backend/src/safety/`
   - `SafetyMonitor` — independent 1ms thread, reads HAL directly
   - Checks: overpressure, air detection, motor divergence, timing violation
   - Posts faults via `FaultQueue` → StateMachine transitions to Fault

7. **gRPC Server** — `backend/src/comms/`
   - `GrpcServer` — SendCommand, LoadProtocol, InjectFault, GetState (unary RPCs)
   - StreamTelemetry (server-streaming, coalesced to requested rate)
   - StreamEvents (server-streaming, condition variable driven)
   - ExportData (CSV/JSON export via DataLogger)
   - Inter-thread: `CommandQueue`, `EventBroadcast`, `TelemetryBroadcast`, `FaultQueue`

### Frontend (Qt6/QML)

Qt6/QML process connecting to backend via gRPC on localhost:50051.

**Core:**

| File | Purpose |
|------|---------|
| `frontend/src/grpc/GrpcClientService.{h,cpp}` | gRPC channel, stub, streaming threads, reconnection with backoff |
| `frontend/src/bridge/InjectorBridge.{h,cpp}` | QObject bridge: Q_PROPERTY, Q_INVOKABLE, telemetry history, event log |

**QML Components (14 files):**

| File | Purpose |
|------|---------|
| `frontend/qml/main.qml` | ApplicationWindow, full layout |
| `frontend/qml/Theme.qml` | Singleton: colors, fonts, spacing |
| `frontend/qml/components/ConnectionIndicator.qml` | Connection status dot (green/red/yellow) |
| `frontend/qml/components/StateIndicator.qml` | Color-coded state rectangle |
| `frontend/qml/components/ProtocolPanel.qml` | Left sidebar: phase list, add/remove, syringe indicators, load button |
| `frontend/qml/components/PhaseRow.qml` | Single phase editor: fluid type, flow rate, volume, pressure limit |
| `frontend/qml/components/SyringeIndicator.qml` | Remaining volume bar |
| `frontend/qml/components/ControlPanel.qml` | State badge + arm/start/pause/resume/reset/e-stop buttons |
| `frontend/qml/components/FaultDetail.qml` | Fault list, acknowledge → reset workflow |
| `frontend/qml/components/Dashboard.qml` | Container: gauges + volume progress + timeline chart |
| `frontend/qml/components/FlowRateGauge.qml` | Target vs actual flow bar with deviation coloring |
| `frontend/qml/components/PressureGauge.qml` | Pressure bar with green/yellow/red zones |
| `frontend/qml/components/VolumeProgress.qml` | Per-phase + total volume progress bars |
| `frontend/qml/components/ElapsedTimer.qml` | MM:SS.s elapsed time display |
| `frontend/qml/components/TimelineChart.qml` | Canvas: dual Y-axis (flow/pressure), phase markers, 10Hz repaint |
| `frontend/qml/components/EventLog.qml` | Scrollable event list with timestamps and type indicators |

### Key Backend Files

| File | Purpose |
|------|---------|
| `backend/src/hal/IHalInterface.h` | HAL interface contract |
| `backend/src/hal/SimulatedHal.{h,cpp}` | Physics simulation with fault injection |
| `backend/src/config/Config.{h,cpp}` | Configuration loading + validation |
| `backend/src/control/PidController.{h,cpp}` | PID controller |
| `backend/src/control/ControlLoop.{h,cpp}` | Real-time 2ms control thread |
| `backend/src/state/StateMachine.{h,cpp}` | 6-state machine with transitions |
| `backend/src/safety/SafetyMonitor.{h,cpp}` | Independent 1ms safety thread |
| `backend/src/comms/GrpcServer.{h,cpp}` | gRPC server (7 RPCs) |
| `backend/src/logging/RingBuffer.h` | Lock-free SPSC ring buffer |
| `backend/src/logging/DataLogger.{h,cpp}` | Tick + event logging, CSV/JSON export |
| `proto/injector.proto` | gRPC service definition |
| `backend/tests/mocks/MockHal.h` | gmock HAL for unit tests |
| `backend/tests/fixtures/TestProtocols.h` | Test protocol + config fixtures |

## Test Suites

Three test executables:
- `injector-unit-tests` — fast, no threading, no gRPC (16 test files)
- `injector-integration-tests` — real threads, timing, lifecycle, fault injection, timing verification (6 test files)
- `injector-grpc-tests` — full backend + gRPC client tests (1 test file, 13 tests)

## Conventions

- C++17. Namespace: `injector::*`.
- `PascalCase` for types/files, `camelCase` for functions/variables, `trailing_` underscore for private members.
- Static library pattern: `injector-backend-lib` (all sources except main.cpp) shared by executable and test targets.
- Tests: `backend/tests/test_*.cpp`, run via CTest.
- QML: dark theme, `Theme.qml` singleton for colors/fonts/spacing.

## CI

GitHub Actions: `.github/workflows/ci.yml`
- Ubuntu Release build + tests
- Ubuntu Debug build with ASan + UBSan
- Windows Release build + tests
