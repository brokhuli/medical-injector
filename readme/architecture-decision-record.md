# Architecture Decision Records — Medical Contrast Power Injector

This document explains *why* each major technology and architectural choice was made, along with the tradeoffs considered.

---

## 1. Two-Process Architecture (Backend + Frontend)

**Decision**: Separate the real-time control engine (C++ backend) from the clinical UI (Qt6/QML frontend) as independent OS processes communicating over gRPC.

**Why**:
- **Process isolation for safety** — if the frontend crashes (Qt rendering bug, UI thread hang), the backend continues injecting with the safety monitor still active. A monolithic process would risk a UI crash halting a mid-injection procedure.
- **Independent lifecycle** — the backend can run headless for automated testing, scripted protocols, or embedded deployment. The frontend can be restarted mid-injection without affecting the control loop.
- **Technology decoupling** — the backend has zero Qt dependencies, making it testable with standard C++ tools. The frontend can evolve its UI framework without touching control logic.

**Tradeoffs**:
- **Added complexity** — gRPC requires proto definitions, code generation, serialization overhead, and connection management (reconnection, backoff).
- **Latency** — IPC over gRPC (~0.5-2 ms per RPC) is slower than in-process function calls. Acceptable because the UI only needs 20 Hz updates, not 500 Hz tick data.
- **Deployment** — two executables to manage instead of one. Mitigated by the frontend auto-connecting on startup.

**Alternatives considered**:
- *Shared library (monolithic)*: Simpler deployment but couples UI lifecycle to control engine.
- *Shared memory IPC*: Lower latency but platform-specific, harder to debug, no built-in schema.
- *WebSocket/REST*: More universally accessible but lacks gRPC's streaming, type safety, and code generation.

---

## 2. C++17 for the Backend

**Decision**: Use C++17 as the backend language with no garbage collection, no runtime, and direct OS thread control.

**Why**:
- **Deterministic timing** — the control loop must hit 2 ms ticks consistently. GC pauses (Java/C#: 10-100+ ms) or interpreter overhead (Python) would blow the timing budget.
- **Direct OS primitives** — CPU affinity (`SetThreadAffinityMask` / `pthread_setaffinity_np`), thread priority (`THREAD_PRIORITY_HIGHEST` / `SCHED_FIFO`), and `steady_clock::sleep_until()` are all available natively.
- **Memory control** — lock-free ring buffers, stack-allocated tick data, and zero-allocation hot paths require manual memory management.
- **Ecosystem** — gRPC, protobuf, Google Test, spdlog all have first-class C++ support.

**Why C++17 specifically** (not 20/23):
- `std::filesystem`, `std::optional`, structured bindings, and `if constexpr` cover the needed features.
- C++20 modules and coroutines add build complexity without clear benefit for this codebase.
- Wider compiler support (MSVC 2019+, GCC 7+, Clang 5+).

**Tradeoffs**:
- **Development speed** — C++ is slower to write than Python/Go. Mitigated by keeping the backend focused (~3,000 lines of core logic).
- **Memory safety** — no borrow checker (vs Rust). Mitigated by mutex discipline, RAII, and sanitizer CI (ASan + UBSan).
- **Build times** — longer than Go/Rust. Mitigated by static library pattern (`injector-backend-lib`) shared between executable and tests.

---

## 3. gRPC as the Communication Layer

**Decision**: Use gRPC with Protocol Buffers for all frontend-backend communication.

**Why**:
- **Server-streaming RPCs** — `StreamTelemetry` and `StreamEvents` naturally map to gRPC's server-streaming model. The backend pushes data as it becomes available without polling.
- **Schema-first contract** — `injector.proto` is the single source of truth. Both sides get generated code with type safety, versioning, and backward compatibility.
- **Bidirectional streaming capability** — while not currently used, gRPC supports client-streaming and bidi-streaming if future requirements demand it (e.g., real-time parameter tuning).
- **Language agnostic** — could add a Python test harness, web dashboard, or mobile client without changing the backend.

**Tradeoffs**:
- **Complexity** — proto compilation step, generated code, and gRPC runtime add build and debugging overhead vs raw sockets or REST.
- **Binary protocol** — harder to inspect with standard tools vs JSON/HTTP. Mitigated by `GetState()` unary RPC for debugging and grpcurl for ad-hoc calls.
- **Dependency weight** — gRPC + protobuf + abseil pull in ~50 MB of dependencies via vcpkg.

**Alternatives considered**:
- *ZeroMQ*: Lower latency, but no schema, no streaming semantics, manual serialization.
- *REST/WebSocket*: More tooling (curl, browser), but no code generation, manual streaming.
- *Cap'n Proto*: Zero-copy serialization, but smaller ecosystem and less mature streaming.

---

## 4. Independent Safety Monitor Thread

**Decision**: The safety monitor runs on its own 1 ms thread, reads the HAL directly, and can halt injection without going through the control loop or state machine.

**Why**:
- **Defense in depth** — if the control loop hangs (bug, priority inversion, OS scheduling spike), the safety monitor still runs and can call `hal->emergencyStop()` synchronously.
- **Faster detection** — 1 ms check rate (2x the control loop) catches faults within half a control tick.
- **Minimal code path** — the safety monitor is <100 lines of checking logic. Simpler code = fewer bugs in the most critical path.

**Emergency halt flow**:
```
Safety Monitor detects fault
  → hal->emergencyStop()        // Motor stop + valve close (synchronous)
  → faultQueue->push(fault)     // Async notification to state machine
```
The HAL is already safe *before* the state machine processes the fault event.

**Tradeoffs**:
- **Thread overhead** — an extra OS thread with high priority. Negligible on modern hardware.
- **Potential for false positives** — reading HAL from two threads (control + safety) requires thread-safe HAL. Solved with `std::mutex` for compound state, `std::atomic<bool>` for the air detector.
- **Duplicate HAL reads** — both threads read the same sensors. The overhead is a few microseconds per tick, insignificant vs the safety benefit.

**Timing violation check**: The safety monitor also monitors the *control loop itself* — if the control loop's last tick timestamp is too old (`> tickRate + 30 ms jitter tolerance`), it fires a timing violation fault. This is the only way to detect a hung control loop.

---

## 5. Lock-Free SPSC Ring Buffer for Telemetry

**Decision**: Use a single-producer/single-consumer lock-free ring buffer to pass tick data from the control loop to the logging/gRPC layer.

**Why**:
- **Non-blocking hot path** — the control loop (producer) must never block waiting for a consumer. A mutex-protected queue could cause priority inversion if the gRPC thread holds the lock during serialization.
- **Bounded memory** — 300,000 entries × ~80 bytes = ~24 MB. Fixed allocation, no runtime growth.
- **Overwrite-oldest** — if the consumer falls behind, newest data overwrites oldest. For a real-time system, stale telemetry is less valuable than current telemetry.

**Implementation details**:
- `std::atomic<size_t>` head/tail with `memory_order_acquire`/`release` — minimal synchronization overhead.
- No CAS loops — SPSC guarantees single writer and single reader, so simple load/store suffices.

**Tradeoffs**:
- **Only works for SPSC** — can't add a second consumer without a redesign. Currently fine because `TelemetryBroadcast` fans out to multiple gRPC streams from the single consumer side.
- **Data loss by design** — old ticks are discarded when the buffer wraps. Acceptable because the `DataLogger` also maintains an event log for critical audit data.
- **Fixed capacity** — must be sized at startup. Too small = frequent overwrites; too large = wasted memory.

**Why not lock-free everywhere**: The `CommandQueue` (control commands from UI) uses `std::mutex + std::queue` because commands are rare (human-rate, ~1/sec) and correctness matters more than latency. Lock contention at 1 command/sec is unmeasurable.

**Why in-memory ring buffer instead of OpenTelemetry + external backends**: A production-grade observability stack (OpenTelemetry exporters to Jaeger for traces, Prometheus for metrics, Grafana for dashboards) was rejected in favor of surfacing telemetry directly through the QML interface backed by this ring buffer. In the field, the clinical operator will not have Jaeger/Grafana/Prometheus running alongside the injector — observability has to be self-contained in the product UI. The result is less full-featured (no cross-service traces, no long-horizon metrics, no ad-hoc PromQL) but far more manageable: no collectors, no agents, no external services to provision or keep healthy. Storage stays in the local ring buffer rather than a time-series database — for an educational single-machine tool, a DB is overkill and adds maintenance burden (schema migrations, retention policies, disk management) that the use case doesn't warrant. Full-resolution data is still recoverable via the `ExportData` RPC for post-hoc analysis.

---

## 6. PID Controller with Acceleration Ramp and Pressure-Aware Decel

**Decision**: Discrete PID controller with anti-windup, filtered derivative term, upstream acceleration ramp limiting the target flow rate, and a pressure-aware end-of-phase deceleration scheme.

**Why**:
- **Industry standard** — commercial medical injectors use PID-based flow control. The approach is well-understood and tunable.
- **Acceleration ramp** — rate-limits target flow changes to `maxAcceleration` (default 1.5 mL/s²). Prevents step changes that would cause pressure spikes and jerky motor response.
- **Filtered derivative** — low-pass filter (α = 0.1) suppresses high-frequency noise in the derivative term, preventing oscillation from noisy sensor readings.
- **Anti-windup** — integral term clamped to `±iTermMax` (default 500 RPM). Prevents windup when the motor is saturated at max/min RPM.
- **Predictive end-of-phase decel** — the control loop asks the HAL (`predictDecelVolume`) how much volume would be delivered if the motor began decelerating now. When the remaining phase volume falls below that prediction, the target is latched to zero with a ramp-rate override, so overshoot is bounded by model prediction rather than empirical tuning. Only the final phase decelerates — intermediate phase boundaries hand flow off continuously (see ADR 13).
- **Pressure-aware decel rate** — `pressureAwareDecelRate()` scales the decel rate linearly from `maxAcceleration` up to `maxAcceleration * (1 + decelPressureGain)` as pressure crosses `decelPressureThreshold` (fraction of the active phase's pressure limit). Higher pressure ⇒ earlier trigger (larger predicted stopping volume) and steeper ramp, keeping pressure off the limit without penalizing normal-pressure runs. Defaults: threshold 0.80, gain 2.0; `gain = 0` reproduces the fixed-rate behavior.

**Tradeoffs**:
- **Tuning required** — PID gains (Kp, Ki, Kd) must be tuned for the specific motor/syringe/tubing combination. Mitigated by making all parameters configurable in `config.json`.
- **Linear assumption** — PID assumes a roughly linear system. The simulated HAL uses linear pressure and first-order motor models, which is valid for the simulation but would need retuning for nonlinear real hardware.
- **No feedforward** — a model-predictive or feedforward controller could achieve faster setpoint tracking. PID was chosen for simplicity and transparency.

---

## 7. HAL Abstraction with Simulated Physics

**Decision**: Define a pure virtual `IHalInterface` and implement `SimulatedHal` with physics-based device models (motor, pressure, valve, syringe, air detector).

**Why**:
- **Testability** — unit tests use `MockHal` (gmock), integration tests use `SimulatedHal`. No hardware required to validate control logic.
- **Swappability** — a real hardware driver would implement `IHalInterface` without changing any control, safety, or state machine code.
- **Realistic behavior** — first-order motor lag (50 ms time constant), linear pressure model, and volume integration provide physically plausible responses for educational use.
- **Fault injection** — `injectFault()` / `clearFaults()` allow deterministic testing of fault paths without physical fault conditions.

**Physics models**:
| Model | Approach | Parameters |
|-------|----------|------------|
| Motor | First-order exponential lag | τ = 50 ms, max 1500 RPM, flowPerRpm = 0.01 |
| Pressure | `P_ss = baseline + flow · resistance · multiplier`, then first-order compliance lag | baseline 10 psi, contrastResistance 45, salineResistance 35 psi/(mL/s), τ = 400 ms |
| Valve | Binary open/close, instant switching | — |
| Syringe | Volume integration: V -= flow · dt | Contrast 100 mL, Saline 50 mL |
| Air detector | Atomic boolean, externally triggered | — |

**Why a single motor model (not a pluggable factory)**: An earlier iteration exposed `IMotorModel` with a factory selecting between first- and second-order implementations. The abstraction was removed once the first-order model proved sufficient for the control-validation use case — the factory, interface, and second-order implementation added code-path branching and config surface without improving fidelity enough to justify it. Swappability for real hardware still happens at the `IHalInterface` boundary, which is the level that matters.

**Why the pressure compliance lag**: The original pure-linear pressure model snapped pressure instantly to the steady-state value, which is unrealistic (tubing, syringe barrel, and patient compliance all act as capacitance) and made the pressure trace look like a step function. A first-order lag with a 400 ms time constant better matches the softening of pressure rise/fall seen in real injectors and lets the pressure-aware decel logic (ADR 6) act on a physically plausible signal. The active resistance is passed in per step because it changes when the valve switches between contrast and saline.

**Tradeoffs**:
- **Simplified physics** — real fluid dynamics involve nonlinear pressure-flow relationships, catheter resistance curves, and temperature effects. The linear + first-order-lag model is sufficient for control algorithm validation but doesn't capture all real-world behavior.
- **No electrical simulation** — real motor drivers have current limits, back-EMF, and thermal protection. These are abstracted away.
- **Thread safety overhead** — `std::mutex` on every HAL access adds ~1 µs per call. Acceptable at 2 ms tick rate (<0.1% overhead).

---

## 8. Qt6/QML for the Frontend

**Decision**: Use Qt6 with QML for the clinical UI, connected to the backend via a C++ bridge object.

**Why**:
- **Declarative UI** — QML's declarative syntax is well-suited for data-driven dashboards (gauges, charts, progress bars) that react to property changes.
- **Canvas for charting** — `TimelineChart.qml` uses QML Canvas for dual-Y-axis real-time plotting at 10 Hz without external charting dependencies.
- **Cross-platform** — Qt6 runs on Windows, Linux, and macOS from the same codebase.
- **Property binding** — QML's reactive property system automatically updates UI elements when telemetry changes, eliminating manual UI refresh logic.

**Bridge pattern** (`InjectorBridge`):
- QObject with `Q_PROPERTY` / `Q_INVOKABLE` / `NOTIFY` signals
- All gRPC callbacks marshaled to the main thread via `QMetaObject::invokeMethod(Qt::QueuedConnection)`
- Telemetry coalesced by `QTimer` at 20 Hz to batch property updates and reduce repaints

**Why separate GrpcClientService from QObject**:
- gRPC spawns its own threads for streaming. Qt's thread affinity rules require signal/slot to happen on the object's owning thread.
- Keeping gRPC in a plain C++ class avoids Qt thread affinity conflicts. The bridge marshals callbacks to the Qt main thread explicitly.

**Tradeoffs**:
- **Qt dependency size** — Qt6 adds ~100+ MB to the deployment. The backend deliberately avoids Qt to keep it lightweight.
- **QML performance** — JavaScript-based QML is slower than native C++ widgets for complex layouts. Mitigated by keeping the UI simple (14 components, 10 Hz repaint).
- **Licensing** — Qt6 is LGPL, which requires dynamic linking for proprietary distribution. Acceptable for an educational/research tool.

**Alternatives considered**:
- *Dear ImGui*: Lower overhead, immediate-mode rendering, but poor accessibility and no native look-and-feel.
- *Electron/Web*: Wider developer familiarity, but heavier runtime and no direct gRPC support (would need a REST gateway).
- *WPF/.NET*: Windows-only, would break cross-platform goal.

---

## 9. State Machine Design: Enum + Switch (Intentionally Simple)

**Decision**: Implement the state machine as a simple enum with switch-case transitions rather than a state pattern, state chart library, or event-driven framework.

**6 States**: `Idle → Armed → Injecting → Paused → Completed`, with `Fault` reachable from any state.

**Why**:
- **Auditability** — the entire state machine fits in <200 lines. Every transition is visible in one function. For safety-critical logic, simplicity is a feature.
- **Testability** — `processCommand()` is synchronous and pure (takes command, returns result). Unit tests call it directly without threads, timers, or event loops.
- **No framework dependency** — state chart libraries (Boost.SML, Qt State Machine) add abstraction layers that obscure the transition logic.

**Tradeoffs**:
- **Scalability** — adding states or complex guard conditions would make the switch-case unwieldy. Acceptable because medical injectors have a small, well-defined state space.
- **No hierarchical states** — can't nest substates (e.g., "Injecting.PhaseN"). Phase tracking is handled by a separate `phaseIndex_` field alongside the state enum.
- **No visual modeling** — can't auto-generate state diagrams from code. The state space is small enough to document manually.

---

## 10. Build System & Dependencies

### CMake with vcpkg

**Decision**: CMake 3.22+ for build configuration, vcpkg for C++ dependency management.

**Why**:
- **Cross-platform** — CMake generates native build files for MSVC, GCC, and Clang. vcpkg provides pre-built binaries for Windows and Linux.
- **Conditional frontend** — `if(Qt6_FOUND)` makes the frontend optional. Backend-only builds work without Qt installed.
- **Static library pattern** — `injector-backend-lib` compiles all backend sources once, linked by both the executable and test targets. Halves build time vs compiling sources twice.

**Dependencies** (vcpkg.json):
| Dependency | Why | Alternative Considered |
|------------|-----|----------------------|
| gRPC | IPC with streaming + codegen | ZeroMQ (no schema), REST (no streaming) |
| spdlog | Structured logging, header-only, fast | std::cout (no levels/formatting), log4cxx (heavy) |
| nlohmann-json | Config parsing, header-only, intuitive API | RapidJSON (faster but verbose API), toml (less common) |
| Google Test | Testing framework with gmock | Catch2 (no mock support), doctest (less mature) |

**No Boost**: The project's needs (threading, filesystem, containers) are covered by C++17 standard library. Boost would add significant build time and dependency weight for marginal benefit.

### CI: GitHub Actions

- **Ubuntu Release**: Standard build + all tests
- **Ubuntu Debug + ASan/UBSan**: Catches memory errors and undefined behavior
- **Windows Release**: MSVC build + tests

**Why sanitizers in CI but not locally**: ASan adds 2-3x runtime overhead, unacceptable for real-time timing tests on developer machines. CI runs on dedicated hardware where timing is already non-deterministic.

---

## 11. Telemetry Coalescing Strategy

**Decision**: The control loop produces tick data at 500 Hz (2 ms), but telemetry is coalesced to 20 Hz (50 ms) before sending to the frontend.

**Why**:
- **Bandwidth** — 500 TelemetryFrames/sec × ~200 bytes each = 100 KB/s. At 20 Hz = 4 KB/s. Reduces network overhead by 25x.
- **UI refresh rate** — human perception tops out at ~30 Hz. Sending 500 Hz to QML would waste CPU on property binding updates that the user can't see.
- **Ring buffer design** — the gRPC streaming handler reads the latest frame from the ring buffer at the requested rate, naturally skipping intermediate ticks.

**Tradeoffs**:
- **Data loss for remote debugging** — intermediate ticks are not sent over the wire. The `ExportData` RPC provides full-resolution CSV/JSON export from the ring buffer for post-hoc analysis.
- **Configurable rate** — `StreamConfig.rate_ms` allows 20-1000 ms, so a diagnostic tool could request higher rates if needed.

---

## 12. Configuration via JSON File

**Decision**: Single `config.json` file with 7 sections, loaded at startup with validated defaults.

**Why**:
- **Human-readable** — operators can inspect and modify configuration without specialized tools.
- **Single file** — all 7 sections (server, control, PID, safety, HAL, syringe, logging) in one place. No scattering across environment variables, CLI flags, and config files.
- **Validated defaults** — every field has a sensible default. Running with no config file produces a working system.

**Tradeoffs**:
- **No hot reload** — changing config requires restarting the backend. Acceptable because PID gains and safety limits should not change mid-injection.
- **No schema validation tool** — validation is in C++ code, not a JSON Schema file. Adding JSON Schema would help external tooling but isn't needed for this scope.
- **Flat file** — no database, no distributed config. Appropriate for a single-machine educational tool.

---

## 13. Continuous Flow Across Phase Transitions

**Decision**: Only the *final* phase of a protocol decelerates to zero. Intermediate phase boundaries hand flow off continuously — the target flow rate is held constant through the valve switch from contrast to saline (or vice versa).

**Why**:
- **Clinical realism** — commercial contrast injectors deliver a saline chaser immediately after the contrast bolus at the same flow rate to push the contrast column through the catheter. Ramping down and back up at every boundary would defeat the purpose of the chaser.
- **Avoids a V-shaped dip** — earlier behavior treated every phase as an independent injection, triggering end-of-phase decel and then re-ramping the next phase from zero. The resulting V in the flow trace was an artifact of the control strategy, not a physical necessity.
- **Pressure dip is a model artifact, not a control bug** — after the valve switches, the pressure model's first-order lag reacts to the new fluid's resistance (contrast 45 vs saline 35 psi/(mL/s)), producing a smooth pressure transient. Flow is held commanded-constant through the switch; the visible pressure blip comes from the compliance lag responding to the resistance step, which is physically correct.

**How it's gated**: The end-of-phase decel trigger is guarded by `phaseIndex == totalPhases - 1`, so intermediate phases pass through the PID with the same target on both sides of the transition. Per-phase volume counters are reset on phase change so `PhaseComplete` still fires correctly.

**Tradeoffs**:
- **Slight overshoot at the boundary** — because decel doesn't engage, a small amount of the outgoing fluid may be delivered past the nominal phase volume while the valve switches. Acceptable because phase volumes are set with clinical margin and the total protocol volume is what matters.
- **Can't independently deceleration-tune an intermediate phase** — the flow-hold is unconditional. If a future protocol needed a pause between phases, it would need a distinct "hold" phase with zero flow.

---

## Summary: Design Philosophy

| Principle | Manifestation |
|-----------|--------------|
| **Safety first** | Independent safety thread, emergency halt before state machine notification, defense in depth |
| **Simplicity in critical paths** | <200 line state machine, <100 line safety monitor, enum+switch over frameworks |
| **Process isolation** | Frontend crash ≠ backend crash, gRPC contract as hard boundary |
| **Lock-free where it matters** | SPSC ring buffer on hot path, mutexes on cold path (commands) |
| **Testability by design** | HAL abstraction, synchronous processCommand(), thread-free unit tests |
| **Configure, don't hardcode** | All timing, PID, safety, and HAL parameters in config.json |
| **Observable** | Structured logging, timing stats, event audit trail, data export |
| **Cross-platform** | `std::chrono`, `std::filesystem`, compile-time `#ifdef` for OS primitives |
