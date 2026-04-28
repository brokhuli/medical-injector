# Test Strategy

This document defines what gets tested, how, and with what infrastructure. Tests are written before or alongside implementation — not bolted on after. The acceptance criteria from the functional spec (02) map directly to test cases here.

---

## 1. Unit Test Strategy

Unit tests cover individual components in isolation. They run single-threaded, use no real I/O, and complete in milliseconds. Every component below the gRPC boundary gets unit tests.

### Tech Decision: Test Framework — Google Test + Google Mock

**Chosen:** Google Test (gtest) + Google Mock (gmock) for all C++ tests.

**Why gtest + gmock:**
- **CMake integration** — `find_package(GTest)` works out of the box. `add_test()` ties into CTest. No custom test runner or build system glue needed.
- **gmock is bundled** — The `MockHal` class needs `MOCK_METHOD` for scripting sensor returns and verifying actuator calls. gmock ships with gtest — no separate mocking library to manage.
- **Already in the dependency ecosystem** — gRPC itself uses gtest for its own tests. The toolchain (CMake + vcpkg + gRPC + protobuf) already knows how to find and build gtest.
- **Cross-platform** — Works on MSVC, GCC, and Clang without quirks. Windows is the primary dev machine, Linux is secondary — both must work.
- **Industry standard for C++** — Extensive documentation, familiar to contributors, strong IDE support (test explorer in VS Code, CLion).

**Alternatives considered:**

| Alternative | Why not |
|-------------|---------|
| Catch2 | Simpler API, header-only, but no built-in mocking framework. Would need a separate mock library (e.g., FakeIt, Trompeloeil) adding another dependency. |
| Boost.Test | Heavy dependency (pulls in Boost). No built-in mocking. Boost is not otherwise used in this project. |
| doctest | Lightweight, fast compilation, but less mature mocking story. Smaller community than gtest. |
| CTest alone (no framework) | CTest is the runner, not the assertion library. Still need gtest or equivalent for `EXPECT_EQ`, `EXPECT_NEAR`, test fixtures, etc. |

**Known risks:**
- gtest macros can slow compilation in large test files. Mitigated by: one test file per component, not monolithic test suites.
- gmock has a learning curve for complex matchers. Mitigated by: keeping mocks simple (MockHal is the only mock for MVP).

### What Gets Unit Tested

| Component | Test File | What's Tested | What's Mocked |
|-----------|-----------|--------------|---------------|
| MotorModel | `test_motor_model.cpp` | First-order lag response, RPM clamping, emergency stop bypass, tick-by-tick convergence | Nothing (pure math) |
| PressureModel | `test_pressure_model.cpp` | Linear pressure computation, baseline offset, zero-flow case | Nothing (pure math) |
| ValveModel | `test_valve_model.cpp` | Open/close state transitions, `closeAll()`, initial state | Nothing (pure state) |
| SyringeModel | `test_syringe_model.cpp` | Volume drain per tick, floor at 0 mL, reset to full, both barrels independent | Nothing (pure math) |
| AirDetectorModel | `test_air_detector.cpp` | Default false, set true, atomic read safety | Nothing (trivial) |
| PidController | `test_pid_controller.cpp` | Step response, steady-state error, anti-windup, output clamping, acceleration ramp, zero-error stability | Nothing (pure math) |
| StateMachine | `test_state_machine.cpp` | All valid transitions, all invalid transitions rejected, multi-phase progression, command validation, protocol loading/rejection | Nothing (pure logic, no threads) |
| SafetyMonitor (logic) | `test_safety_monitor.cpp` | Overpressure detection, air detection, motor divergence counting, timing violation detection, simultaneous faults, fault-during-pause | `MockHal` (returns scripted sensor values) |
| RingBuffer | `test_ring_buffer.cpp` | Push/pop, overwrite-oldest, empty reads, full buffer, size tracking | Nothing (pure data structure) |
| Config | `test_config.cpp` | JSON parsing, default values, missing fields, out-of-range rejection, CLI overrides | Nothing (file I/O with test fixtures) |
| DataLogger | `test_data_logger.cpp` | Tick data recording, event recording, CSV export format, JSON export format, ring buffer overflow | Nothing (pure logic) |
| CommandQueue | `test_command_queue.cpp` | Enqueue/dequeue, thread-safe concurrent access, empty dequeue returns nothing | Nothing (data structure) |

### What Does NOT Get Unit Tested

- **QML components** — Tested via manual inspection and end-to-end flows. QML testing frameworks exist but add complexity disproportionate to the value for MVP.
- **gRPC generated code** — Google's responsibility, not ours.
- **`main.cpp` (both processes)** — Thin wiring code; covered by integration tests.
- **Thread scheduling/priority** — OS behavior, not testable in unit tests. Verified via timing analysis in integration tests.

### MockHal

The `MockHal` class implements `IHalInterface` with gmock, allowing unit tests to script sensor readings and verify actuator commands.

```cpp
// backend/tests/mocks/MockHal.h
#pragma once
#include "hal/IHalInterface.h"
#include <gmock/gmock.h>

namespace injector::testing {

class MockHal : public hal::IHalInterface {
public:
    MOCK_METHOD(double, readPressure, (), (const, override));
    MOCK_METHOD(double, readMotorRpm, (), (const, override));
    MOCK_METHOD(bool, readAirDetector, (), (const, override));
    MOCK_METHOD(double, readSyringeVolume, (hal::Barrel barrel), (const, override));
    MOCK_METHOD(void, setMotorRpm, (double rpm), (override));
    MOCK_METHOD(void, setValve, (hal::FluidChannel channel, hal::ValveState state), (override));
    MOCK_METHOD(void, emergencyStop, (), (override));
    MOCK_METHOD(void, tick, (double dt), (override));
    MOCK_METHOD(void, injectFault, (const hal::SimulatedFault& fault), (override));
    MOCK_METHOD(void, clearFaults, (), (override));
};

}  // namespace injector::testing
```

### Unit Test Examples (Key Scenarios)

**Motor model — first-order lag:**
```
Given: commandedRpm = 1000, actualRpm = 0, timeConstant = 50 ms
When:  tick(0.002) called (2 ms)
Then:  actualRpm ≈ 1000 * (1 - e^(-2/50)) ≈ 39.2 RPM
       (not 0, not 1000 — partial convergence)
```

**PID controller — steady state:**
```
Given: target = 4.0 mL/s, Kp=100, Ki=50, Kd=5
When:  run 1000 ticks with simulated motor+pressure model
Then:  mean actual flow rate over last 500 ticks is 4.0 ± 0.08 mL/s (2%)
```

**State machine — invalid transition:**
```
Given: state = Idle, no protocol loaded
When:  command = Start
Then:  returns error "Cannot start: current state is Idle"
       state remains Idle
```

**Safety monitor — overpressure:**
```
Given: pressureLimit = 325.0 psi
When:  MockHal.readPressure() returns 326.0
Then:  safetyCheck() returns [FaultType::Overpressure{326.0, 325.0}]
```

**Safety monitor — motor divergence (sustained):**
```
Given: divergenceThreshold = 200 RPM, divergenceTicks = 25
When:  MockHal returns commandedRpm=1000, actualRpm=500 for 25 consecutive checks
Then:  safetyCheck() returns [FaultType::MotorFault{1000, 500}] on tick 25
       (no fault on ticks 1-24)
```

**Ring buffer — overwrite oldest:**
```
Given: capacity = 3, buffer contains [A, B, C]
When:  push(D)
Then:  buffer contains [B, C, D], size = 3
```

---

## 2. Integration Test Plan

Integration tests verify component boundaries — that two or more components work correctly together. They may involve threading, gRPC communication, or the full backend process.

### Test Infrastructure

- **gRPC test client:** A lightweight C++ gRPC client (not the frontend) that sends commands and reads streaming responses. Used to test the backend as a black box via its public API.
- **Test backend:** The full backend process started in-process (not as a subprocess) with a test configuration (fast tick rate, small buffers, known PID gains).
- **Timeout policy:** All integration tests have a 30-second timeout. Streaming tests use latches/barriers to synchronize, not `sleep()`.

### Integration Test Matrix

| Test | Components Under Test | Boundary Verified | Test File |
|------|----------------------|-------------------|-----------|
| **HAL physics consistency** | MotorModel + PressureModel + ValveModel + SyringeModel via SimulatedHal | Models interact correctly through tick sequencing | `test_simulated_hal_integration.cpp` |
| **Control loop + HAL** | ControlLoop + SimulatedHal | PID drives HAL to target flow rate; volume accumulates correctly | `test_control_loop.cpp` |
| **Safety monitor + HAL** | SafetyMonitor + SimulatedHal | Safety monitor detects faults from real physics model (not mocked values) | `test_safety_integration.cpp` |
| **State machine + control loop** | StateMachine + ControlLoop + SimulatedHal | Full injection lifecycle: Idle → Armed → Injecting → Completed | `test_injection_lifecycle.cpp` |
| **Safety fault end-to-end** | SafetyMonitor + StateMachine + ControlLoop + SimulatedHal | Fault injection → safety detection → motor stop → state transition to Fault | `test_fault_end_to_end.cpp` |
| **gRPC command round-trip** | GrpcServer + StateMachine + CommandQueue | Send command via gRPC, receive state change event via stream | `test_grpc_integration.cpp` |
| **gRPC telemetry streaming** | GrpcServer + ControlLoop + TelemetryBroadcast | Start injection via gRPC, receive telemetry frames at expected rate | `test_grpc_integration.cpp` |
| **Data export** | DataLogger + GrpcServer + ControlLoop | Run injection, export via gRPC, verify CSV/JSON content | `test_grpc_integration.cpp` |
| **Config loading** | Config + all components | Start backend with config file, verify parameters propagate to HAL/PID/safety | `test_config_integration.cpp` |

### Key Integration Test Scenarios

**Full happy-path injection (maps to AC-4.3):**
```
1. Start backend with default config
2. LoadProtocol: [Contrast 4.0 mL/s 80 mL 325 psi, Saline 2.0 mL/s 30 mL 200 psi]
3. SendCommand: ARM → verify Armed event
4. SendCommand: START → verify Injecting event
5. StreamTelemetry: collect frames until Completed event
6. Verify: Phase 1 delivered 80 mL ± 2%
7. Verify: Phase 2 delivered 30 mL ± 2%
8. Verify: Phase transition event between phases
9. Verify: total elapsed time reasonable (≈ 35 seconds)
10. ExportData("csv"): verify row count ≈ total_time / tick_rate
```

**Overpressure fault (maps to AC-5.1):**
```
1. Start injection at 4.0 mL/s
2. Wait for steady state (~2 seconds)
3. InjectFault(OVERPRESSURE, 350 psi)
4. Verify: Fault event received within 100 ms
5. Verify: Fault event details show type=OVERPRESSURE, value=350, threshold=325
6. Verify: telemetry shows state=FAULT, actualFlowRate=0, motorRpm=0
7. SendCommand: RESET → verify Idle event
```

**Pause and resume (maps to Flow 3):**
```
1. Start injection at 4.0 mL/s
2. Wait 5 seconds
3. SendCommand: PAUSE → verify Paused event
4. Record volume at pause
5. Collect telemetry for 2 seconds → verify flow rate = 0, volume unchanged
6. SendCommand: RESUME → verify Injecting event
7. Wait for completion
8. Verify: total volume = programmed volume ± 2%
```

**Frontend disconnect resilience:**
```
1. Start injection
2. Cancel gRPC streams (simulates disconnect)
3. Wait 5 seconds
4. Reconnect, call GetState
5. Verify: injection continued, state is Injecting or Completed, volume progressed
```

---

## 3. End-to-End Scenarios

End-to-end tests exercise the full system including the frontend. These are primarily manual for MVP, with potential for automation in later phases.

### Scenario Mapping (from Functional Spec User Flows)

| Scenario | Functional Spec Source | Verification Method |
|----------|----------------------|-------------------|
| Configure and execute 2-phase injection | Flow 1 (10 steps) | Manual: walk through each step, verify UI updates |
| Overpressure fault during injection | Flow 2 (5 steps) | Manual: inject fault via UI/gRPC, verify fault display |
| Pause and resume mid-injection | Flow 3 (3 steps) | Manual: verify flow ramps down/up, volume pauses/resumes |
| Emergency stop from any state | Flow 4 | Manual: verify E-stop works from Armed, Injecting, Paused |
| Frontend started before backend | Reconnection flow | Manual: start frontend first, then backend, verify auto-connect |
| Backend restart during injection | Reliability | Manual: kill backend during injection, verify frontend shows disconnect, restart backend, verify frontend reconnects to Idle |

### End-to-End Checklist (Pre-Release)

Run once before each milestone is considered complete:

- [ ] Fresh start: both processes launch without errors
- [ ] Load a 2-phase protocol (contrast + saline)
- [ ] Arm → Start → observe injection to completion
- [ ] Dashboard shows flow rate, pressure, volume updating live
- [ ] Timeline chart plots flow rate and pressure continuously
- [ ] Phase transition visible in UI (phase highlight changes)
- [ ] Completed state shown, final volume matches programmed
- [ ] Export CSV — opens in spreadsheet, columns correct
- [ ] Arm → Start → Pause → Resume → Completed
- [ ] Arm → Start → inject overpressure fault → Fault state → Acknowledge → Reset
- [ ] Arm → Start → inject air bubble → Fault state
- [ ] Emergency stop from Injecting state
- [ ] Emergency stop from Armed state
- [ ] Invalid commands rejected (Start while Idle — button disabled)
- [ ] Protocol validation: flow rate 15 mL/s rejected, volume 0 rejected
- [ ] Syringe volume check: protocol exceeding syringe rejected
- [ ] Kill backend → frontend shows "Disconnected" → restart backend → auto-reconnect

---

## 4. Test Data Strategy

### Physics Model Fixtures

Predefined HAL configurations for repeatable test scenarios:

**Default (matches config defaults):**
```json
{
  "flowPerRpm": 0.01,
  "tubingResistance": 50.0,
  "baselinePressure": 10.0,
  "motorTimeConstantMs": 50.0,
  "contrastVolumeMl": 100.0,
  "salineVolumeMl": 50.0
}
```

**Fast convergence (for quick integration tests):**
```json
{
  "flowPerRpm": 0.01,
  "tubingResistance": 50.0,
  "baselinePressure": 10.0,
  "motorTimeConstantMs": 10.0,
  "contrastVolumeMl": 100.0,
  "salineVolumeMl": 50.0
}
```
Motor reaches target in ~30 ms instead of ~150 ms. Tests run faster without waiting for ramp-up.

**Near-limit (for safety testing):**
```json
{
  "flowPerRpm": 0.01,
  "tubingResistance": 80.0,
  "baselinePressure": 10.0,
  "motorTimeConstantMs": 50.0,
  "contrastVolumeMl": 100.0,
  "salineVolumeMl": 50.0
}
```
At 4 mL/s: pressure = 4.0 * 80 + 10 = 330 psi. Exceeds default 325 psi limit. Useful for testing safety response to marginal conditions.

### Protocol Fixtures

Predefined protocols used across multiple tests:

| Name | Phases | Purpose |
|------|--------|---------|
| `single_phase_contrast` | 1 phase: Contrast 4.0 mL/s, 80 mL, 325 psi | Simplest happy path |
| `two_phase_standard` | Phase 1: Contrast 4.0/80/325, Phase 2: Saline 2.0/30/200 | Standard CT injection |
| `three_phase_mixed` | Contrast 4.0/40/325, Saline 3.0/30/200, Contrast 2.0/20/325 | Multi-phase with fluid switching |
| `small_volume_fast` | Contrast 2.0/10/325 | Quick test, completes in ~5 seconds |
| `max_phases` | 20 identical phases: Contrast 1.0/5/325 | Boundary: maximum protocol size |
| `high_flow_low_limit` | Contrast 8.0/50/200 | Edge case: PID cannot reach target without exceeding pressure limit |
| `near_empty_syringe` | Contrast 4.0/95/325 (syringe has 100 mL) | Edge case: minimal margin |

### Mock Sensor Sequences

For safety monitor unit tests, predefined sequences of sensor readings:

| Sequence Name | Description | Values |
|---------------|-------------|--------|
| `steady_state` | Normal injection at 4 mL/s | pressure: 210, motorRpm: 400, air: false |
| `pressure_ramp` | Gradual pressure rise (partial occlusion) | pressure: 210, 230, 250, 270, 290, 310, 330 |
| `pressure_spike` | Instant overpressure | pressure: 210, 210, 350 |
| `motor_stall` | Motor fails to reach target | commandedRpm: 400, actualRpm: 50 (sustained) |
| `motor_recovery` | Motor diverges then recovers before threshold | divergence for 20 ticks (< 25 threshold), then converges |
| `air_detected` | Air bubble mid-injection | air: false, false, true |
| `simultaneous_faults` | Overpressure + air in same tick | pressure: 350, air: true |

---

## 5. Test Organization & Running

### Directory Structure

All test files live under `backend/tests/`. For the full file listing, see **06-repo-structure.md, Section 1**.

Key directories:
- `mocks/` — `MockHal.h`
- `fixtures/` — `TestConfigs.h`, `TestProtocols.h`
- Root: `test_*.cpp` files (12 unit test files + 7 integration test files)

### Test CMakeLists.txt

```cmake
find_package(GTest REQUIRED)

# Unit tests — fast, no threading, no gRPC
add_executable(injector-unit-tests
    test_motor_model.cpp
    test_pressure_model.cpp
    test_valve_model.cpp
    test_syringe_model.cpp
    test_air_detector.cpp
    test_pid_controller.cpp
    test_state_machine.cpp
    test_safety_monitor.cpp
    test_ring_buffer.cpp
    test_command_queue.cpp
    test_data_logger.cpp
    test_config.cpp
)
target_link_libraries(injector-unit-tests PRIVATE
    injector-backend-lib    # static lib of all backend sources (excluding main.cpp)
    GTest::gtest_main
    GTest::gmock
)
add_test(NAME unit-tests COMMAND injector-unit-tests)

# Integration tests — may use threads, gRPC, real timing
add_executable(injector-integration-tests
    test_simulated_hal_integration.cpp
    test_control_loop.cpp
    test_safety_integration.cpp
    test_injection_lifecycle.cpp
    test_fault_end_to_end.cpp
    test_grpc_integration.cpp
    test_config_integration.cpp
)
target_link_libraries(injector-integration-tests PRIVATE
    injector-backend-lib
    GTest::gtest_main
    GTest::gmock
    gRPC::grpc++
)
add_test(NAME integration-tests COMMAND injector-integration-tests)
set_tests_properties(integration-tests PROPERTIES TIMEOUT 120)
```

**Note:** Backend sources (excluding `main.cpp`) are compiled as a static library `injector-backend-lib` so both the executable and test targets can link against them without recompilation.

### Running Tests

```bash
# All tests
ctest --test-dir build --output-on-failure

# Unit tests only (fast, run often)
ctest --test-dir build -R unit-tests --output-on-failure

# Integration tests only (slower)
ctest --test-dir build -R integration-tests --output-on-failure

# With verbose gtest output
./build/backend/tests/injector-unit-tests --gtest_print_time=1

# Filter to specific test
./build/backend/tests/injector-unit-tests --gtest_filter="StateMachine.*"

# Run under sanitizers (if built with Debug)
./build/backend/tests/injector-unit-tests   # ASan/TSan/UBSan active automatically
```

### CI Expectations

- Unit tests: must pass in < 30 seconds total
- Integration tests: must pass in < 120 seconds total
- All tests pass 10/10 consecutive runs (no flakiness)
- Sanitizers (TSan, ASan, UBSan): zero findings
- Tests run on both Windows (MSVC) and Linux (GCC)

---

## 6. Acceptance Criteria → Test Case Mapping

Every acceptance criterion from the functional spec (02) has a corresponding test. This table maps them.

### HAL (F-MVP-1)

| AC | Test Type | Test File | Scenario |
|----|-----------|-----------|----------|
| AC-1.1 Pressure responds to flow | Unit | `test_pressure_model.cpp` | `compute(4.0)` returns `4.0 * 50 + 10 = 210` |
| AC-1.2 Motor inertia | Unit | `test_motor_model.cpp` | After 1 tick at 2 ms: 0 < actualRpm < 1000 |
| AC-1.3 Valve actuation | Unit | `test_valve_model.cpp` | Open/close/read cycle |
| AC-1.4 Air detector default | Unit | `test_air_detector.cpp` | Default returns false |
| AC-1.5 Syringe tracking | Unit | `test_syringe_model.cpp` | 100 mL - 20 mL drained = 80 mL ± 0.1 |

### Scheduling (F-MVP-2)

| AC | Test Type | Test File | Scenario |
|----|-----------|-----------|----------|
| AC-2.1 Tick rate | Integration | `test_control_loop.cpp` | 1000 ticks, mean 2.0 ms ± 0.1, max < 5 ms |
| AC-2.2 Jitter stats | Integration | `test_control_loop.cpp` | After 100 ticks, query returns min/max/mean/stddev |
| AC-2.3 Priority ordering | Integration | `test_control_loop.cpp` | Under load, control loop maintains timing |

### PID (F-MVP-3)

| AC | Test Type | Test File | Scenario |
|----|-----------|-----------|----------|
| AC-3.1 Steady-state accuracy | Unit | `test_pid_controller.cpp` | 1000 ticks at target 4.0 → mean 4.0 ± 2% |
| AC-3.2 Volume accuracy | Integration | `test_injection_lifecycle.cpp` | 80 mL phase delivers 78.4-81.6 mL |
| AC-3.3 Acceleration ramp | Unit | `test_pid_controller.cpp` | Flow rate derivative ≤ 10 mL/s² |
| AC-3.4 No oscillation | Unit | `test_pid_controller.cpp` | Settles within 5% in ≤ 500 ticks (1 sec) |

### State Machine (F-MVP-4)

| AC | Test Type | Test File | Scenario |
|----|-----------|-----------|----------|
| AC-4.1 Valid transition | Unit | `test_state_machine.cpp` | Idle + Arm → Armed, logged |
| AC-4.2 Invalid rejection | Unit | `test_state_machine.cpp` | Idle + Start → error, stays Idle |
| AC-4.3 Multi-phase | Integration | `test_injection_lifecycle.cpp` | 3 phases, each ± 2% volume |
| AC-4.4 Phase boundary | Integration | `test_injection_lifecycle.cpp` | Target changes, ramp (not step) |

### Safety Monitor (F-MVP-5)

| AC | Test Type | Test File | Scenario |
|----|-----------|-----------|----------|
| AC-5.1 Overpressure | Unit + Integration | `test_safety_monitor.cpp`, `test_fault_end_to_end.cpp` | 326 psi → fault within 10 ms |
| AC-5.2 Air detection | Unit | `test_safety_monitor.cpp` | air=true → fault |
| AC-5.3 Motor fault | Unit | `test_safety_monitor.cpp` | Sustained divergence 25 ticks → fault |
| AC-5.4 Timing violation | Unit | `test_safety_monitor.cpp` | Tick > 5 ms → fault |
| AC-5.5 Independence | Integration | `test_safety_integration.cpp` | State machine hung, safety still halts |
| AC-5.6 Fault during pause | Unit | `test_state_machine.cpp` | Paused + fault → Fault |

### Command Interface (F-MVP-6)

| AC | Test Type | Test File | Scenario |
|----|-----------|-----------|----------|
| AC-6.1 Round-trip time | Integration | `test_grpc_integration.cpp` | Arm → event within 100 ms |
| AC-6.2 Telemetry streaming | Integration | `test_grpc_integration.cpp` | 5 sec injection → ~100 frames ± 10% |
| AC-6.3 Invalid command rejection | Integration | `test_grpc_integration.cpp` | Pause in Completed → error |
| AC-6.4 InjectFault | Integration | `test_grpc_integration.cpp` | InjectFault(overpressure, 350) → fault event |

### Data Logging (F-MVP-7)

| AC | Test Type | Test File | Scenario |
|----|-----------|-----------|----------|
| AC-7.1 Tick capture | Integration | `test_grpc_integration.cpp` | 10 sec → ≥ 4900 rows |
| AC-7.2 Non-blocking | Unit | `test_ring_buffer.cpp` | Full buffer + push → no delay, oldest dropped |
| AC-7.3 Event completeness | Integration | `test_injection_lifecycle.cpp` | 6 events present, monotonic timestamps |
| AC-7.4 CSV format | Unit | `test_data_logger.cpp` | Header + consistent columns |

### UI (F-MVP-8, 9, 10)

UI acceptance criteria are verified via manual end-to-end testing (Section 3). Automated UI testing is deferred to Phase 2+.

---

## 7. Test Principles

1. **Tests are deterministic.** No `sleep()` in unit tests. Integration tests use explicit synchronization (latches, barriers, condition variables) not timing assumptions.

2. **Tests are fast.** Unit tests complete in < 30 seconds total. If a test needs to run 1000 PID iterations, it uses a fixed `dt` value and calls `tick()` in a loop — no wall-clock waiting.

3. **Tests are independent.** No shared state between test cases. Each test constructs its own objects. No test ordering dependencies.

4. **Tests test behavior, not implementation.** Verify that the PID reaches the target, not that it called a specific internal method. Verify the state machine rejects invalid commands, not that it used a specific `if` branch.

5. **One assertion concept per test.** A test named `OverpressureTriggersAtThreshold` tests exactly that — not also that the motor stops and the state changes. Those are separate tests.

6. **Failure messages are diagnostic.** Use gtest's `EXPECT_NEAR(actual, 4.0, 0.08) << "Flow rate did not reach target after 1000 ticks"` — not bare `EXPECT_TRUE(ok)`.

7. **Mock the boundary, not the internals.** Mock `IHalInterface` when testing the safety monitor. Don't mock `PressureModel` when testing `SimulatedHal` — that's an internal detail.
