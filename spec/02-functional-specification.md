# Functional Specification

## 1. Feature List with Priorities

### Phase 1 — MVP

The MVP demonstrates the 7-layer architecture working end-to-end with one complete injection cycle. Every layer from HAL through UI is exercised in a single happy-path run.

#### F-MVP-1: Simulated Hardware Abstraction Layer (HAL)

- Virtual pressure sensor returning simulated mmHg readings driven by a physics model (flow through resistance)
- Virtual motor driver accepting RPM commands and reporting actual RPM with simulated inertia and lag
- Virtual valve actuators (open/close) for contrast and saline lines
- Virtual air detector returning a boolean air-present signal (default: no air)
- Virtual syringe model tracking remaining volume per barrel (contrast, saline)
- All HAL calls return within a bounded time; no real I/O

#### F-MVP-2: Simulated RTOS / Scheduling Layer

- A dedicated thread (pinned to a core on Linux, best-effort on Windows) running the control loop at a configurable tick rate (default 2 ms)
- Monotonic high-resolution timer driving the loop
- Jitter measurement: each tick records actual vs. expected wake time; exposed as telemetry
- Priority-based thread model: control loop > safety monitor > command interface > logging > UI

#### F-MVP-3: PID Control Loop

- Closed-loop PID controller converting target flow rate to motor RPM commands via HAL
- Configurable P, I, D gains (with sane defaults)
- Acceleration and deceleration ramp limiting (configurable max mL/s²)
- Loop reads sensor values, computes error, applies PID, writes actuator commands — every tick
- Tracks cumulative volume delivered by integrating actual flow rate over time

#### F-MVP-4: State Machine

- Six states: `Idle`, `Armed`, `Injecting`, `Paused`, `Completed`, `Fault`. Full state enum, transition table, and command definitions in **04-schema-and-contracts.md, Section 1**.
- Multi-phase execution: the state machine steps through an ordered list of protocol phases (e.g., contrast bolus at 4 mL/s for 80 mL, saline flush at 2 mL/s for 30 mL), transitioning the control loop target at each phase boundary
- Invalid transition attempts are rejected and logged

#### F-MVP-5: Safety Monitor

- Runs as an independent thread, checking at the same or faster rate as the control loop
- Overpressure check: if simulated pressure exceeds configured threshold (default 325 psi), triggers fault
- Air detection check: if air detector reports air present, triggers fault
- Motor fault check: if commanded RPM and actual RPM diverge beyond threshold for more than N ticks, triggers fault
- Timing violation check: if the control loop misses its deadline by more than the configured jitter tolerance, triggers fault
- On any fault: immediately commands motor stop, closes all valves, transitions state machine to `Fault`, records fault type and timestamp
- Safety monitor cannot be disabled by the UI or command interface

#### F-MVP-6: Command Interface (Controller ↔ UI Bridge)

- Message-based interface between the UI/application layer and the real-time controller
- Accepts commands: `LoadProtocol`, `Arm`, `Disarm`, `Start`, `Pause`, `Resume`, `Reset`, `InjectFault` (for testing)
- Emits telemetry at configurable rate (default 50 ms / 20 Hz): current state, phase index, flow rate (target and actual), pressure, volume delivered per phase, cumulative volume, motor RPM, active faults
- Emits events: state transitions, fault triggers, phase transitions, protocol completion
- Uses a thread-safe message queue
- Commands that arrive during invalid states return an error response

#### F-MVP-7: Data Logging

- Ring buffer capturing timestamped control loop data every tick: time, target flow rate, actual flow rate, pressure, motor RPM, valve states, state machine state
- Non-blocking writes (ring buffer with overwrite-oldest policy)
- Export to CSV or JSON on demand (post-injection)
- Event log capturing all state transitions, faults, and commands with timestamps

#### F-MVP-8: Clinical UI — Protocol Configuration

- Form to define a multi-phase injection protocol:
  - Per phase: fluid type (contrast/saline), flow rate (mL/s), volume (mL), pressure limit (psi)
  - Add/remove/reorder phases
  - Protocol name (optional)
- Input validation per ranges defined in **04-schema-and-contracts.md, Section 6**
- Load protocol into the controller via command interface
- Display loaded syringe volumes (contrast remaining, saline remaining)

#### F-MVP-9: Clinical UI — Injection Control Panel

- Arm/Disarm button (enabled only in valid states)
- Start/Pause/Resume button (context-sensitive based on current state)
- Emergency Stop button (always visible, always enabled, sends immediate stop)
- Current state displayed prominently (color-coded: green=Idle, yellow=Armed, blue=Injecting, orange=Paused, red=Fault, grey=Completed)
- Fault acknowledgment/reset button (enabled only in Fault state)

#### F-MVP-10: Clinical UI — Real-Time Dashboard

- Live-updating display (target: under 500 ms latency from controller state change to screen update):
  - Flow rate gauge (target vs. actual)
  - Pressure gauge (current value with threshold indicator)
  - Volume delivered per phase (progress bar per phase)
  - Total volume delivered / total programmed
  - Current phase indicator (highlight active phase in protocol list)
  - Elapsed time
- Injection timeline: scrolling time-series chart showing flow rate and pressure over time

---

### Phase 2 — Full Protocol Flexibility

#### F-P2-1: Protocol Library
Save, load, duplicate, and delete named protocols. Preset library of common protocols (CT chest, CT abdomen, MRI contrast). Import/export protocols as JSON.

#### F-P2-2: Variable Flow Rate Profiles
Ramp-up, ramp-down, and step profiles within a single phase. Custom flow rate curves defined as time–flow rate point sequences. Visual flow rate curve editor.

#### F-P2-3: Patient Parameter Integration
Patient weight and eGFR input. Weight-based dosing calculator (auto-compute volume from mg/kg and concentration). Contrast dose warnings based on renal function thresholds.

#### F-P2-4: Dual-Syringe Model
Simultaneous contrast + saline mixing at configurable ratios. Dilution protocol phases (e.g., 50/50 contrast/saline at specified total flow rate).

#### F-P2-5: Injection History and Replay
Store completed injection runs with full telemetry. Replay a past injection (accelerated or real-time). Compare two runs side-by-side.

#### F-P2-6: Configurable Physics Model
Adjustable tubing resistance, syringe friction, fluid viscosity. Temperature effect on viscosity (simplified). Model different syringe sizes (60, 100, 150, 200 mL).

---

### Phase 3 — Educational & Research Features

#### F-P3-1: Architecture Explorer (UC-4)
Visual diagram of the 7-layer architecture with real-time activity indicators. Click into any layer to see current state, last N operations, timing stats. State machine visualizer with transition history. PID tuning dashboard with live P/I/D terms and gain sliders.

#### F-P3-2: Fault Injection Workbench (UC-3)
UI panel to trigger simulated faults on demand (overpressure spike, air bubble, motor stall, timing delay). Configurable fault parameters. Fault timeline view with detection latency measurements. Scripted fault scenarios.

#### F-P3-3: Parameter Experimentation Mode (UC-5)
Side-by-side comparison with different PID gains, pressure limits, or flow rates. "What-if" mode: modify parameters mid-injection and see divergence. Export experiment results as structured data.

#### F-P3-4: Communication Inspector
Live view of messages between UI and controller. Message latency histogram. Introduce artificial communication delays to observe UI degradation.

#### F-P3-5: Timing Analysis Dashboard
Control loop jitter histogram. Worst-case execution time tracking per tick. Safety monitor response time statistics. Visual timeline of thread execution.

#### F-P3-6: Guided Tutorial Mode
Step-by-step walkthroughs for each persona. Annotations on UI elements explaining purpose and mapping to real device equivalents.

---

## 2. User Flows

### Flow 1: Configure and Execute a Multi-Phase Injection (Happy Path)

**Persona:** Jordan (RT) or Dr. Patel (Resident)
**Precondition:** Application running. System in `Idle`. Syringes loaded (defaults: 100 mL contrast, 50 mL saline).

| Step | User Action | System Response | What User Sees |
|------|-------------|-----------------|----------------|
| 1 | Opens Protocol Configuration panel | Displays empty protocol form | Form with "Add Phase" button; syringe indicators showing 100 mL contrast / 50 mL saline |
| 2 | Adds Phase 1: Contrast, 4.0 mL/s, 80 mL, 325 psi limit | Validates inputs | Phase 1 row appears, inputs highlighted green |
| 3 | Adds Phase 2: Saline, 2.0 mL/s, 30 mL, 200 psi limit | Validates inputs | Phase 2 row appears below Phase 1 |
| 4 | Clicks "Load Protocol" | Controller validates (sufficient syringe volume, valid params). Returns success. | "Protocol Loaded" confirmation. Syringe indicators show programmed amounts. |
| 5 | Clicks "Arm" | Transitions `Idle` → `Armed` | State indicator turns yellow "ARMED". Start button becomes enabled. |
| 6 | Clicks "Start" | Transitions `Armed` → `Injecting`. Control loop begins Phase 1. PID targets 4.0 mL/s. Motor spins up. Valves open. | State turns blue "INJECTING". Flow rate ramps up toward 4.0 mL/s. Pressure rises. Volume bar fills. Timeline chart starts plotting. Phase 1 highlighted. |
| 7 | Observes injection | Control loop maintains flow rate. Telemetry streams at ~50 ms. | Flow rate stabilizes ~4.0 mL/s. Pressure steady. Volume progresses. |
| 8 | (Auto) Phase 1 completes | State machine transitions to Phase 2. Control loop retargets to 2.0 mL/s saline. Valves switch. | Phase 2 highlighted. Flow rate ramps down to 2.0. Contrast bar complete; saline bar begins filling. |
| 9 | (Auto) Phase 2 completes | Transitions `Injecting` → `Completed`. Motor stops. Valves close. | State turns grey "COMPLETED". Final values displayed. "Total: 110 mL delivered". |
| 10 | Reviews results, exports log | Data logging provides CSV/JSON export | Download button. Summary: total volume, total time, peak pressure. |

### Flow 2: Safety Fault During Injection (Overpressure)

**Persona:** Jordan (RT) or Morgan (QA)
**Precondition:** System in `Injecting`, mid-Phase 1 (~40 mL delivered of 80 mL).

| Step | User Action | System Response | What User Sees |
|------|-------------|-----------------|----------------|
| 1 | Pressure rises (simulated occlusion or injected fault) | Pressure sensor reading exceeds 325 psi threshold | Pressure gauge climbs into red zone |
| 2 | (Auto, < 10 ms) Safety monitor detects overpressure | Motor stopped. All valves closed. State → `Fault`. Fault logged. | State turns red "FAULT". Alarm indicator. Flow drops to 0. "OVERPRESSURE FAULT" message. |
| 3 | Reads fault info | Event log shows details | Fault panel: "Overpressure at T+14.2s. 331 psi (limit: 325). Volume: 42.1/80 mL. Halt latency: 4.2 ms." |
| 4 | Clicks "Acknowledge Fault" | Fault acknowledged. Reset enabled. | Acknowledge button greys out. Reset highlights. |
| 5 | Clicks "Reset" | Transitions `Fault` → `Idle`. Actuators confirmed off. | State returns to green "IDLE". Syringe volumes reflect partial consumption. |

### Flow 3: Pause and Resume Mid-Injection

**Persona:** Jordan (RT)
**Precondition:** System in `Injecting`.

| Step | User Action | System Response | What User Sees |
|------|-------------|-----------------|----------------|
| 1 | Clicks "Pause" | Transitions `Injecting` → `Paused`. Motor decelerates (controlled ramp-down). | State turns orange "PAUSED". Flow ramps to 0. Volume holds. |
| 2 | Waits, inspects dashboard | Control loop runs with target 0. Safety monitor still active. Telemetry continues. | Dashboard live but values static. |
| 3 | Clicks "Resume" | Transitions `Paused` → `Injecting`. Motor ramps back up. | State returns to blue "INJECTING". Flow ramps up. Volume resumes. |

### Flow 4: Emergency Stop

**Persona:** Any
**Precondition:** System in any non-Idle state.

| Step | User Action | System Response | What User Sees |
|------|-------------|-----------------|----------------|
| 1 | Clicks large red "EMERGENCY STOP" | Immediate highest-priority command. Motor → 0. All valves closed. State → `Fault` (type: manual stop). | State turns red. All gauges drop. "MANUAL STOP" fault. Same acknowledge/reset flow as automated faults. |

---

## 3. Edge Cases & Failure Scenarios

### 3A: Simulated Failures (Injector Design)

These are conditions the simulator must handle as part of its core safety architecture.

| ID | Scenario | Expected Behavior |
|----|----------|-------------------|
| EF-1 | **Overpressure during phase transition** — Valve switching and motor adjustment cause transient pressure spike at phase boundary | Safety monitor evaluates spike. If below threshold, injection continues. If exceeds threshold even transiently, fault triggers. |
| EF-2 | **Air detection during saline flush** — Air in saline line detected mid-phase | Safety monitor halts injection. Validates safety works during non-contrast phases. |
| EF-3 | **Motor stall / degradation** — Motor fails to reach commanded RPM (simulated mechanical resistance) | Motor fault detector recognizes sustained divergence between commanded and actual RPM. Triggers fault. |
| EF-4 | **Gradual pressure buildup** — Slow rise from partial occlusion over many seconds | System detects when threshold is crossed regardless of rate of change. |
| EF-5 | **Simultaneous faults** — Two fault conditions in the same control loop tick (e.g., overpressure AND motor fault) | Both logged and reported. Single halt action. Both causes recorded. |
| EF-6 | **Fault during pause** — Fault condition arises while paused (motor stopped, valves open) | Transitions `Paused` → `Fault`. |
| EF-7 | **Syringe runs empty** — Volume tracking drift causes syringe to empty unexpectedly | Treated as fault. Motor must not run dry. (Should normally be caught at protocol load/arm time.) |

### 3B: Application-Level Edge Cases

| ID | Scenario | Expected Behavior |
|----|----------|-------------------|
| AE-1 | **Invalid protocol parameters** — Flow rate 0, negative volume, non-numeric input | UI validates and rejects. Controller validates as second defense. |
| AE-2 | **Rapid state transition commands** — Start then immediately Pause within milliseconds | Command queue processes in order. State machine handles both without race conditions. |
| AE-3 | **Commands in invalid states** — Start while Idle, Resume while Injecting | Command interface rejects with clear error. UI prevents via disabled buttons, but controller is safe regardless. |
| AE-4 | **Protocol modification while armed/injecting** — Load new protocol during execution | Rejected. Protocol changes only in `Idle`. |
| AE-5 | **UI disconnection / reconnection** — UI process crashes during injection | Controller continues safely. Timeout-based fault may trigger if UI heartbeat lost. Reconnected UI queries current state and resumes telemetry display. |
| AE-6 | **Control loop timing degradation** — Heavy system load delays ticks | Safety monitor's timing violation check detects. Fault during injection; warning while idle. |
| AE-7 | **Telemetry buffer overflow** — UI slow to consume telemetry | Controller never blocks. Oldest messages dropped. Dropped-message counter maintained. |
| AE-8 | **Double-click / duplicate commands** — Double-click Arm or Start | Idempotent: if already in target state, second command is a no-op. |
| AE-9 | **Emergency stop while in Fault** — E-stop when already faulted | No-op or confirms actuators off. Must not crash. |
| AE-10 | **Maximum protocol size** — 50+ phases | Enforce reasonable maximum (e.g., 20 phases) with clear message, or handle gracefully. |
| AE-11 | **Zero-volume phase** — Phase with volume 0 mL | Rejected at validation, or skipped instantly during execution. |
| AE-12 | **High flow rate with low pressure limit** — 10 mL/s target, 50 psi limit | PID cannot reach target without exceeding pressure. Flow stays below target. No fault as long as actual pressure is below limit. No wild oscillation. |

---

## 4. Acceptance Criteria (MVP Features)

### F-MVP-1: Simulated HAL

**AC-1.1: Pressure sensor responds to flow**
Given the motor is driving fluid at a nonzero flow rate,
When the control loop reads the pressure sensor,
Then the returned value is > 0 and varies proportionally with flow rate and simulated resistance.

**AC-1.2: Motor inertia modeling**
Given the motor is at 0 RPM and a command to set 1000 RPM is issued,
When 1 tick elapses,
Then reported actual RPM is > 0 but < 1000 (not instantaneous).

**AC-1.3: Valve actuation**
Given a valve is closed,
When `open_valve(contrast)` is called,
Then subsequent reads return `open` and flow through that channel becomes possible.

**AC-1.4: Air detector default state**
Given no fault has been injected,
When the air detector is read,
Then it returns `false`.

**AC-1.5: Syringe volume tracking**
Given a syringe loaded with 100 mL and 20 mL delivered,
When remaining volume is queried,
Then the returned value is 80 mL (± 0.1 mL).

### F-MVP-2: Simulated RTOS / Scheduling

**AC-2.1: Control loop tick rate**
Given `Injecting` state with default 2 ms tick rate,
When 1000 consecutive ticks are measured,
Then mean interval is 2.0 ms ± 0.1 ms, and no single tick exceeds 5 ms.

**AC-2.2: Jitter measurement available**
Given the control loop has run for ≥ 100 ticks,
When jitter stats are queried,
Then the system returns min, max, mean tick interval and standard deviation.

**AC-2.3: Thread priority ordering**
Given control loop, safety monitor, and command interface threads are running under CPU contention,
Then the control loop maintains its tick rate while other threads may be delayed.

### F-MVP-3: PID Control Loop

**AC-3.1: Steady-state flow rate accuracy**
Given target 4.0 mL/s and ≥ 2 seconds of injection (past ramp-up),
When actual flow rate is sampled over 1 second,
Then mean is 4.0 mL/s ± 2%.

**AC-3.2: Volume accuracy**
Given a single phase of 80 mL at 4.0 mL/s,
When the phase completes,
Then total delivered volume is 80 mL ± 2% (78.4–81.6 mL).

**AC-3.3: Acceleration ramp**
Given max acceleration 10 mL/s² and target step from 0 to 4.0 mL/s,
When injection starts,
Then flow rate increases at ≤ 10 mL/s², reaching 4.0 in ~0.4 s (not instantaneously).

**AC-3.4: PID prevents oscillation**
Given default PID gains and a step change in target,
When response is observed,
Then flow rate settles within 5% of target in ≤ 1 second with no sustained oscillation (≤ 2 overshoot cycles).

### F-MVP-4: State Machine

**AC-4.1: Valid transition**
Given `Idle` with valid protocol loaded,
When `Arm` command received,
Then system transitions to `Armed` within 1 tick; transition logged with timestamp.

**AC-4.2: Invalid transition rejection**
Given `Idle` state,
When `Start` command received,
Then rejected with `INVALID_STATE_TRANSITION` error; system remains `Idle`; rejection logged.

**AC-4.3: Multi-phase execution**
Given 3-phase protocol (Phase A: 40 mL, B: 30 mL, C: 20 mL),
When injection runs to completion,
Then three phase transitions logged; each phase delivers programmed volume ± 2%; total time ± 5%.

**AC-4.4: Phase boundary transition**
Given Phase 1 at 4.0 mL/s and Phase 2 at 2.0 mL/s,
When Phase 1 completes,
Then control loop target changes to 2.0; flow rate transitions via deceleration ramp (not instant drop).

### F-MVP-5: Safety Monitor

**AC-5.1: Overpressure detection**
Given injecting with 325 psi limit,
When pressure reaches 326 psi,
Then fault triggers within 10 ms (simulated time); motor → 0 RPM; valves closed; state → `Fault`; log records type, value, timestamp.

**AC-5.2: Air detection**
Given injecting,
When air detector reports `true`,
Then fault triggers within 10 ms with type "air_detected".

**AC-5.3: Motor fault**
Given motor commanded to 1000 RPM,
When actual RPM stays below 200 for > 50 ms (25 ticks at 2 ms),
Then motor fault triggers.

**AC-5.4: Timing violation**
Given 2 ms tick rate with 3 ms jitter tolerance,
When a single tick takes > 5 ms during `Injecting` or `Paused`,
Then timing violation fault triggers.

**AC-5.5: Safety monitor independence**
Given a simulated hang in the state machine thread,
When the safety monitor runs,
Then it still detects faults and halts motor/valves independently.

**AC-5.6: Fault during pause**
Given `Paused` state,
When air detector reports `true`,
Then system transitions `Paused` → `Fault`.

### F-MVP-6: Command Interface

**AC-6.1: Command round-trip time**
Given `Idle` state,
When UI sends `Arm`,
Then UI receives state-transition event confirming `Armed` within 100 ms.

**AC-6.2: Telemetry streaming**
Given `Injecting` with 50 ms telemetry rate,
When observed over 5 seconds,
Then ~100 messages received (± 10%), each containing: state, phase index, target/actual flow rate, pressure, volume, motor RPM.

**AC-6.3: Invalid state command rejection**
Given `Completed` state,
When `Pause` sent,
Then error response with "invalid state for command" within 100 ms.

**AC-6.4: InjectFault command**
Given `Injecting` state,
When `InjectFault(overpressure, 350)` sent,
Then pressure sensor reports 350 psi, causing safety monitor to trigger overpressure fault.

### F-MVP-7: Data Logging

**AC-7.1: Tick-level capture**
Given 10-second injection at 2 ms tick rate (5000 ticks),
When exported,
Then ≥ 4900 rows, each with: timestamp, target/actual flow rate, pressure, motor RPM, valve states, state.

**AC-7.2: Non-blocking logging**
Given ring buffer full,
When new entry written,
Then write completes without blocking; oldest entry overwritten; tick not delayed > 0.1 ms.

**AC-7.3: Event log completeness**
Given injection with arm, start, phase transition, pause, resume, completion,
When event log reviewed,
Then all six events present with monotonically increasing timestamps.

**AC-7.4: CSV export format**
Given completed injection,
When exported as CSV,
Then file has header row, consistent column count, opens in spreadsheet without errors.

### F-MVP-8: Protocol Configuration UI

**AC-8.1: Phase creation**
Given configuration form open,
When user adds phase (flow rate 4.0, volume 80, limit 325, contrast),
Then phase appears in list with all values displayed.

**AC-8.2: Input validation — out of range**
Given form open,
When user enters flow rate 15.0 mL/s,
Then field shows error "Flow rate must be between 0.1 and 10.0 mL/s"; Load Protocol button disabled.

**AC-8.3: Input validation — insufficient volume**
Given contrast syringe contains 50 mL,
When protocol with 80 mL contrast is loaded,
Then rejected: "Insufficient contrast volume: 80 mL required, 50 mL available."

**AC-8.4: Phase reordering**
Given Phase A (contrast) and Phase B (saline),
When reordered to B then A,
Then list shows B first; loading executes B before A.

### F-MVP-9: Injection Control Panel UI

**AC-9.1: State-dependent button enabling**

| State | Arm | Disarm | Start | Pause | Resume | E-Stop | Reset |
|-------|-----|--------|-------|-------|--------|--------|-------|
| Idle | ✓ | — | — | — | — | ✓ | — |
| Armed | — | ✓ | ✓ | — | — | ✓ | — |
| Injecting | — | — | — | ✓ | — | ✓ | — |
| Paused | — | — | — | — | ✓ | ✓ | — |
| Fault | — | — | — | — | — | ✓ | ✓ |
| Completed | — | — | — | — | — | ✓ | — |

**AC-9.2: State color coding**
Idle=green, Armed=yellow, Injecting=blue, Paused=orange, Fault=red, Completed=grey.

**AC-9.3: Emergency stop always works**
Given any state,
When E-Stop clicked,
Then system enters `Fault` (or stays if already there); all actuators confirmed off.

### F-MVP-10: Real-Time Dashboard UI

**AC-10.1: Update latency**
Given `Injecting`,
When controller flow rate changes,
Then dashboard displays update within 500 ms.

**AC-10.2: Flow rate display accuracy**
Given controller reports 3.95 mL/s,
Then dashboard displays 3.95 (or 4.0 if rounding to 1 decimal — rounding policy must be consistent).

**AC-10.3: Volume progress**
Given Phase 1 configured 80 mL, 40 mL delivered,
Then progress bar shows 50%; numeric shows "40.0 / 80.0 mL".

**AC-10.4: Timeline chart**
Given 20-second completed injection,
Then chart shows continuous flow rate and pressure lines over full duration with phase boundaries and fault events marked.

**AC-10.5: Dashboard in idle**
Given `Idle` state,
Then all gauges show 0/baseline; no errors; timeline empty or shows "No active injection."

---

## 5. Feature-to-Use-Case Mapping

| Use Case | MVP Features | Phase 2 | Phase 3 |
|----------|-------------|---------|---------|
| UC-1: Configure & Execute | F-MVP-1 through F-MVP-4, F-MVP-8 | F-P2-1, F-P2-2, F-P2-3, F-P2-4 | — |
| UC-2: Real-Time Monitor | F-MVP-6, F-MVP-7, F-MVP-10 | F-P2-5 | — |
| UC-3: Safety Interventions | F-MVP-5, F-MVP-9 | — | F-P3-2 |
| UC-4: Inspect Architecture | F-MVP-4 (state machine), F-MVP-7 (logs) | — | F-P3-1, F-P3-4, F-P3-5 |
| UC-5: Experiment | F-MVP-3 (PID params), F-MVP-8 (protocol config) | F-P2-6 | F-P3-3 |

---

## 6. Open Design Decisions

These must be resolved before implementation begins (next step: System Architecture Blueprint).

| # | Decision | Recommendation | Alternatives |
|---|----------|---------------|-------------|
| 1 | **Communication mechanism** — Message queues vs. shared memory with lock-free ring buffers | Message queues for MVP (simpler) | Shared memory is more faithful to real hardware |
| 2 | **Physics model fidelity** — How realistic does pressure/flow model need to be? | Simple linear model for MVP (pressure = flow rate × resistance) | Phase 2 adds nonlinear effects |
| 3 | **UI framework** — Must support real-time chart updates at 20 Hz | TBD in architecture blueprint | Web (React/Vue + WebSocket), desktop (Qt, Electron), terminal |
| 4 | **Simulated time vs. wall clock** — Real-time only or time acceleration? | Real-time only for MVP | Phase 2 adds acceleration for replay |
| 5 | **Persistence** — File-based vs. database for logs and protocols | File-based export for MVP | Phase 2 evaluates database for history/replay |
