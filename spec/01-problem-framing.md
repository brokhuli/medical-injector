# Problem Statement

## Problem

CT and MRI contrast injection procedures depend on proprietary, closed-source power injector software that is expensive, tightly coupled to vendor hardware, and opaque in its control logic. Radiologists, technologists, and biomedical engineers have no way to study, customize, or improve the injection workflow without going through vendor-controlled update cycles. This limits training opportunities, slows protocol optimization, and creates a knowledge gap around how these safety-critical systems actually work.

This project builds an open, software-only simulation of a medical contrast power injector — including the real-time control layer, safety monitoring, and clinical UI — that can run on commodity hardware. The goal is not to replace a certified medical device, but to create a faithful reference implementation for education, research, protocol development, and software architecture exploration.

## Target Users & Personas

### 1. Radiology Technologist (RT) — "Jordan"
- **Role:** Operates injectors daily during CT/MRI procedures
- **Goal:** Practice injection protocols, understand system behavior under edge cases (air detection, pressure limits), and train on new protocols without tying up a real injector
- **Pain today:** Training happens on live equipment during downtime; mistakes during learning carry real patient risk

### 2. Biomedical / Clinical Engineer — "Sam"
- **Role:** Maintains and troubleshoots injector hardware and software in a hospital setting
- **Goal:** Understand the control architecture (state machines, PID loops, safety interlocks) to diagnose issues faster and evaluate vendor claims
- **Pain today:** Vendor systems are black boxes; service manuals describe *what* but not *why*

### 3. Medical Device Software Developer — "Alex"
- **Role:** Builds or contributes to safety-critical embedded/real-time software
- **Goal:** Use the project as a reference architecture for IEC 62304-style design patterns — separation of real-time control from UI, safety monitors, deterministic scheduling
- **Pain today:** Few open-source examples of medical device control software exist; most learning happens behind corporate walls

### 4. Radiology Resident / Medical Physicist — "Dr. Patel"
- **Role:** Learning imaging physics and contrast injection pharmacokinetics
- **Goal:** Visualize how flow rate, pressure, and timing interact during multi-phase injection protocols; experiment with protocol parameters
- **Pain today:** Textbook descriptions are abstract; no hands-on way to see cause-and-effect in injection dynamics

### 5. QA / Regulatory Engineer — "Morgan"
- **Role:** Writes or reviews verification & validation documentation for medical devices
- **Goal:** Study a concrete example of how safety requirements (pressure limits, air detection, watchdog timers) map to software architecture and test cases
- **Pain today:** Regulatory frameworks (IEC 62304, FDA guidance) are process-heavy but example-light

## Core Use Cases (Top 5)

### UC-1: Configure and Execute a Multi-Phase Injection Protocol
The user defines a contrast injection protocol with multiple phases (e.g., contrast bolus → saline flush), specifying flow rate, volume, and pressure limits per phase. The system executes the protocol in simulated real-time, driving virtual motors and reading simulated sensors through a closed-loop control system.

### UC-2: Monitor Injection in Real Time
During an active injection, the user observes a live dashboard showing flow rate, pressure, volume delivered, and injection phase — updated at clinically realistic intervals (~100–500 ms). The display reflects the actual state of the simulated control loop, not just a scripted animation.

### UC-3: Trigger and Observe Safety Interventions
The user (or the simulation) introduces a fault condition — overpressure, air detection, motor anomaly, or missed timing deadline. The independent safety monitor detects the condition and autonomously halts injection, closes valves, and raises an alarm. The user can inspect the event timeline to understand what happened and why.

### UC-4: Inspect System Architecture and State
The user can view the current state machine state (Idle → Armed → Injecting → Flush → Completed → Fault), active control loop parameters (PID error, motor output), and the communication flow between the UI layer and the real-time controller. This supports learning and debugging.

### UC-5: Experiment with Protocol Parameters
The user modifies protocol variables (flow rate curves, volume, pressure thresholds, acceleration ramps) and immediately sees the effect on simulated injection behavior. This supports training, protocol optimization research, and "what-if" exploration without clinical risk.

## Success Metrics

| Metric | Target | Why It Matters |
|--------|--------|----------------|
| **Control loop timing fidelity** | Simulated loop executes at ≤ 5 ms intervals with < 1 ms jitter (on dedicated core) | Validates that the architecture faithfully models real-time behavior |
| **Safety response latency** | Fault-to-halt in < 10 ms (simulated time) | Demonstrates that the safety monitor can override the control loop independently |
| **Protocol accuracy** | Delivered volume within ± 2% of programmed volume across all phases | Shows closed-loop control is working, not just open-loop playback |
| **UI responsiveness** | Dashboard updates within 500 ms of control loop state changes | Confirms the UI ↔ controller separation works without blocking real-time operations |
| **Architectural clarity** | A new developer can identify all 7 layers (HAL, RTOS, control loop, state machine, safety monitor, command interface, data logging) in the codebase within 30 minutes | The project succeeds as a reference implementation only if it's understandable |
| **Fault coverage** | System correctly detects and responds to all defined fault classes (overpressure, air, motor fault, timing violation) | Safety architecture is proven, not just described |
