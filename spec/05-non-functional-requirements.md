# Non-Functional Requirements

This document defines the quality attributes the system must meet. These are testable constraints that shape architecture decisions — not aspirational goals. Where an NFR conflicts with a decision in the architecture specs (03a/03b/03c), update the architecture.

---

## 1. Performance

### 1.1 Control Loop Timing

| Metric | Requirement | How to Verify |
|--------|-------------|---------------|
| Tick interval | 2 ms nominal (configurable 1-10 ms) | Measure 10,000 consecutive ticks with `steady_clock` |
| Mean tick interval | 2.0 ms +/- 0.1 ms | Statistical analysis of tick log |
| Maximum single-tick deviation | < 5 ms (2.5x nominal) | Worst-case from tick log |
| Jitter (standard deviation) | < 0.5 ms | Computed from tick log |
| Tick computation time | < 500 us (25% of 2 ms budget) | Profile `tick()` + PID + HAL reads/writes |
| Sustained operation | Timing requirements hold for >= 1 hour continuous injection | Long-running test |

**Why these numbers:** The 2 ms tick is the fundamental heartbeat. If the control loop drifts, the PID controller loses accuracy, volume tracking accumulates error, and the safety monitor's timing violation check fires. The 5 ms hard ceiling gives 3 ms of headroom before the safety monitor triggers a fault (configured at `jitterToleranceMs: 3.0`, so 2 + 3 = 5 ms absolute).

**Platform expectations:**
- Linux with `isolcpus` + `SCHED_FIFO`: should achieve < 0.1 ms jitter consistently
- Linux without tuning: should achieve < 0.3 ms jitter under normal load
- Windows (best effort): may see occasional spikes to 3-4 ms under load; the safety monitor treats > 5 ms as a fault during injection

### 1.2 Safety Monitor Timing

| Metric | Requirement | How to Verify |
|--------|-------------|---------------|
| Check interval | 1 ms nominal | Measure consecutive check timestamps |
| Fault-to-halt latency | < 10 ms from condition onset to motor stop | Inject fault, measure time to `actualRpm == 0` |
| Emergency stop latency | < 5 ms from `emergencyStop()` call to motor + valves off | Timestamp before call, timestamp after HAL state confirms stop |

**Why 10 ms fault-to-halt:** Real medical injector systems achieve sub-millisecond response on dedicated hardware. Our 10 ms budget accounts for: detection delay (up to 1 ms between checks) + mutex acquisition (< 10 us) + HAL write (< 10 us) + motor model instant stop. The 10 ms target is generous — actual performance should be 1-2 ms.

### 1.3 PID Control Accuracy

| Metric | Requirement | How to Verify |
|--------|-------------|---------------|
| Steady-state flow rate error | < 2% of target after settling | Run 4.0 mL/s for 5s, measure mean actual |
| Settling time | < 1 second to within 5% of target | Step response test |
| Overshoot | < 10% of step size, max 2 overshoot cycles | Step response test |
| Volume accuracy | Delivered within +/- 2% of programmed per phase | Run full protocol, compare delivered vs. programmed |
| Acceleration limit | Ramp rate never exceeds configured `maxAcceleration` | Log actual flow rate, compute derivative |

### 1.4 gRPC Communication

| Metric | Requirement | How to Verify |
|--------|-------------|---------------|
| Command round-trip | < 100 ms from send to state-change event received | Timestamp at frontend send, timestamp at event receipt |
| Telemetry latency | < 200 ms from control loop snapshot to frontend receipt | Compare telemetry timestamp with frontend wall clock (approximate) |
| Telemetry throughput | 20 frames/sec sustained (at default 50 ms / 20 Hz rate) | Count frames over 60 seconds |
| Event delivery | < 50 ms from event emission to frontend receipt | Measure on localhost |
| Connection establishment | < 2 seconds from frontend start to `connected` state | Time from process launch to first successful `GetState` |
| Reconnection | < 15 seconds from disconnect to re-established streams | Kill and restart backend, measure frontend recovery |

### 1.5 UI Responsiveness

| Metric | Requirement | How to Verify |
|--------|-------------|---------------|
| Dashboard update latency | < 500 ms from backend state change to screen pixel update | End-to-end measurement including gRPC + QML render |
| Frame rate | >= 30 fps during injection (QML render) | Qt frame timing diagnostics |
| Input latency | < 100 ms from button click to visual feedback (button state change) | Manual testing with UI instrumentation |
| Chart rendering | Timeline chart repaints at >= 10 Hz without dropped frames | Timer interval measurement |
| Idle CPU (frontend) | < 5% CPU when system is idle and connected | Task manager / `top` observation |

---

## 2. Scalability

### 2.1 Explicit Non-Goals

This is a single-user desktop application simulating one injector. The following are explicitly out of scope:

- **Multiple simultaneous injectors** — One backend process, one injector instance
- **Multiple simultaneous frontends** — The gRPC server accepts multiple connections (gRPC's default), but the system is designed and tested for one frontend client
- **Horizontal scaling** — No load balancing, no clustering, no distributed state
- **Cloud deployment** — Runs on the developer's local machine
- **High-throughput data ingestion** — The ring buffer holds 10 minutes of tick data; this is sufficient

### 2.2 Actual Scaling Dimensions

| Dimension | Supported Range | Limiting Factor |
|-----------|----------------|-----------------|
| Protocol phases | 1-20 | Validation limit; state machine iterates linearly |
| Injection duration | Up to 10 minutes | Ring buffer size (see `logging.ringBufferSize` default in **04-schema-and-contracts.md, Section 5**) |
| Telemetry history (frontend) | Up to 5 minutes (6,000 frames at 20 Hz) | QVariantList memory in InjectorBridge |
| Event log | Up to 10,000 events | Vector in memory; bounded at config `maxEvents` |
| Export file size | Up to ~50 MB (300K ticks as CSV) | String returned via gRPC; could be slow for max-size exports |

---

## 3. Security

### 3.1 Threat Model

This is a **local desktop application for education and research**. It is not a networked service, not internet-facing, and not a real medical device. The security model reflects this scope.

**In scope:**
- Preventing accidental misconfiguration that could crash the application
- Input validation at all boundaries (gRPC, config file, QML inputs)
- No execution of user-supplied code or scripts

**Out of scope (not applicable):**
- Authentication / authorization (single-user, localhost)
- Encryption (gRPC on localhost, no sensitive data)
- Audit logging for compliance
- Protection against a malicious local user (they have full filesystem access anyway)

### 3.2 Input Validation

All external inputs are validated before processing:

| Input Boundary | Validation |
|---------------|------------|
| gRPC commands | Enum range check; unknown commands rejected |
| Protocol parameters | Range checks per field (see 04-schema-and-contracts.md Section 6) |
| Fault injection parameters | Type-specific validation; invalid combinations rejected |
| Config file (JSON) | Schema validation at startup; malformed config fails with clear error, falls back to defaults |
| QML user inputs | Client-side range validation; backend re-validates authoritatively |
| Export format | Whitelist: "csv" or "json" only |

### 3.3 Process Isolation

| Guarantee | Mechanism |
|-----------|-----------|
| Frontend crash does not affect backend | Separate OS processes communicating via gRPC |
| Backend crash does not corrupt frontend | Frontend detects disconnect, shows status, reconnects |
| No shared memory between processes | gRPC serializes all data across the process boundary |
| Safety monitor independence | Separate thread, reads HAL directly, can halt without state machine cooperation |

---

## 4. Reliability

### 4.1 Failure Handling

| Failure Scenario | Required Behavior | Recovery |
|-----------------|-------------------|----------|
| **Overpressure** | Motor stop + valve close within 10 ms | Acknowledge + Reset to Idle |
| **Air detected** | Motor stop + valve close within 10 ms | Acknowledge + Reset to Idle |
| **Motor stall** | Fault after sustained divergence (25 ms default) | Acknowledge + Reset to Idle |
| **Control loop timing violation** | Fault if tick exceeds deadline during injection | Acknowledge + Reset to Idle |
| **Frontend disconnect** | Backend continues running; injection continues if active; no automatic stop on disconnect (the safety monitor handles real faults) | Frontend reconnects, calls GetState, resumes display |
| **Frontend crash** | Same as disconnect — backend is unaffected | Restart frontend; auto-reconnects |
| **Backend crash** | Frontend shows "Disconnected"; no injection data recovery | Restart backend; frontend reconnects to fresh Idle state |
| **Config file missing** | Start with all defaults; log warning | Provide config file |
| **Config file malformed** | Reject startup with clear parse error | Fix config file |
| **Config values out of range** | Reject startup with field-level error message | Fix config values |
| **gRPC port in use** | Fail startup with "port already in use" error | Change port or stop conflicting process |
| **Ring buffer full** | Overwrite oldest entries; no blocking | Acceptable data loss; export captures most recent data |
| **Event log full** | Overwrite oldest entries | Acceptable; recent events are more relevant |
| **Telemetry consumer slow** | Drop oldest undelivered frames; never block control loop | Frontend may see gaps in timeline chart |

### 4.2 Safe States

The system has a clearly defined safe state that all fault paths converge to:

**Safe state definition:**
- Motor RPM: 0 (commanded and actual)
- All valves: closed
- State machine: `Fault`
- Safety monitor: still running, still checking

**Transitions to safe state:**
- Any safety fault detection
- `EMERGENCY_STOP` command
- The safe state is reachable from every non-Idle state

**Leaving safe state:**
- Only via `RESET` command, which transitions to `Idle`
- Syringe volumes reflect partial consumption (not reset to full)
- Faults are cleared; protocol remains loaded

### 4.3 Data Integrity

| Concern | Guarantee |
|---------|-----------|
| Volume tracking | Integrated from actual flow rate every tick; cumulative error < 2% over full protocol |
| Tick data logging | Non-blocking writes; may lose oldest data but never corrupts buffer |
| Event ordering | Monotonically increasing timestamps from `steady_clock` |
| State consistency | State machine is the single authority; no split-brain between threads |
| Protocol immutability | Loaded protocol cannot be modified while not in Idle state |

---

## 5. Observability

### 5.1 Logging

**Framework:** `spdlog` with structured output.

| Log Level | What Gets Logged | Expected Volume |
|-----------|-----------------|-----------------|
| `error` | Faults, failed commands, startup failures, unexpected exceptions | Rare (< 1/sec during faults) |
| `warn` | Timing jitter approaching threshold, telemetry consumer lag, config fallback to defaults | Occasional |
| `info` | State transitions, protocol loaded, injection start/complete, connection events, startup/shutdown | Low (< 10/sec during injection) |
| `debug` | Command processing, phase transitions, fault injection, safety check details | Moderate |
| `trace` | Per-tick control loop values (only enable for debugging — extremely high volume) | 500/sec at 2 ms tick |

**Log format:**
```
[2026-03-29 14:22:01.234] [info] [state_machine] Transition: Idle -> Armed (trigger: ARM)
[2026-03-29 14:22:03.456] [info] [control_loop] Injection started: 2 phases, 110 mL total
[2026-03-29 14:22:18.789] [error] [safety_monitor] FAULT: Overpressure 331.2 psi (limit: 325.0), halt latency: 4.2 ms
```

**Log destination:** `stdout` (console) by default. File output configurable but not required for MVP.

### 5.2 Telemetry (Real-Time Metrics)

Exposed via the `StreamTelemetry` gRPC stream and visible in the frontend dashboard:

| Metric | Update Rate | Purpose |
|--------|-------------|---------|
| Actual flow rate | 20 Hz | Primary injection monitoring |
| Pressure | 20 Hz | Safety monitoring, dashboard display |
| Motor RPM (commanded vs. actual) | 20 Hz | Control loop health, motor fault pre-indication |
| Volume delivered (per phase, total) | 20 Hz | Progress tracking |
| Syringe remaining | 20 Hz | Consumable tracking |
| State machine state | On change | UI state display |
| Phase index | On change | Protocol progress |
| Elapsed time | 20 Hz | Injection timing |

### 5.3 Timing Diagnostics

Available via the data logger and tick-level export:

| Metric | Source | How to Access |
|--------|--------|---------------|
| Tick interval histogram | Tick timestamps in ring buffer | Export + offline analysis |
| Worst-case tick time | Max delta in tick log | Export + offline analysis |
| Mean/stddev tick interval | Computed from tick log | Telemetry jitter stats (AC-2.2) |
| Safety check latency | Fault event timestamps vs. condition onset | Event log |
| Command processing time | Event log timestamps | Event log |

### 5.4 Frontend Diagnostics

| Metric | Mechanism | Purpose |
|--------|-----------|---------|
| Connection status | `connectionStatus` Q_PROPERTY | User-visible indicator |
| Telemetry frame count | Counter in InjectorBridge | Detect dropped frames |
| gRPC stream health | Reconnection events logged | Debug connectivity issues |

---

## 6. Portability

### 6.1 Platform Support

| Platform | Backend | Frontend | Priority |
|----------|---------|----------|----------|
| Windows 11 (x64) | Full support | Full support | Primary (developer's machine) |
| Linux (x64, Ubuntu 22.04+) | Full support + real-time tuning | Full support | Secondary (better RT performance) |
| macOS (arm64) | Compiles and runs, no RT tuning | Compiles and runs | Best-effort, not tested regularly |

### 6.2 Compiler Support

| Compiler | Minimum Version | Standard |
|----------|----------------|----------|
| GCC | 10+ | C++17 (C++20 preferred) |
| Clang | 13+ | C++17 (C++20 preferred) |
| MSVC | 2019 (19.29+) | C++17 |

### 6.3 Platform Abstraction

| Concern | Abstraction |
|---------|-------------|
| Thread pinning | `pthread_setaffinity_np()` on Linux, `SetThreadAffinityMask()` on Windows; compile-time `#ifdef` in one location |
| Thread priority | `SCHED_FIFO` on Linux, `SetThreadPriority()` on Windows; same pattern |
| High-resolution timer | `std::chrono::steady_clock` everywhere (cross-platform) |
| Sleep precision | `std::this_thread::sleep_until()` with `steady_clock`; precision varies by OS |
| Filesystem paths | `std::filesystem` (C++17); forward slashes work on both platforms |

### 6.4 Build System

- CMake 3.22+ as the single build system
- `find_package()` for all dependencies (gRPC, protobuf, Qt6, spdlog, nlohmann_json)
- vcpkg or system packages for dependency management — no vendored source trees
- Single `CMakeLists.txt` at root with `add_subdirectory(backend)` and `add_subdirectory(frontend)`
- Out-of-source builds only

---

## 7. Maintainability

### 7.1 Code Organization

| Principle | Enforcement |
|-----------|-------------|
| Backend and frontend are independent CMake targets | Cannot accidentally include frontend headers in backend or vice versa |
| HAL is an abstract interface | Control loop and safety monitor depend on `IHalInterface`, not `SimulatedHal` |
| gRPC contract is a shared `.proto` file | Both sides generate code from the same source |
| No circular dependencies between threads | Command queue, telemetry broadcast, and event broadcast are one-directional |
| State machine is the single source of truth for state | Other threads read state but only the state machine transitions it |

### 7.2 Code Quality

| Tool | Purpose | When |
|------|---------|------|
| `clang-tidy` | Static analysis, style enforcement | Pre-commit or CI |
| `-fsanitize=thread` (TSan) | Detect data races | Debug builds, CI |
| `-fsanitize=address` (ASan) | Detect memory errors | Debug builds, CI |
| `-fsanitize=undefined` (UBSan) | Detect undefined behavior | Debug builds, CI |
| `-Wall -Wextra -Werror` | Compiler warnings as errors | All builds |

### 7.3 Complexity Budget

| Component | Target Complexity | Rationale |
|-----------|------------------|-----------|
| State machine | < 200 lines | Enum + switch. No framework. Complexity = bugs in safety-critical path. |
| Safety monitor | < 100 lines | Intentionally simple. Each check is a single `if`. |
| PID controller | < 50 lines | Standard discrete PID. No exotic variants for MVP. |
| HAL interface | < 20 methods | Clean boundary. Each method does one thing. |
| InjectorBridge | < 500 lines | Translates between QML types and gRPC types. No business logic. |

---

## 8. Testability

The architecture is designed for testability: abstract interfaces (`IHalInterface`) enable mock-based testing, pure-logic components (state machine, PID, safety checks) can be unit tested without threading, and the gRPC boundary enables integration testing without a UI.

For the full test strategy — including mock design, test fixtures, deterministic testing approach, and acceptance criteria mapping — see **07-test-strategy.md**.

---

## 9. NFR Verification Summary

Every NFR must be verifiable. This table maps each requirement to its verification method.

| NFR | Section | Verification Method |
|-----|---------|-------------------|
| Control loop tick rate | 1.1 | Automated: tick log statistical analysis (unit test) |
| Control loop jitter | 1.1 | Automated: long-running timing test |
| Fault-to-halt latency | 1.2 | Automated: inject fault, measure halt time (integration test) |
| PID steady-state accuracy | 1.3 | Automated: run injection, check mean flow rate (unit test) |
| Volume accuracy | 1.3 | Automated: run full protocol, compare volumes (integration test) |
| Command round-trip | 1.4 | Automated: timestamp-based gRPC test |
| Telemetry throughput | 1.4 | Automated: count frames over time (integration test) |
| UI update latency | 1.5 | Manual: visual inspection with instrumented timestamps |
| Frontend frame rate | 1.5 | Manual: Qt diagnostics overlay |
| Safe state convergence | 4.2 | Automated: trigger every fault type, verify motor=0 + valves closed (integration test) |
| State machine correctness | 4.3 | Automated: exhaustive transition testing (unit test) |
| Cross-platform build | 6.1 | CI: build on Windows + Linux |
| Sanitizer clean | 7.2 | CI: TSan + ASan + UBSan pass with zero findings |
| Deterministic test suite | 8.3 | CI: all tests pass reliably across 10 consecutive runs |
