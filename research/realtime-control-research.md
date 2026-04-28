# Real-Time Control Architecture Research

## System Overview: Two Processors, Not One

A key architectural insight: commercial medical injector systems are typically split into two domains:

| Domain | Responsibilities | OS |
|---|---|---|
| **Real-time controller** | Injection execution, motor/sensor I/O, deterministic safety | RTOS |
| **Application/UI processor** | UI, workflows, networking | Linux (non-RT) |

They communicate via shared memory, IPC, or serial (CAN, UART, etc.).

> This separation is intentional: UI bugs cannot break injection safety.

---

## Real-Time Control Architecture

### Layer 1: Hardware Abstraction Layer (HAL)

Lowest level before hardware. No business logic — only clean interfaces.

**Responsibilities:**
- Read sensors (pressure, position, air detection)
- Control actuators (motor drivers, valves)
- Normalize hardware differences

**Example API:**
```
read_pressure_sensor()  → returns mmHg
set_motor_speed(rpm)
open_valve(channel)
```

---

### Layer 2: Device Drivers / RTOS

**Common RTOS options:** VxWorks, QNX, FreeRTOS, or custom

**Handles:**
- Interrupts (e.g., pressure spikes)
- Scheduling with strict timing guarantees
- Low-level timing (millisecond or microsecond precision)

---

### Layer 3: Control Loops

The heart of the system — where injection is physically controlled via closed-loop PID control.

**Example loop (runs every few ms):**
```
target_flow_rate = 4 mL/s
actual_flow_rate = sensor_reading()

error = target - actual
motor_adjustment = PID(error)

apply_motor_adjustment()
```

**Controls:**
- Flow rate
- Pressure limits
- Acceleration/deceleration curves

**Requirements:** deterministic, jitter-free, extremely well-tested.

---

### Layer 4: State Machine

Orchestrates injection phases via a finite state machine.

**States:** Idle → Armed → Injecting → Paused → Completed → Fault

**Transitions triggered by:** time, sensor input, or UI commands.

---

### Layer 5: Safety Monitor (Independent Watchdog)

Runs in parallel with main control logic and can **override everything**.

**Checks:**
- Pressure exceeds threshold
- Air detected
- Motor behaving unexpectedly
- Software missed timing deadline

**On trigger:** immediately stop motor → close valves → raise alarm

**Implementation:** independent task/thread, hardware watchdog timers, redundant checks.

---

### Layer 6: Command Interface (Bridge to UI)

**Receives:**
- Start/stop commands
- Protocol parameters
- Configuration updates

**Sends:**
- Real-time telemetry
- Status updates
- Error states

**Typical message format:**
```json
{
  "command": "START_INJECTION",
  "flow_rate": 4.0,
  "volume": 80
}
```

---

### Layer 7: Data Logging (Real-Time Safe)

Logs without disrupting the control loop. Logging must **never** block real-time execution.

**Mechanisms:** ring buffers, async logging threads.

**Logs:** pressure vs. time, flow rate vs. time, events/errors.

---

## Timing Model

| Task | Frequency |
|---|---|
| Control loop | Every 1–5 ms |
| Safety checks | Equal or faster than control loop |
| UI updates | Every 100–500 ms |

Real-time tasks always have highest scheduling priority.

---

## Architecture Diagram

```
┌──────────────────────────────┐
│     UI / Application CPU     │
│   (Linux / Qt / Workflow)    │
└─────────────┬────────────────┘
              │ IPC
┌─────────────▼────────────────┐
│     Real-Time Controller     │
│         (RTOS-based)         │
│                              │
│  ┌────────────────────────┐  │
│  │   Command Interface    │  │
│  └──────────┬─────────────┘  │
│             ▼                │
│  ┌────────────────────────┐  │
│  │     State Machine      │  │
│  └──────────┬─────────────┘  │
│             ▼                │
│  ┌────────────────────────┐  │
│  │     Control Loops      │  │
│  └──────────┬─────────────┘  │
│             ▼                │
│  ┌────────────────────────┐  │
│  │ Hardware Abstraction   │  │
│  └──────────┬─────────────┘  │
│             ▼                │
│   Motors / Sensors / Valves  │
│                              │
│  ┌────────────────────────┐  │
│  │    Safety Monitor      │◄─┘  (can override all)
│  └────────────────────────┘
└──────────────────────────────┘
```

---

## Key Design Principles

**Determinism above all else**
- No garbage collection pauses
- No unpredictable latency
- Fixed execution timing

**Fail-safe by design**
- If anything is uncertain → stop injection
- Safe state is always defined

**Loose coupling between layers**
- Control loop is independent of UI
- HAL isolates hardware changes

**Redundancy**
- Software checks + hardware checks
- Watchdog timers throughout

---

## Comparison: Injection Control vs. Typical Backend Systems

| Concern | Backend SaaS | Injection Control |
|---|---|---|
| Consistency | Eventual | Immediate correctness |
| Failures | Can retry | Must prevent |
| Scaling | Horizontal | Must be deterministic |
| Latency | ~100 ms acceptable | Must be bounded (ms) |

---

## CPU Core Dedication

### Would Injection Software "Burn" a CPU Core?

**Short answer: yes** — effectively one CPU core is reserved for the real-time control loop, though not wastefully.

### Two Common Designs

**1. Dedicated core (modern systems)**

On multi-core CPUs:
- Core 0 → RTOS / real-time control loop
- Core 1+ → UI, logging, networking

The RT core runs only critical threads, avoids Linux/UI interference, and uses CPU affinity + isolation.

**2. Single core with strict priorities (older/simpler systems)**

The RTOS scheduler runs everything; the control loop has highest priority; other tasks only run in leftover time.

**Example (2 ms cycle):**
```
Every 2 ms:
  run control loop  (~200 µs)
  remaining ~1.8 ms:
    logging, comms, background tasks
```

### Busy-Wait vs. Scheduled Execution

Not typical:
```cpp
while (true) {
    // spin constantly at 100% CPU  ← wasteful
}
```

Typical pattern:
```
loop every 2 ms:
  wake up (timer interrupt)
  run control logic
  sleep until next cycle
```

Actual CPU usage: **5–20% utilization**, but **100% reserved priority**.

### Why Dedicate a Core?

Jitter is the real enemy. Even tiny delays cause:
- Motor control instability
- Flow rate deviation
- Safety margin shrinkage

Sources of jitter: cache misses from other processes, interrupt storms, OS scheduling delays.

From a regulatory standpoint (FDA, IEC 62304), a dedicated core makes certification significantly easier — worst-case execution time (WCET) must be bounded and provable.

### Realistic Numbers (Example System)

| Metric | Value |
|---|---|
| Control loop frequency | 500 Hz (every 2 ms) |
| Execution time | 100–400 µs |
| CPU usage | ~5–20% |

The core is effectively off-limits to anything non-deterministic.

> **Analogy:** A runway reserved for emergency landings — even if not constantly used, you don't schedule random traffic on it.

---

## Implementing CPU Core Dedication in Code

The correct mental model: you dedicate a **thread**, and that thread runs your loop.

```
loop → thread → pinned to core
```

### C++ Example: Thread Pinning (Linux)

**Step 1: Put the loop in its own thread**

```cpp
#include <thread>

void control_loop() {
    while (true) {
        // timing-sensitive work
    }
}
```

**Step 2: Pin the thread to a specific core**

```cpp
#include <pthread.h>

void pin_to_core(std::thread& t, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_setaffinity_np(t.native_handle(),
                           sizeof(cpu_set_t),
                           &cpuset);
}
```

**Step 3: Apply it**

```cpp
std::thread t(control_loop);
pin_to_core(t, 3);  // dedicate core 3 to this loop
```

**Step 4 (optional): Add real-time scheduling**

```cpp
sched_param sch_params;
sch_params.sched_priority = 80;
pthread_setschedparam(t.native_handle(), SCHED_FIFO, &sch_params);
```

### Linux: Isolating a Core at the OS Level

For true isolation, add to boot parameters:
```
isolcpus=3
```

Then pin only your thread to that core. This is the closest to embedded real-time behavior achievable on a general-purpose OS.

### Windows: CPU Affinity

Via Task Manager (manual) or programmatically:
```powershell
Start-Process your_app -ProcessorAffinity 0x8  # 0x8 = core 3
```

Less deterministic than Linux — the OS can still interrupt the core and schedule kernel tasks.

### Common Mistake: Single-Thread Loops

```cpp
// Cannot dedicate cores here — it's one thread on one core
while (true) {
    loopA();
    loopB();
    loopC();
}
```

### Real-World Thread Pattern (Commercial Injector Style)

```
Thread 1 (core 0) → control loop     (highest priority)
Thread 2           → safety monitor
Thread 3           → communication
Thread 4           → UI              (often separate CPU)
```

### When to Pin Threads

| Loop Frequency | Recommendation |
|---|---|
| High (1–5 ms) | Dedicated thread + pin to core + real-time scheduling |
| Medium (10–100 ms) | Usually fine sharing a core |
| Low (100 ms+) | Don't bother pinning |

### Capability Note: AMD Ryzen 7 7735HS

The 8-core/16-thread Ryzen 7 7735HS supports CPU affinity pinning on both Linux and Windows. On Linux with `isolcpus` + `SCHED_FIFO`, you can achieve very close to deterministic behavior — sufficient for simulation and soft real-time systems. True hard real-time requires an RTOS or dedicated microcontroller.

| Approach | Achieves |
|---|---|
| Core pinning alone | Low contention, predictable ms-level timing |
| + `isolcpus` + `SCHED_FIFO` | Near-deterministic behavior |
| + PREEMPT_RT kernel | Closest to hard real-time on Linux |
| Dedicated MCU/RTOS | True hard real-time (as in medical devices) |
