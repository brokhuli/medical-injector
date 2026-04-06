# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Medical contrast power injector simulator. Two-process architecture: C++ backend (real-time control engine + gRPC server) and Qt/QML frontend (clinical UI + gRPC client). Educational/research tool, not a medical device.

## Build & Development Commands

- Configure: `cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2019_64`
- Build all: `cmake --build build --config Release`
- Build backend only: `cmake --build build --config Release --target injector-backend`
- Build frontend only: `cmake --build build --config Release --target injector-frontend`
- Run tests: `ctest --test-dir build -C Release --output-on-failure`
- Run backend: `./build/backend/Release/injector-backend.exe`
- Run frontend: `./build/frontend/Release/injector-frontend.exe` (start backend first)
- Frontend CLI: `./build/frontend/Release/injector-frontend.exe --backend=localhost:50051`

Note: On Windows, commands must be run from a VS Developer Shell (`Launch-VsDevShell.ps1 -Arch amd64`).
Note: Frontend requires Qt6 6.5+. If Qt6 is not found, only the backend is built.

## Architecture

Two-process, 7-layer backend architecture. See `spec/` for full details.

### Backend Layers (implemented so far)

1. **HAL (Hardware Abstraction Layer)** — `backend/src/hal/`
   - `IHalInterface` — pure virtual interface for all hardware access
   - `SimulatedHal` — implements IHalInterface with physics models
   - Models: `MotorModel` (first-order lag), `PressureModel` (linear), `ValveModel` (binary), `SyringeModel` (volume tracking), `AirDetectorModel` (atomic bool)
   - Thread-safe: `std::mutex` for state, `std::atomic<bool>` for air detector

2. **Config** — `backend/src/config/`
   - `Config` — 7-section JSON configuration with defaults and validation
   - Sections: server, control, pid, safety, hal, syringe, logging

3. **Control Loop** — `backend/src/control/`
   - `PidController` — discrete PID with anti-windup, filtered D term, acceleration ramp
   - `ControlLoop` — dedicated 2ms tick thread, CPU affinity, reads HAL, runs PID, writes motor command
   - `TickData` — per-tick telemetry snapshot struct (~80 bytes)

4. **Data Logging** — `backend/src/logging/`
   - `RingBuffer<T>` — header-only lock-free SPSC ring buffer (overwrite-oldest)
   - `DataLogger` — tick ring buffer + event log, CSV/JSON export

### Key Files

| File | Purpose |
|------|---------|
| `backend/src/hal/IHalInterface.h` | HAL interface contract |
| `backend/src/hal/SimulatedHal.{h,cpp}` | Physics simulation |
| `backend/src/config/Config.{h,cpp}` | Configuration loading |
| `backend/src/control/PidController.{h,cpp}` | PID controller |
| `backend/src/control/ControlLoop.{h,cpp}` | Real-time control thread |
| `backend/src/logging/RingBuffer.h` | Lock-free SPSC ring buffer |
| `backend/src/logging/DataLogger.{h,cpp}` | Tick + event logging |
| `backend/tests/mocks/MockHal.h` | gmock HAL for unit tests |

### Frontend (M5)

Qt6/QML process connecting to backend via gRPC on localhost:50051.

| File | Purpose |
|------|---------|
| `frontend/src/grpc/GrpcClientService.{h,cpp}` | gRPC channel, stub, streaming threads, reconnection |
| `frontend/src/bridge/InjectorBridge.{h,cpp}` | QObject bridge: Q_PROPERTY, Q_INVOKABLE for QML |
| `frontend/qml/main.qml` | ApplicationWindow, root layout |
| `frontend/qml/Theme.qml` | Singleton: colors, fonts, spacing |
| `frontend/qml/components/ConnectionIndicator.qml` | Connection status dot |
| `frontend/qml/components/StateIndicator.qml` | Color-coded state rectangle |

## Conventions

- C++17. Namespace: `injector::*`.
- `PascalCase` for types/files, `camelCase` for functions/variables, `trailing_` underscore for private members.
- Static library pattern: `injector-backend-lib` (all sources except main.cpp) shared by executable and test targets.
- Tests: `backend/tests/test_*.cpp`, run via CTest.
