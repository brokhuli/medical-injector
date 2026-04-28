# Simulated Devices Architecture

The simulated devices layer replaces real hardware (motors, sensors, valves, syringes) with computed physics models. It is the `IHalInterface` implementation defined in the backend architecture. All "hardware" behavior is math — no real I/O, no serial ports, no microcontrollers.

This layer lives entirely within the backend process. The frontend has no direct access to it — it only sees the effects through telemetry streamed via gRPC.

## Role in the System

```
┌──────────────────────────────────────────────┐
│       Backend Process                        │
│                                              │
│  Control Loop Thread                         │
│    │ setMotorRpm(rpm)                        │
│    │ readPressure() → psi                    │
│    │ readMotorRpm() → actual RPM             │
│    ▼                                         │
│  ┌────────────────────────────────────────┐  │
│  │       IHalInterface (abstract class)   │  │
│  └──────────────┬─────────────────────────┘  │
│                 │                             │
│  ┌──────────────▼─────────────────────────┐  │
│  │       SimulatedHal (this spec)         │  │
│  │                                        │  │
│  │  ┌──────────┐  ┌──────────┐           │  │
│  │  │  Motor   │  │ Pressure │           │  │
│  │  │  Model   │──│  Model   │           │  │
│  │  └──────────┘  └──────────┘           │  │
│  │  ┌──────────┐  ┌──────────┐           │  │
│  │  │  Valve   │  │ Syringe  │           │  │
│  │  │  Model   │──│  Model   │           │  │
│  │  └──────────┘  └──────────┘           │  │
│  │  ┌──────────┐  ┌──────────┐           │  │
│  │  │   Air    │  │  Fault   │           │  │
│  │  │ Detector │  │ Injector │           │  │
│  │  └──────────┘  └──────────┘           │  │
│  └────────────────────────────────────────┘  │
│                 ▲                             │
│                 │ readPressure()              │
│                 │ readAirDetector()           │
│  ┌──────────────┴─────────────────────────┐  │
│  │       Safety Monitor Thread            │  │
│  │       (reads HAL independently)        │  │
│  └────────────────────────────────────────┘  │
│                                              │
│  ┌────────────────────────────────────────┐  │
│  │       gRPC Server                      │  │
│  │  (streams telemetry derived from HAL   │  │
│  │   readings to frontend — never touches │  │
│  │   HAL directly)                        │  │
│  └────────────────────────────────────────┘  │
└──────────────────────────────────────────────┘
          │ gRPC stream
          ▼
┌──────────────────────────────────────────────┐
│  Frontend Process (sees telemetry only)      │
└──────────────────────────────────────────────┘
```

The `SimulatedHal` class implements `IHalInterface`. The control loop and safety monitor both hold a `std::shared_ptr<SimulatedHal>` reference. The control loop calls `tick()` to advance the physics; both threads call read methods for sensor values. The gRPC server never accesses the HAL — it reads from the telemetry broadcast populated by the control loop.

## Device Models

### Motor Model

Simulates a DC motor driving a syringe plunger via a lead screw mechanism.

**Behavior:**
- Accepts a commanded RPM (from PID controller)
- Actual RPM approaches commanded RPM with first-order exponential lag (simulates motor inertia and acceleration limits)
- Motor RPM translates linearly to flow rate via a constant (mL/s per RPM)

**Physics:**
```
actual_rpm += (commanded_rpm - actual_rpm) * (dt / time_constant)
flow_rate = actual_rpm * flow_per_rpm
```

**Parameters:**

| Parameter | Default | Unit | Description |
|-----------|---------|------|-------------|
| `timeConstant` | 50.0 | ms | Time to reach 63% of a step change. Models motor + mechanical inertia. |
| `flowPerRpm` | 0.01 | mL/s per RPM | Conversion factor. 400 RPM = 4.0 mL/s. Represents lead screw pitch + syringe cross-section. |
| `maxRpm` | 1500.0 | RPM | Physical limit. PID output is clamped to this. |
| `minRpm` | 0.0 | RPM | Cannot spin backwards in MVP. |

**State:**
```cpp
struct MotorModel {
    double commandedRpm = 0.0;
    double actualRpm = 0.0;
    double timeConstantS;       // seconds (converted from ms at construction)
    double flowPerRpm;
    double maxRpm;

    void tick(double dt);       // advance actual_rpm toward commanded_rpm
    double flowRate() const;    // actual_rpm * flow_per_rpm
};
```

**Edge cases:**
- `commandedRpm = 0`: motor decelerates to stop following the same time constant
- `commandedRpm` changes mid-tick: uses the new value immediately
- Emergency stop sets `commandedRpm = 0` AND `actualRpm = 0` (instant stop, bypasses time constant — this is the magnetic brake)

### Pressure Model

Simulates fluid pressure in the tubing between the syringe and the patient connection.

**Behavior:**
- Pressure is a function of flow rate and tubing resistance (Poiseuille's law, linearized)
- Includes a baseline static pressure (column height)
- Responds instantly to flow rate changes (fluid is incompressible at these scales)

**Physics:**
```
pressure = (flow_rate * tubing_resistance) + baseline_pressure
```

Where `flow_rate` comes from the motor model's current actual RPM.

**Parameters:**

| Parameter | Default | Unit | Description |
|-----------|---------|------|-------------|
| `tubingResistance` | 50.0 | psi per mL/s | Models tubing length, diameter, and fluid viscosity. 4 mL/s → 200 psi dynamic. |
| `baselinePressure` | 10.0 | psi | Static pressure (hydrostatic head). Present even at zero flow. |

**State:**
```cpp
struct PressureModel {
    double tubingResistance;
    double baselinePressure;
    // No internal state — pressure is computed from current flow rate each tick

    double compute(double flowRate) const;
};
```

**Why linear is sufficient for MVP:**
- Real Poiseuille flow is also approximately linear for laminar flow in rigid tubing
- Nonlinear effects (turbulence, syringe friction, compliant tubing) are Phase 2 enhancements
- The linear model produces realistic-looking pressure curves that respond correctly to flow rate changes

### Valve Model

Simulates two solenoid valves controlling fluid path selection (contrast channel, saline channel).

**Behavior:**
- Binary state: `Open` or `Closed` (no partial opening in MVP)
- Instantaneous actuation (real solenoid valves actuate in ~10 ms, but at 2 ms tick rate the difference is negligible)
- Flow only occurs through an open valve. If both valves are closed, effective flow rate is 0 regardless of motor RPM.
- Motor can spin with valves closed — pressure would build. The safety monitor catches this via overpressure.

**State:**
```cpp
enum class ValveState { Open, Closed };

struct ValveModel {
    ValveState contrastValve = ValveState::Closed;
    ValveState salineValve = ValveState::Closed;

    bool isOpen(FluidChannel channel) const;
    void set(FluidChannel channel, ValveState state);
    void closeAll();
};
```

**Constraints:**
- Only one valve should be open at a time during injection (the state machine enforces this by opening the correct valve for the current phase's fluid type)
- Both valves closed = safe state (emergency stop closes both)
- Opening both simultaneously is allowed by the model but would be a protocol error

### Syringe Model

Simulates two syringe barrels (contrast and saline) tracking remaining fluid volume.

**Behavior:**
- Each barrel has a capacity and a current remaining volume
- Volume decreases at the actual flow rate when the corresponding valve is open
- Volume cannot go below 0 (syringe is physically empty)

**Physics:**
```
remaining_volume -= actual_flow_rate * dt    (when valve for this barrel is open)
remaining_volume = max(remaining_volume, 0)
```

**Parameters:**

| Parameter | Default | Unit | Description |
|-----------|---------|------|-------------|
| `contrastCapacity` | 100.0 | mL | Loaded contrast volume |
| `salineCapacity` | 50.0 | mL | Loaded saline volume |

**State:**
```cpp
struct SyringeModel {
    double contrastRemaining;    // mL
    double salineRemaining;      // mL
    double contrastCapacity;     // mL (for reset/reload)
    double salineCapacity;       // mL

    void drain(FluidChannel channel, double flowRate, double dt);
    void resetToFull();
};
```

**Edge cases:**
- If remaining volume reaches 0 during injection, the model reports 0 and continues computing flow (the safety monitor or state machine should detect this as a fault)
- Reloading syringes (resetting to capacity) only happens on state machine reset to `Idle`

### Air Detector Model

Simulates an ultrasonic air-in-line detector.

**Behavior:**
- Returns a boolean: `true` = air detected, `false` = no air
- Default state is `false` (no air)
- Air is only present when explicitly injected via the fault injection system
- Once injected, air persists until cleared (by fault reset)

**State:**
```cpp
struct AirDetectorModel {
    std::atomic<bool> airPresent{false};   // atomic for lock-free reads by safety monitor
};
```

**Why so simple:**
- Real air detectors are binary sensors (air/no-air threshold on ultrasonic signal)
- The interesting behavior is in the safety monitor's *response* to air detection, not in the sensor model itself
- Phase 2/3 could add probabilistic detection (missed bubbles, false positives) for more realistic training

## Fault Injection System

The fault injection system allows test scenarios and the UI's fault injection feature to introduce simulated failures. Faults are triggered via the `InjectFault` gRPC RPC, which the backend's gRPC handler routes to the state machine, which calls `SimulatedHal::injectFault()`.

**Supported faults:** The `SimulatedFault` struct and `FaultType` enum are defined in **04-schema-and-contracts.md, Section 4** (HAL Interface Contract). Five fault types: Overpressure, AirBubble, MotorStall, PartialOcclusion, TimingDelay.

**How faults modify models:**

| Fault | Affected Model | Mechanism |
|-------|---------------|-----------|
| `Overpressure` | PressureModel | Overrides computed pressure with `targetPsi`. Persists until cleared. |
| `AirBubble` | AirDetectorModel | Sets `airPresent = true`. Persists until cleared. |
| `MotorStall` | MotorModel | Clamps `actualRpm` to `maxRpm` regardless of commanded RPM. Motor appears to stall or underperform. |
| `PartialOcclusion` | PressureModel | Multiplies `tubingResistance`, causing gradual pressure rise at the same flow rate. |
| `TimingDelay` | Control Loop (external) | Not a model change — injects a `std::this_thread::sleep_for()` into the control loop to trigger the safety monitor's timing violation check. |

**Fault lifecycle:**
1. Frontend sends `InjectFault` gRPC RPC with fault type and parameters
2. gRPC handler enqueues fault command to state machine
3. State machine calls `SimulatedHal::injectFault()`, modifies the relevant model's state
4. Next `tick()` call produces sensor values reflecting the fault
5. Safety monitor detects the fault condition on its next check
6. Fault persists until `clearFaults()` is called (triggered by state machine Reset)

## Tick Sequencing

The `tick(dt)` method advances all models by `dt` seconds. Called by the control loop thread once per tick (default every 2 ms, so `dt = 0.002`).

**Order of operations within `tick()`:**

```
1. Motor model:    update actual_rpm (approach commanded_rpm)
2. Flow rate:      compute from motor actual_rpm × flow_per_rpm
3. Active valve:   determine which barrel is being drained
4. Syringe model:  decrement remaining volume by flow_rate × dt
5. Pressure model: compute pressure from flow_rate (or override if fault active)
6. Air detector:   return current state (unchanged unless fault injected)
```

**Why this order matters:**
- Motor RPM must update before flow rate is computed (motor drives flow)
- Flow rate must be computed before syringe volume is decremented (flow determines how much volume is consumed)
- Pressure must be computed after flow rate (pressure is a function of flow)

## Thread Safety

The `SimulatedHal` is accessed by two threads simultaneously:
- **Control loop thread:** calls `tick()`, `setMotorRpm()`, `setValve()`, reads all sensors
- **Safety monitor thread:** reads `readPressure()`, `readMotorRpm()`, `readAirDetector()`, calls `emergencyStop()`

The gRPC server threads do **not** access the HAL. They read telemetry from a broadcast buffer populated by the control loop — an important boundary that keeps the gRPC layer from introducing lock contention on the real-time path.

**Approach:**

`SimulatedHal` implements `IHalInterface` (defined in **04-schema-and-contracts.md, Section 4**). Key implementation details beyond the interface contract:

```cpp
class SimulatedHal : public IHalInterface {
    // ... IHalInterface overrides (see 04 for full method signatures) ...

private:
    mutable std::mutex stateMutex_;          // protects all model state
    MotorModel motor_;
    PressureModel pressure_;
    ValveModel valves_;
    SyringeModel syringes_;
    double currentFlowRate_ = 0.0;           // cached from last tick
    double currentPressure_ = 0.0;           // cached from last tick

    std::atomic<bool> airPresent_{false};    // atomic for lock-free safety reads

    mutable std::mutex faultMutex_;          // protects fault state
    FaultOverrides faultOverrides_;
};
```

**Lock contention analysis:**
- Control loop holds `stateMutex_` for ~10 µs per tick (tick computation + sensor reads)
- Safety monitor holds `stateMutex_` for ~1 µs per check (read 3 values)
- At 2 ms and 1 ms tick rates respectively, overlap probability is <1%
- If the safety monitor finds the mutex locked, it blocks for at most 10 µs — negligible vs. its 1 ms tick
- gRPC threads never contend on this mutex (they read from the telemetry broadcast, not the HAL)

**The `airPresent_` field is `std::atomic<bool>`** specifically so the safety monitor's most critical check (air detection) never needs to acquire the mutex. Even if the mutex were poisoned or the control loop were hung, the safety monitor can still detect air.

## Configuration

All model parameters are loaded from the backend's JSON config file — specifically the `hal` and `syringe` sections. See **04-schema-and-contracts.md, Section 5** for the full schema, field constraints, ranges, and defaults.

No separate config file — the simulated devices are a component of the backend process, not a separate service.

## Typical Simulation Values

For derived constants and typical steady-state values (RPM, pressure, headroom, phase duration) computed from the default config, see **04-schema-and-contracts.md, Section 8: Derived Constants & Relationships**.

Key insight for this layer: at default parameters, normal operation leaves realistic headroom — pressure is well below the 325 psi limit, but a partial occlusion (3× resistance) would push pressure to 610 psi, triggering overpressure.

## Future Extensions (Phase 2+)

The `IHalInterface` abstract class makes these extensions straightforward — they change the `SimulatedHal` implementation without affecting the control loop or safety monitor. The gRPC interface is also unaffected — telemetry frames carry the same fields regardless of the physics model behind them.

| Extension | What Changes |
|-----------|-------------|
| Nonlinear pressure model | Quadratic or lookup-table pressure vs. flow. `PressureModel` internals change. |
| Temperature-dependent viscosity | `tubingResistance` becomes a function of temperature. New parameter. |
| Multiple syringe sizes | `flowPerRpm` varies by syringe diameter. Configurable per barrel. |
| Dual-syringe mixing | Both valves open simultaneously. Flow splits proportionally. `tick()` drains both barrels. |
| Stochastic sensor noise | Add Gaussian noise to pressure and RPM readings. Configurable SNR. Uses `<random>`. |
| Real hardware adapter | New class implementing `IHalInterface` that talks to actual motor controllers via serial/CAN. Zero changes to control loop or gRPC layer. |
