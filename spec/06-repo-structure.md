# Repository & Code Structure

This document defines the folder layout, naming conventions, coding standards, and patterns for the medical injector simulator. It is the scaffold — implementation fills it in.

---

## 1. Folder Structure

```
medical-injector/
├── CMakeLists.txt                         # Root CMake — defines project, adds subdirectories
├── CLAUDE.md                              # Claude Code project context (build commands, architecture, conventions)
├── .gitignore
├── .clang-tidy                            # Static analysis config
├── .clang-format                          # Code formatting config
│
├── proto/
│   └── injector.proto                     # Single source of truth for gRPC contract
│
├── backend/
│   ├── CMakeLists.txt                     # Backend target: injector-backend
│   ├── src/
│   │   ├── main.cpp                       # Entry point: parse config, init components, start threads + gRPC server
│   │   │
│   │   ├── hal/
│   │   │   ├── IHalInterface.h            # Abstract HAL interface
│   │   │   ├── SimulatedHal.h             # Simulated HAL header
│   │   │   ├── SimulatedHal.cpp           # Simulated HAL implementation
│   │   │   ├── MotorModel.h               # Motor physics
│   │   │   ├── MotorModel.cpp
│   │   │   ├── PressureModel.h            # Pressure physics
│   │   │   ├── PressureModel.cpp
│   │   │   ├── ValveModel.h               # Valve state
│   │   │   ├── ValveModel.cpp
│   │   │   ├── SyringeModel.h             # Syringe volume tracking
│   │   │   ├── SyringeModel.cpp
│   │   │   └── AirDetectorModel.h         # Air detector (header-only, trivial)
│   │   │
│   │   ├── control/
│   │   │   ├── ControlLoop.h              # Control loop thread — tick driver + PID
│   │   │   ├── ControlLoop.cpp
│   │   │   ├── PidController.h            # Discrete PID with anti-windup
│   │   │   └── PidController.cpp
│   │   │
│   │   ├── safety/
│   │   │   ├── SafetyMonitor.h            # Safety monitor thread
│   │   │   └── SafetyMonitor.cpp
│   │   │
│   │   ├── state/
│   │   │   ├── StateMachine.h             # State machine + command processing
│   │   │   ├── StateMachine.cpp
│   │   │   ├── InjectorState.h            # State enum + transition logic
│   │   │   └── Protocol.h                 # Protocol and Phase structs
│   │   │
│   │   ├── comms/
│   │   │   ├── GrpcServer.h               # gRPC service implementation
│   │   │   ├── GrpcServer.cpp
│   │   │   ├── CommandQueue.h             # Thread-safe command queue
│   │   │   ├── TelemetryBroadcast.h       # Lock-free SPSC ring buffer for telemetry
│   │   │   └── EventBroadcast.h           # Mutex + condition_variable event broadcast
│   │   │
│   │   ├── logging/
│   │   │   ├── DataLogger.h               # Ring buffer tick data + event log
│   │   │   ├── DataLogger.cpp
│   │   │   └── RingBuffer.h               # Generic lock-free SPSC ring buffer (header-only, template)
│   │   │
│   │   └── config/
│   │       ├── Config.h                   # Config struct + defaults
│   │       └── Config.cpp                 # JSON parsing, CLI override, validation
│   │
│   └── tests/
│       ├── CMakeLists.txt                 # Test target: injector-backend-tests
│       ├── test_motor_model.cpp
│       ├── test_pressure_model.cpp
│       ├── test_valve_model.cpp
│       ├── test_syringe_model.cpp
│       ├── test_pid_controller.cpp
│       ├── test_state_machine.cpp
│       ├── test_safety_monitor.cpp
│       ├── test_control_loop.cpp          # Uses mock HAL
│       ├── test_ring_buffer.cpp
│       ├── test_config.cpp
│       ├── test_grpc_integration.cpp      # gRPC client test against real server
│       └── mocks/
│           └── MockHal.h                  # Mock IHalInterface for unit tests
│
├── frontend/
│   ├── CMakeLists.txt                     # Frontend target: injector-frontend
│   ├── src/
│   │   ├── main.cpp                       # QGuiApplication + QQmlEngine setup
│   │   ├── bridge/
│   │   │   ├── InjectorBridge.h           # QObject bridge: Q_PROPERTY, Q_INVOKABLE
│   │   │   └── InjectorBridge.cpp
│   │   └── grpc/
│   │       ├── GrpcClientService.h        # gRPC channel, stub, streaming threads
│   │       └── GrpcClientService.cpp
│   ├── qml/
│   │   ├── main.qml                       # ApplicationWindow, root layout
│   │   ├── Theme.qml                      # Singleton: colors, fonts, spacing
│   │   ├── qmldir                         # QML module definition
│   │   └── components/
│   │       ├── ProtocolPanel.qml           # Protocol configuration sidebar
│   │       ├── PhaseRow.qml               # Single phase editor row
│   │       ├── SyringeIndicator.qml       # Remaining volume display
│   │       ├── ControlPanel.qml           # State badge + control buttons
│   │       ├── StateIndicator.qml         # Color-coded state rectangle
│   │       ├── FaultDetail.qml            # Fault info + acknowledge/reset
│   │       ├── ConnectionIndicator.qml    # Connection status dot
│   │       ├── Dashboard.qml              # Container for all gauges
│   │       ├── FlowRateGauge.qml          # Target vs actual display
│   │       ├── PressureGauge.qml          # With threshold indicator
│   │       ├── VolumeProgress.qml         # Per-phase progress bars
│   │       ├── TimelineChart.qml          # Real-time chart
│   │       ├── ElapsedTimer.qml           # Running clock
│   │       └── EventLog.qml              # Scrollable event list
│   └── resources.qrc                      # Qt resource file
│
├── spec/                                  # Design specs (this directory)
│   ├── 01-problem-framing.md
│   ├── 02-functional-specification.md
│   ├── 03a-architecture-backend.md
│   ├── 03b-architecture-frontend.md
│   ├── 03c-architecture-simulated-devices.md
│   ├── 04-schema-and-contracts.md
│   ├── 05-non-functional-requirements.md
│   ├── 06-repo-structure.md
│   ├── 07-test-strategy.md
│   ├── 08-developer-workflow.md
│   └── 09-incremental-build-plan.md
│
└── research/                              # Background research (not built)
    ├── realtime-control-research.md
    └── greenfield-development.md
```

### Key Structural Decisions

**Shared `proto/` directory at repo root.** Both backend and frontend CMake targets reference the same `.proto` file. No symlinks, no copies — one file, two consumers. The generated code lives in each target's build directory (out-of-source).

**Backend `src/` organized by architectural layer.** The directory names map directly to the 7-layer architecture: `hal/`, `control/`, `safety/`, `state/`, `comms/`, `logging/`, `config/`. A developer reading the architecture spec can find the corresponding code immediately.

**Frontend `src/` organized by role.** `bridge/` is the QML-to-gRPC boundary. `grpc/` is the network client. `qml/components/` contains all visual components. Simple and flat.

**Tests are colocated with their target.** `backend/tests/` contains all backend tests. No separate top-level `tests/` directory — tests belong to the thing they test.

**No `include/` directory.** Headers live next to their `.cpp` files. The backend is a single target, not a library consumed by others — a separate `include/` directory would add indirection with no benefit.

---

## 2. Naming Conventions

### Files

| Type           | Convention            | Examples                                              |
| -------------- | --------------------- | ----------------------------------------------------- |
| C++ header     | `PascalCase.h`        | `SimulatedHal.h`, `PidController.h`, `StateMachine.h` |
| C++ source     | `PascalCase.cpp`      | `SimulatedHal.cpp`, `PidController.cpp`               |
| Header-only    | `PascalCase.h`        | `RingBuffer.h`, `AirDetectorModel.h`                  |
| Test files     | `test_snake_case.cpp` | `test_motor_model.cpp`, `test_state_machine.cpp`      |
| QML components | `PascalCase.qml`      | `ControlPanel.qml`, `FlowRateGauge.qml`               |
| Proto files    | `snake_case.proto`    | `injector.proto`                                      |
| Config files   | `snake_case.json`     | `config.json`                                         |
| CMake files    | `CMakeLists.txt`      | (standard)                                            |

### C++ Identifiers

| Type                      | Convention                             | Examples                                                  |
| ------------------------- | -------------------------------------- | --------------------------------------------------------- |
| Classes / structs         | `PascalCase`                           | `SimulatedHal`, `PidController`, `MotorModel`             |
| Functions / methods       | `camelCase`                            | `readPressure()`, `setMotorRpm()`, `emergencyStop()`      |
| Variables (local, member) | `camelCase`                            | `targetFlowRate`, `actualRpm`, `phaseIndex`               |
| Private members           | `camelCase_` (trailing underscore)     | `stateMutex_`, `motorModel_`, `running_`                  |
| Constants                 | `UPPER_SNAKE_CASE`                     | `MAX_RPM`, `DEFAULT_TICK_RATE_MS`                         |
| Enums                     | `PascalCase` type, `PascalCase` values | `enum class InjectorState { Idle, Armed, Injecting }`     |
| Namespaces                | `snake_case`                           | `namespace injector`, `namespace injector::hal`           |
| Template parameters       | `PascalCase`                           | `template<typename T>`                                    |
| Macros (avoid)            | `UPPER_SNAKE_CASE`                     | Only for include guards if `#pragma once` is insufficient |

### Namespaces

All backend code lives under the `injector` namespace, with sub-namespaces per layer:

```cpp
namespace injector {
namespace hal { ... }
namespace control { ... }
namespace safety { ... }
namespace state { ... }
namespace comms { ... }
namespace logging { ... }
namespace config { ... }
}
```

Frontend C++ code uses `namespace injector::ui`.

### QML / JavaScript

| Type                 | Convention                       | Examples                               |
| -------------------- | -------------------------------- | -------------------------------------- |
| Component names      | `PascalCase` (matching filename) | `ControlPanel`, `FlowRateGauge`        |
| Properties           | `camelCase`                      | `targetFlowRate`, `injectorState`      |
| Signal handlers      | `onCamelCase`                    | `onTelemetryChanged`, `onStateChanged` |
| JavaScript functions | `camelCase`                      | `formatPressure()`, `updateChart()`    |
| IDs                  | `camelCase`                      | `flowRateLabel`, `pressureGauge`       |

---

## 3. Coding Standards & Patterns

### Header Guards

Use `#pragma once` in all headers. It is supported by GCC, Clang, and MSVC.

### Include Order

```cpp
// 1. Corresponding header (for .cpp files)
#include "SimulatedHal.h"

// 2. Project headers
#include "hal/MotorModel.h"
#include "state/InjectorState.h"

// 3. Third-party headers
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

// 4. Standard library headers
#include <atomic>
#include <mutex>
#include <thread>
```

Blank line between each group. Alphabetical within each group.

### Memory Management

- **RAII everywhere.** No raw `new`/`delete`.
- `std::unique_ptr` for exclusive ownership (most cases).
- `std::shared_ptr` for the HAL instance (shared between control loop, safety monitor, and state machine).
- Stack allocation for small, short-lived objects.
- No custom allocators for MVP.

### Error Handling

- **No exceptions in the real-time path.** The control loop and safety monitor must never throw. Use return values or error codes.
- **Exceptions allowed at startup** (config parsing, gRPC server initialization) — these are recoverable or fatal.
- **`std::optional<T>`** for functions that may not produce a result.
- **`spdlog::error()`** for logging error conditions; do not use `std::cerr` directly.
- **gRPC errors** are returned via `CommandResponse.success = false` with a descriptive `error` string.

### Threading

- **`std::thread`** for all application threads. No framework thread pools (except gRPC's internal pool).
- **`std::mutex` + `std::lock_guard`** for shared state. Always use `lock_guard` or `unique_lock` — never raw `lock()`/`unlock()`.
- **`std::atomic`** for single-value hot-path reads (control targets, air detector flag).
- **`std::condition_variable`** for event notification (event broadcast to gRPC handler).
- **Never hold a mutex while calling external code** (gRPC, spdlog). Copy data under the lock, then use the copy.
- **No `std::recursive_mutex`.** If you think you need one, the locking design is wrong.

### Const Correctness

- Sensor read methods are `const` (`readPressure() const`).
- Config structs are `const` after construction.
- Use `const auto&` for loop variables over collections.
- Mark methods `const` unless they modify state.

### Early Returns (Never-Nesting)

- Prefer guard clauses and early returns over deep nesting.
- Maximum nesting depth: 2 levels. If deeper, refactor using early returns, `continue`, or extract a helper.
- Place the "happy path" at the lowest indentation level.

### No Magic Numbers

- Extract numeric literals into named `constexpr` constants or config values.
- Physics constants, thresholds, limits, and array sizes should all have descriptive names.
- Acceptable exceptions: `0`, `1`, `-1`, `0.0`, `1.0`, and `nullptr` where meaning is obvious from context.

### `[[nodiscard]]`

- Apply `[[nodiscard]]` to functions where ignoring the return value is likely a bug.
- All sensor read methods (`readPressure()`, `readMotorRpm()`, etc.) should be `[[nodiscard]]`.
- Safety check functions and validation functions should be `[[nodiscard]]`.

### Rule of Zero

- If a class uses only RAII members (smart pointers, containers, `std::atomic`, `std::mutex`), do not write a destructor, copy/move constructor, or copy/move assignment operator.
- Only write special member functions when the class directly manages a resource (raw handle, file descriptor). In this project, that should be never.

### Prefer `auto` with Initializers

- Use `auto` when the type is obvious from the right-hand side: `auto hal = std::make_shared<SimulatedHal>(cfg);`
- Spell out the type when it is not obvious: `double pressure = computePressure();`
- Always use `auto` for iterator types and structured bindings.

### Single Responsibility for Functions

- Each function should do one thing. If a function needs a comment explaining what "this part" does, extract that part into a named function.
- Aim for functions that fit on one screen (~40 lines). Longer is acceptable if the logic is linear and sequential.
- Pairs with never-nesting: short, focused functions rarely nest deeply.

### Type Safety

- **`enum class`** (scoped enums) for all enumerations. Never bare `enum`.
- **Explicit integer types** from `<cstdint>`: `uint64_t`, `int32_t`, `uint8_t`.
- **No implicit narrowing conversions.** Use `static_cast<>` when necessary.
- **`std::chrono` types** for all time values. No bare `int milliseconds` — use `std::chrono::milliseconds`.

---

## 4. CLAUDE.md Project Context

The `CLAUDE.md` file at the repo root should be updated as the project develops. Here is the target content once the scaffold is built:

```markdown
# CLAUDE.md

## Project Overview

Medical contrast power injector simulator. Two-process architecture: C++ backend (real-time control engine + gRPC server) and Qt/QML frontend (clinical UI + gRPC client). Educational/research tool, not a medical device.

## Build & Development Commands

- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Debug`
- Build all: `cmake --build build`
- Build backend only: `cmake --build build --target injector-backend`
- Build frontend only: `cmake --build build --target injector-frontend`
- Run backend: `./build/backend/injector-backend [--config config.json]`
- Run frontend: `./build/frontend/injector-frontend [--backend=localhost:50051]`
- Run tests: `ctest --test-dir build --output-on-failure`
- Run backend tests only: `ctest --test-dir build -R backend`
- Lint: `clang-tidy -p build backend/src/**/*.cpp`
- Format: `clang-format -i backend/src/**/*.{h,cpp} frontend/src/**/*.{h,cpp}`

## Architecture

Two-process, 7-layer architecture:

1. **HAL** (`backend/src/hal/`) — Simulated hardware: motor, pressure, valves, syringes, air detector
2. **Scheduling** (`backend/src/control/ControlLoop`) — Dedicated thread, 2 ms tick, pinned core
3. **Control Loop** (`backend/src/control/`) — PID controller, flow rate → motor RPM
4. **State Machine** (`backend/src/state/`) — Idle → Armed → Injecting → Paused → Completed → Fault
5. **Safety Monitor** (`backend/src/safety/`) — Independent thread, 1 ms tick, reads HAL directly
6. **Command Interface** (`backend/src/comms/`) — gRPC server on localhost:50051
7. **Data Logging** (`backend/src/logging/`) — Ring buffer tick data, event log, CSV/JSON export

Frontend (`frontend/`) connects via gRPC. InjectorBridge (QObject) bridges QML ↔ gRPC.

## Conventions

- C++17 minimum. Namespace: `injector::*`.
- `PascalCase` for types and files, `camelCase` for functions and variables, `trailing_` underscore for private members.
- No exceptions in real-time path (control loop, safety monitor).
- `IHalInterface` is the abstraction boundary — control/safety code depends on the interface, not SimulatedHal.
- Proto file: `proto/injector.proto` (shared by both targets).
- Tests: `backend/tests/test_*.cpp`, run with CTest.

## Key Files

- `proto/injector.proto` — gRPC contract (single source of truth for frontend ↔ backend API)
- `backend/src/hal/IHalInterface.h` — Hardware abstraction interface
- `backend/src/state/InjectorState.h` — State enum + transition rules
- `backend/src/state/Protocol.h` — Protocol and Phase data types
- `backend/src/config/Config.h` — All configuration with defaults
```

---

## 5. CMake Structure

### Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(medical-injector VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)

# ── Proto generation ─────────────────────────────────────────
# Generated sources go into each target's build dir.
# Helper function defined here, used by backend/ and frontend/.

# ── Subdirectories ───────────────────────────────────────────
add_subdirectory(backend)
add_subdirectory(frontend)
```

### Backend CMakeLists.txt (sketch)

```cmake
add_executable(injector-backend
    src/main.cpp
    src/hal/SimulatedHal.cpp
    src/hal/MotorModel.cpp
    src/hal/PressureModel.cpp
    src/hal/ValveModel.cpp
    src/hal/SyringeModel.cpp
    src/control/ControlLoop.cpp
    src/control/PidController.cpp
    src/safety/SafetyMonitor.cpp
    src/state/StateMachine.cpp
    src/comms/GrpcServer.cpp
    src/logging/DataLogger.cpp
    src/config/Config.cpp
    # + generated proto sources
)

target_include_directories(injector-backend PRIVATE src)
target_link_libraries(injector-backend PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Sanitizers in Debug
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(injector-backend PRIVATE
        -fsanitize=address,undefined,thread
    )
    target_link_options(injector-backend PRIVATE
        -fsanitize=address,undefined,thread
    )
endif()

# Warnings
target_compile_options(injector-backend PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Werror -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# Tests — see 07-test-strategy.md, Section 5 for the test CMakeLists.txt
enable_testing()
add_subdirectory(tests)
```

### Frontend CMakeLists.txt (sketch)

```cmake
find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)

add_executable(injector-frontend
    src/main.cpp
    src/bridge/InjectorBridge.cpp
    src/grpc/GrpcClientService.cpp
    # + generated proto sources
)

target_include_directories(injector-frontend PRIVATE src)
target_link_libraries(injector-frontend PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    gRPC::grpc++
    protobuf::libprotobuf
)

# QML module — component list matches the folder structure in Section 1
qt_add_qml_module(injector-frontend
    URI MedicalInjector
    VERSION 1.0
    QML_FILES qml/main.qml qml/Theme.qml qml/components/*.qml
)
```

---

## 6. .clang-format

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
AccessModifierOffset: -4
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
BreakBeforeBraces: Attach
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^"'
    Priority: 1
  - Regex: "^<spdlog/"
    Priority: 2
  - Regex: "^<nlohmann/"
    Priority: 2
  - Regex: "^<grpc"
    Priority: 2
  - Regex: "^<"
    Priority: 3
SortIncludes: CaseSensitive
SpaceAfterCStyleCast: false
SpaceAfterTemplateKeyword: true
```

## 7. .clang-tidy

```yaml
Checks: >
  -*,
  bugprone-*,
  cppcoreguidelines-*,
  modernize-*,
  performance-*,
  readability-*,
  -modernize-use-trailing-return-type,
  -readability-magic-numbers,
  -cppcoreguidelines-avoid-magic-numbers,
  -cppcoreguidelines-pro-bounds-array-to-pointer-decay,
  -readability-identifier-length

WarningsAsErrors: >
  bugprone-use-after-move,
  bugprone-dangling-handle,
  cppcoreguidelines-no-malloc,
  modernize-use-nullptr,
  modernize-use-override

HeaderFilterRegex: "backend/src/.*|frontend/src/.*"

CheckOptions:
  - key: readability-identifier-naming.ClassCase
    value: CamelCase
  - key: readability-identifier-naming.FunctionCase
    value: camelBack
  - key: readability-identifier-naming.VariableCase
    value: camelBack
  - key: readability-identifier-naming.PrivateMemberSuffix
    value: "_"
  - key: readability-identifier-naming.ConstantCase
    value: UPPER_CASE
  - key: readability-identifier-naming.EnumConstantCase
    value: CamelCase
  - key: readability-identifier-naming.NamespaceCase
    value: lower_case
```

## 8. .gitignore

```gitignore
# Build
build/
cmake-build-*/
out/

# IDE
.idea/
.vscode/
*.swp
*.swo
*~

# OS
.DS_Store
Thumbs.db

# Compiled
*.o
*.obj
*.exe
*.dll
*.so
*.dylib

# Qt
moc_*
qrc_*
ui_*
*.qmlc
*.jsc

# Generated proto
*.pb.h
*.pb.cc
*.grpc.pb.h
*.grpc.pb.cc

# Dependencies (if using vcpkg locally)
vcpkg_installed/

# Logs
*.log
```
