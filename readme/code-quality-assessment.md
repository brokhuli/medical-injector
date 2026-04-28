# Code Quality Assessment — Medical Contrast Power Injector

**Rating scale**: 1-10 (1 = poor, 5 = adequate, 7 = good, 9 = excellent, 10 = exceptional)

---

## 1. Performance — 7.5 / 10

The system demonstrates strong performance awareness where it matters most: the real-time control loop.

**Strengths**:
- Lock-free SPSC ring buffer (`RingBuffer.h`) with correct `memory_order_acquire`/`release` semantics — zero contention on the hot path
- CPU core pinning (`ControlLoop.cpp:87-100`) with platform-specific `SetThreadAffinityMask` / `pthread_setaffinity_np`
- `steady_clock` for monotonic microsecond-precision timing — immune to system clock drift
- Atomic relaxed loads for non-critical stats accumulation

**Weaknesses**:
- Division in the hot path for mean tick calculation every 2 ms tick (`tickMsSum / ticks` at `ControlLoop.cpp:291`) — should use exponential moving average
- `nlohmann::json` string building in the event broadcast path allocates on every command cycle
- TickData (~80 bytes) is stack-allocated fresh every tick rather than reused from a pool

**Improvements**:
1. Replace cumulative mean with exponential moving average: `mean = 0.95 * mean + 0.05 * sample`
2. Pre-allocate event JSON buffers or use a fixed-format string builder
3. Profile and consider cache-line alignment for TickData and ring buffer entries

---

## 2. Simplicity — 7.0 / 10

Core algorithms are clean and readable. Complexity creeps in at the edges.

**Strengths**:
- PID controller is 52 lines (`PidController.cpp:10-62`) — clear flow: ramp → error → PID terms → clamp
- State machine uses enum + switch — the entire transition table is visible in one function
- Config loading is straightforward JSON extraction with explicit field access

**Weaknesses**:
- End-of-phase deceleration ramp (`ControlLoop.cpp:159-193`) is 35 lines of quadratic math mixing physics (motor lag tau) with control theory (max acceleration) in one formula
- Phase volume tracking exists in *two places*: `ControlLoop` tracks `phaseVolumeDelivered` locally, `StateMachine` tracks `volumeDeliveredPerPhase_` — two sources of truth
- `processCommand()` is 200+ lines in a single function (`StateMachine.cpp:56-258`)

**Improvements**:
1. Extract deceleration ramp into a named helper with documented physics assumptions
2. Consolidate volume tracking to a single authoritative location
3. Split `processCommand()` into per-command handler methods

---

## 3. Modularity — 7.0 / 10

The 7-layer architecture is well-defined with clean interfaces at the critical boundaries.

**Strengths**:
- `IHalInterface` is a pure virtual contract — control logic is completely decoupled from hardware simulation
- Queue-based communication (`CommandQueue`, `FaultQueue`, `EventBroadcast`) between layers — no direct cross-layer calls
- Frontend has zero backend dependencies beyond the proto definition
- Static library pattern (`injector-backend-lib`) cleanly shared between executable and test targets

**Weaknesses**:
- `ControlLoop` constructor takes 6 optional raw pointers — caller must know which combinations are valid for test vs production mode
- Protocol validation hardcodes acceptable ranges (`flowRate 0.1-10.0`, `volume 1.0-200.0`, `pressureLimit 50-400`) in `StateMachine.cpp:193-205` instead of reading from config
- CMake builds all backend sources into one monolithic library — can't build HAL or state machine independently
- `GrpcServer` manually drains the telemetry broadcast queue instead of calling a "get latest" API

**Improvements**:
1. Use a builder pattern or dependency struct for `ControlLoop` construction — make valid configurations explicit
2. Move protocol validation limits to `Config` so they're tuneable
3. Consider splitting the static library into per-layer targets for faster incremental builds

---

## 4. Predictability (Determinism) — 8.0 / 10

This is a standout area — the system was clearly designed with real-time determinism as a first-class concern.

**Strengths**:
- Dedicated 2 ms control loop with `sleep_until(nextTick)` — no event-driven jitter
- `steady_clock` (monotonic, not wall-clock) used everywhere for timing
- Safety monitor runs at 1 ms on a separate thread — checks continue even if the control loop is delayed
- Timing stats tracked: mean, min, max tick duration, overrun count — deviations are measurable
- Lock-free ring buffer means the control loop never blocks waiting for consumers

**Weaknesses**:
- Windows scheduler granularity (15-20 ms spikes) undermines the 2 ms target — `jitterToleranceMs: 30` reflects this reality rather than solving it
- No `timeBeginPeriod(1)` call on Windows to reduce scheduler quantum
- Overrun detection uses 2x tick rate threshold (`tickMs > config_.tickRateMs * 2.0`) — an undocumented magic number
- No mechanism to detect or recover from a missed tick — overruns are counted but execution continues at the next `sleep_until`

**Improvements**:
1. On Windows, call `timeBeginPeriod(1)` at startup to request 1 ms scheduler resolution (and `timeEndPeriod(1)` on shutdown)
2. Document the 2x overrun threshold rationale — or make it configurable
3. Consider a catch-up strategy for missed ticks (double-step vs skip-and-continue)

---

## 5. Extensibility — 6.5 / 10

The architecture supports hardware swapping and protocol changes, but other extension points are limited.

**Strengths**:
- HAL interface makes adding real hardware a matter of implementing `IHalInterface` — zero changes to control/safety/state code
- Proto-based gRPC contract allows adding new clients (Python, web) without backend changes
- Multi-phase protocol model is inherently extensible (just add phases)
- Config-driven parameters mean many behaviors can be changed without code modification

**Weaknesses**:
- Adding a new state (e.g., "Priming", "SyringeChange") requires modifying the switch-case in `processCommand()`, the enum, and every place that handles state — no state registration mechanism
- Adding a new RPC requires modifying `GrpcServer.cpp`, the proto file, and the frontend — no plugin or handler registration
- Safety checks are hardcoded in `SafetyMonitor::checkOnce()` — no way to add/remove checks at runtime or via config
- No event hooks or extension points for third-party integrations (e.g., external alarm systems)

**Improvements**:
1. Consider a check-registration pattern for SafetyMonitor so new checks can be added without modifying the core loop
2. Add a state transition table (map of `{state, command} → newState`) to make new states declarative rather than procedural
3. Design a handler registration pattern for gRPC RPCs to reduce the cost of adding new endpoints

---

## 6. Reliability — 7.0 / 10

The safety-critical paths are well-designed, but networking and edge cases have gaps.

**Strengths**:
- Safety monitor halts the HAL *synchronously* before notifying the state machine — the system is safe before anyone processes the fault event
- All threads use RAII shutdown: destructors call `stop()`, which sets `running_ = false` and joins
- Fault state is reachable from every other state — no stuck-state scenarios
- Frontend reconnection with exponential backoff (1s → 2s → 4s → 10s max)

**Weaknesses**:
- No exception handling in `ControlLoop::run()` — if `hal_->tick()` throws, the thread dies silently with no log
- `GrpcServer::start()` has a TOCTOU race: two rapid calls could both pass the `if (running_)` check and spawn two threads
- gRPC streaming failures (`writer->Write()` returns false) are handled with a silent `break` — no log explaining why the client disconnected
- Config file not found produces a warning and falls back to defaults — arguably should fail loudly for a safety-relevant system

**Improvements**:
1. Wrap `ControlLoop::run()` and `SafetyMonitor::run()` in try-catch with `spdlog::critical` and graceful shutdown
2. Use `std::atomic::compare_exchange_strong` for `running_` flag to prevent double-start
3. Log gRPC stream disconnection reasons with `status.error_message()`

---

## 7. Robustness — 6.5 / 10

Thread lifecycle is handled correctly, but concurrency edge cases exist.

**Strengths**:
- All thread loops check `running_.load(memory_order_acquire)` — clean shutdown semantics
- Ring buffer handles producer lapping consumer by overwriting oldest — no corruption
- gRPC streaming contexts are properly cancelled on disconnect, unblocking `Read()` calls
- Atomic memory ordering is mostly correct across the codebase

**Weaknesses**:
- **Race condition in GrpcServer**: Double-checked locking on `running_` uses relaxed load (`GrpcServer.cpp:345-346`) — concurrent `start()` calls could spawn multiple threads
- **Protocol access race**: After loading a protocol into `loadedProtocol_`, `GetState()` on the gRPC thread could read it while the state machine is resizing the phases vector during a transition
- **No timeout on frontend streaming reads**: `reader->Read()` blocks indefinitely (`GrpcClientService.cpp:118-124`). If the server hangs without closing the stream, the client thread hangs
- **Potential deadlock**: If `processCommand()` calls `emitEvent()`, which calls `events_->publish()`, and a subscriber callback tries to read `currentState()` (which locks `stateMutex_`), deadlock occurs

**Improvements**:
1. Use `compare_exchange_strong` for all `running_` flag checks
2. Protect `loadedProtocol_` with `protocolMutex` in `GetState()` or use a snapshot copy
3. Set deadlines on streaming contexts: `context.set_deadline(now + 30s)`
4. Ensure event publish callbacks never re-enter the state machine lock (document this invariant)

---

## 8. Scalability — 5.5 / 10

Designed for a single-machine, single-injector scenario — and that's appropriate for its purpose.

**Strengths**:
- Ring buffer is bounded (300K entries, ~24 MB) — memory usage doesn't grow with runtime
- Event log is bounded at `maxEvents` (10,000) — old events are dropped
- Telemetry coalescing (500 Hz → 20 Hz) reduces bandwidth by 25x
- gRPC can handle multiple concurrent streaming clients

**Weaknesses**:
- Single-process backend assumes one injector — no mechanism for managing multiple injectors from one backend
- Ring buffer is SPSC — can't add a second consumer without a fan-out layer (TelemetryBroadcast partially addresses this)
- No connection pooling or load balancing — single gRPC channel
- Event log uses `std::vector` with `push_back` — occasional reallocation pauses when capacity doubles
- No horizontal scaling story (not needed for this use case, but limits future multi-room scenarios)

**Improvements**:
1. Pre-allocate event log vector to `maxEvents` capacity at startup to avoid mid-injection reallocation
2. If multi-injector support is ever needed, consider a registry pattern with per-injector backend instances behind a gateway
3. Use a circular buffer for event log (like the tick ring buffer) instead of a growing vector

---

## 9. Testability — 8.5 / 10

One of the strongest aspects of the codebase. Clearly designed with testing in mind from the start.

**Strengths**:
- `StateMachine::processCommand()` is synchronous and pure — unit tests call it directly without threads, timers, or event loops
- `MockHal` (gmock) allows complete isolation of control logic from hardware simulation
- `TestProtocols.h` provides reusable protocol fixtures (`smallVolumeFast()`, `twoPhaseStandard()`)
- Three test suites with clear scopes: unit (fast, no threads), integration (real threads + timing), gRPC (full stack)
- `PidController::compute()` is a pure function — no I/O, no state machine, fully deterministic
- Integration fixtures wire all components with helper methods (`loadProtocol`, `waitForState`)

**Weaknesses**:
- No unit tests for gRPC message mapping logic (enum conversions, parameter validation in `GrpcServer.cpp:15-91`)
- No stress tests for ring buffer under extreme skew (slow consumer, fast producer sustained)
- Timing-based assertions (`meanTickMs < 10.0`) are fragile on slow CI machines
- No test for graceful shutdown under load (stopping ControlLoop mid-PID-computation)
- No fault-during-transition tests (e.g., air detection fires while state machine is processing a PAUSE command)

**Improvements**:
1. Add unit tests for gRPC enum mapping and parameter validation — these are pure functions, easy to test
2. Replace timing-threshold assertions with relative assertions (e.g., "overruns < 5% of ticks")
3. Add a chaos test: randomly inject faults during state transitions to validate concurrent fault handling

---

## 10. Maintainability — 7.0 / 10

The code is generally readable, but some patterns increase the cost of change.

**Strengths**:
- Consistent naming: `PascalCase` types, `camelCase` functions, `trailing_` for private members
- Headers are well-organized with clear include hierarchies
- Static library pattern means adding a new source file only requires editing one CMakeLists.txt
- Test fixtures and helpers reduce test maintenance burden

**Weaknesses**:
- `processCommand()` at 200+ lines is the most-changed function (state transitions, validation, event emission) — high merge conflict risk
- Duplicate enum-to-string conversions in `InjectorBridge.cpp` (3 functions, lines 9-38) and `GrpcServer.cpp` — must be updated in parallel
- Raw pointers without ownership documentation (`ControlLoop.h:95-98`, `StateMachine.h:81-85`) — newcomers must trace lifetimes manually
- Magic numbers in validation (`0.1`, `10.0`, `1.0`, `200.0`, `50.0`, `400.0`) require code changes for what should be configuration

**Improvements**:
1. Split `processCommand()` into per-command handlers: `handleArm()`, `handleStart()`, etc.
2. Centralize enum conversions in a shared `EnumMappings.h` used by both frontend and backend
3. Add `// Owned by caller; must outlive this object` comments to all raw pointer members
4. Extract validation constants to config

---

## 11. Observability — 7.0 / 10

Operator-facing observability is strong; log-based diagnosis (when the UI isn't attached) remains thin.

**Strengths**:
- spdlog integrated across the codebase (25 log points in 6 files): state transitions, config loading, connection lifecycle, gRPC server startup and stream connect/disconnect
- Full telemetry stream (flow, pressure, RPM, valves, volumes, elapsed time) plus tick-health (mean/max tick, overrun count) computed in the control loop and pushed to the frontend at the client's requested rate
- `LoopHealthBar` surfaces tick-health live in the UI — operators see scheduling pressure without tailing logs
- `EventLog` component renders the full event history with timestamps, types, and JSON details, driven by the event-broadcast system
- `FaultReportDialog` captures a telemetry snapshot at fault-event arrival (before the motor-halt propagates into subsequent telemetry frames) and presents it alongside control-loop health and a pre-fault telemetry trace — diagnostic context is preserved for the operator after the system has already stopped
- `DataLogger` provides CSV/JSON export of the full tick ring buffer and event log for offline analysis

**Weaknesses (backend logs in isolation)**:
- **ControlLoop has zero log points**: no periodic timing summary, no entry/exit log, no overrun threshold warning — if the gRPC client is disconnected, the real-time thread is silent
- **gRPC handler errors are not logged**: `SendCommand`, `LoadProtocol`, `InjectFault`, `GetState`, `ExportData` return failure statuses but don't log the rejected input or failure reason — only server startup failure and stream lifecycle are logged
- **No queue depth metrics**: `CommandQueue` and `FaultQueue` don't expose size — impossible to detect backpressure from logs or metrics
- **No control-loop heartbeat**: if the loop hangs, only the safety monitor's timing-violation fault surfaces the problem; there's no watchdog log line to correlate against
- **Event details are opaque JSON strings**: backend logs carry the struct; grep-for-a-fault-type requires parsing

**Improvements**:
1. Add periodic control-loop timing summary log (every 10 s): `spdlog::info("Control loop: mean={:.2f}ms max={:.2f}ms overruns={}", ...)`
2. Log gRPC handler failures with context: `spdlog::warn("SendCommand rejected: state={} cmd={}", ...)`
3. Expose queue depths via a metrics snapshot on `GetState` and log a warning when a queue exceeds 80 % of capacity
4. Emit a structured fault-type field alongside the JSON blob so log lines are greppable without a JSON parser

**Why 7.0 and not higher**: the UI-side story is genuinely good, but the system is intended to degrade gracefully when the frontend is disconnected, and in that mode you are left with a nearly silent control loop. Closing that gap is what separates a 7 from an 8 here.

---

## 12. Portability — 8.5 / 10

Strong cross-platform story with proper abstractions.

**Strengths**:
- Platform-specific threading (affinity, priority) cleanly abstracted behind `#ifdef _WIN32` / POSIX blocks in exactly two files
- `std::chrono::steady_clock` used everywhere — no platform-specific timer APIs
- All dependencies (gRPC, spdlog, nlohmann-json, gtest) are fully portable via vcpkg
- `std::filesystem` (C++17) for path handling
- CI runs both Ubuntu and Windows builds

**Weaknesses**:
- No macOS support in `#ifdef` blocks — compiles under the `#else` (POSIX) path but `pthread_setaffinity_np` doesn't exist on macOS
- No CMake platform validation — unsupported platforms fail at link time with cryptic errors instead of a clear message
- Hardcoded `"0.0.0.0"` bind address works but isn't configurable for IPv6 or specific interfaces
- Frontend requires Qt6 6.5+ — limits deployment to platforms with Qt packages available

**Improvements**:
1. Add `#elif defined(__APPLE__)` with macOS-appropriate thread affinity (or a graceful no-op with warning)
2. Add CMake platform check: `if(NOT WIN32 AND NOT UNIX) message(FATAL_ERROR "Unsupported platform")`
3. Make bind address configurable in `config.json` server section

---

## Overall Assessment

| Metric | Score | Grade |
|--------|-------|-------|
| Performance | 7.5 | B+ |
| Simplicity | 7.0 | B |
| Modularity | 7.0 | B |
| Predictability (Determinism) | 8.0 | A- |
| Extensibility | 6.5 | B- |
| Reliability | 7.0 | B |
| Robustness | 6.5 | B- |
| Scalability | 5.5 | C+ |
| Testability | 8.5 | A |
| Maintainability | 7.0 | B |
| Observability | 7.0 | B |
| Portability | 8.5 | A |

### Overall Score: 7.2 / 10 (B)

### Summary

This is a **well-architected educational/research system** that gets the hard things right: real-time determinism, safety independence, HAL abstraction, and testability. The 7-layer backend design is sound, and the decision to keep safety-critical paths simple (enum+switch state machine, <100-line safety monitor) shows mature engineering judgment.

**Top 3 strengths**:
1. **Testability** (8.5) — Synchronous state machine, mock HAL, pure PID function, three-tier test suites. Testing was clearly a design constraint, not an afterthought.
2. **Portability** (8.5) — Clean platform abstraction, portable dependencies, CI on both OSes. The system compiles and runs on Windows and Linux with minimal platform-specific code.
3. **Determinism** (8.0) — Dedicated real-time threads with core pinning, monotonic clocks, lock-free hot path, independent safety monitoring. The timing architecture is production-grade.

**Top 3 areas for improvement**:
1. **Robustness** (6.5) — Race conditions in `GrpcServer::start()`, unprotected protocol access during `GetState()`, no timeouts on frontend streaming reads, potential deadlock in event callbacks. These are the kind of bugs that only manifest under load or during fault recovery.
2. **Observability** (7.0) — The UI now provides strong live diagnostics (tick-health bar, fault report dialog with snapshot-at-fault), but the *backend in isolation* is still quiet: ControlLoop has no log points, gRPC handler errors are not logged, and queue depths aren't exposed. Diagnosing a disconnected-client incident from logs alone would be difficult.
3. **Scalability** (5.5) — Appropriate for its single-injector purpose, but the SPSC ring buffer, growing event vector, and single-process model would need rethinking for multi-injector scenarios.

**The system is stronger than the score suggests** for its intended purpose. Many of the lower scores (scalability, extensibility) reflect limitations that are *correct design decisions* for an educational/research simulator — the code doesn't over-engineer for hypothetical multi-injector deployment. The areas that genuinely need attention are robustness (concurrency edge cases) and observability (specifically, backend-log gaps when the frontend isn't attached — the operator-facing story is already solid).
