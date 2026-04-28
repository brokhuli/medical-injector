# Medical Contrast Power Injector - Simulator

## Author

Built by **Steve Ullom**, software architect with an interest in real-time control systems, safety-critical software, and the intersection of embedded engineering and clinical workflow.

---

## What it is

An educational and research simulator for a dual-barrel medical contrast power injector. This application models the real-time control, safety monitoring, and clinical workflow of a device used in CT and angiography suites to deliver contrast media and saline at precise flow rates and volumes.

> **Not a medical device.** This software is for educational, training, and research use only. It is not intended for use in patient care, diagnosis, or treatment.

---

## What It Does

The simulator lets you author a multi-phase injection protocol — mixing contrast and saline, with per-phase flow rate, volume, and pressure limits — and then executes that protocol through a simulated fluid path. You see exactly what a clinician would see: live flow and pressure traces, per-phase volume progress, remaining syringe volume, state transitions, and any faults that arise.

---

## How It Works

Two processes communicate over gRPC on the local machine:

- **Backend** — a C++ real-time control engine. A 2 ms PID control loop drives a simulated motor, a separate 1 ms safety monitor watches for overpressure, air, and timing violations, and a state machine sequences the phases of the protocol.

- **Frontend** — a Qt6/QML clinical UI. Gauges, a timeline chart, a protocol editor, and control buttons (Arm / Start / Pause / Resume / Reset / E-Stop) give you the full operator experience.

Under the hood, the backend simulates the physics of the fluid path: a first-order motor lag, per-fluid tubing resistance (contrast is more viscous than saline), and a compliance lag that makes pressure rise and fall smoothly rather than jumping. End-of-phase deceleration is predictive — the controller looks ahead and begins decelerating at exactly the right moment to hit the target volume without overshooting the pressure limit.

---

## Key Features

- **Multi-phase protocols** — chain contrast and saline phases with independent flow, volume, and pressure settings.
- **Live telemetry** — real-time flow rate, pressure, motor RPM, and volume progress streamed from the control loop.
- **Fault injection** — deliberately trigger overpressure, air detection, or motor faults to practice the acknowledge-and-reset workflow.
- **Independent safety monitor** — runs on its own thread and can halt injection even if the main control loop stalls.
- **Deterministic physics** — the same protocol produces the same trace every time, so behavior is repeatable and comparable.

---

## Safety Note

This simulator deliberately mimics the feel and workflow of a clinical power injector, but it has no connection to any real fluid delivery hardware. Do not use it to make clinical decisions, validate real protocols, or train in a way that substitutes for manufacturer-provided training on actual equipment.
