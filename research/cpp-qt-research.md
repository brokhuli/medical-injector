# C++ Backend / Qt Frontend Research

## How Does a C++ Backend Talk to a Qt Frontend?

In a Qt application, the C++ backend and Qt frontend aren't separate layers in the way they are in web apps — Qt is a C++ framework, so they communicate directly through objects, signals, and slots.

### 1. Signals and Slots (Core Mechanism)

The primary communication mechanism in Qt. The backend emits a signal; the frontend listens with a slot.

```cpp
// Backend
class Backend : public QObject {
    Q_OBJECT
signals:
    void dataChanged(QString data);
};

// Frontend connection
connect(backend, &Backend::dataChanged,
        ui->label, &QLabel::setText);
```

When `dataChanged` is emitted, the UI updates automatically.

---

### 2. Qt Widgets (C++ UI)

When using Qt Widgets (`QMainWindow`, `QPushButton`, etc.), the UI and backend are often tightly coupled in C++. You directly call methods or connect signals.

**Flow:**
```
Button click → signal → backend function
Backend updates → emits signal → UI updates
```

---

### 3. QML Frontend + C++ Backend

The modern and most common setup. Communication is more structured.

**Step 1: Expose C++ to QML**
```cpp
Backend backend;
engine.rootContext()->setContextProperty("backend", &backend);
```

**Step 2: Call backend from QML**
```qml
Button {
    text: "Click"
    onClicked: backend.doSomething()
}
```

**Step 3: Send data back via signals**
```qml
Connections {
    target: backend
    function onDataChanged(data) {
        label.text = data
    }
}
```

---

### 4. Properties (Reactive Binding in QML)

Qt uses property bindings for automatic UI updates.

**C++:**
```cpp
Q_PROPERTY(QString message READ message NOTIFY messageChanged)
```

**QML:**
```qml
Text {
    text: backend.message
}
```

When the backend updates `message`, the UI updates automatically.

---

### 5. Model/View (Structured Data)

For lists and tables, use `QAbstractListModel` or `QAbstractTableModel` bound to QML views (`ListView`, `TableView`). This is how apps handle dynamic datasets cleanly.

---

### 6. Threads (Async Backend)

If the backend runs in another thread:
- Use signals/slots (thread-safe in Qt)
- Never access UI directly from worker threads

---

### Mental Model

| Component | Role |
|---|---|
| Backend | Logic (C++ classes) |
| Frontend | UI (Widgets or QML) |
| Bridge | Signals, slots, and properties |

Not HTTP, not JSON — just in-process object communication.

---

## Do Backend and Frontend Run in the Same Application Domain?

**Yes** — by default, the C++ backend and frontend (Widgets or QML) run in the same process and share the same memory space.

- They are part of one executable
- They share the same heap and objects
- Communication is direct — no networking layer

Unlike web apps: no HTTP, no serialization (JSON, etc.) — just in-memory object interaction.

### How Communication Works Within One Process

**Direct calls (synchronous):**
```cpp
backend->doSomething();
```

**Signals/slots (sync or async depending on threads):**
```cpp
emit dataChanged("hello");
```

Qt decides: same thread → direct call; different thread → queued via event loop.

### Threading Within One Process

Even with threads, everything is still in the same process. Communication uses queued signals/slots (thread-safe).

**Critical rule:** UI must run in the main (GUI) thread; backend workers can run elsewhere.

### Exception: Explicit Process Separation

Qt can separate backend and frontend as distinct processes, but only when explicitly designed that way (e.g., via `QLocalSocket`, `QProcess`, REST, or WebSockets). In those cases, serialization (JSON, protobuf, etc.) is required.

### Comparison

| Architecture | Backend ↔ Frontend |
|---|---|
| Qt (default) | Same process, direct calls |
| Web app | Different processes, HTTP |
| Electron | Separate processes, IPC |

---

## Does the Frontend Risk Stealing Processing from the Backend?

**Yes, but only if structured poorly.** Qt itself doesn't steal processing — it's about how threads are used.

### The Core Issue: Shared Main Thread

By default, the UI runs on the main thread. If backend logic also runs there, they compete.

```cpp
void Backend::heavyWork() {
    // long computation — called from UI thread → UI freezes
}
```

The UI freezes because the event loop is blocked.

### What "Stealing Processing" Actually Looks Like

- UI becomes unresponsive (lag, freeze)
- Backend tasks block rendering and input
- Too many UI updates slow the backend

### Proper Design

**1. Move heavy backend work off the UI thread**

```cpp
worker->moveToThread(thread);
```

**2. Communicate via signals/slots**

Backend thread emits a signal; Qt automatically queues it to the UI thread.

**3. Keep UI lightweight**

The frontend should render, handle input, and display data — not do heavy computation, block in loops, or run long synchronous tasks.

### What Happens Under the Hood

Qt uses an event loop: the UI thread processes paint, click, and update events. If the event loop is blocked, everything pauses.

> **The real risk is blocking the event loop** — not the frontend stealing CPU cycles.

**Analogy:** The UI thread is a receptionist; backend threads are workers. If the receptionist starts doing heavy work, nobody can check in (UI freezes).

### Best Practices

- Always move heavy work off the main thread
- Use signals/slots for cross-thread communication
- Batch UI updates — don't spam signals
- Avoid tight loops in the UI thread

---

## Ensuring Frontend Responsiveness with a Deterministic Backend

For an instrument controller requiring deterministic behavior, the key is to completely decouple timing-critical backend work from the UI thread, and design communication so the UI can lag without affecting control.

**Core principle:** the backend can be deterministic regardless of UI responsiveness — as long as they don't share execution.

```
Backend = time-critical, predictable
Frontend = best-effort, responsive but non-critical
```

### 1. Put the Backend in Its Own Thread

Never run control logic in the UI thread.

```cpp
QThread* controlThread = new QThread;
Controller* controller = new Controller;

controller->moveToThread(controlThread);
controlThread->start();
```

The controller now runs independently — the UI cannot block it.

### 2. Avoid Qt Event Loop for Hard Timing

`QTimer` is not precise enough under load. Use instead:
- Hardware timers
- Real-time OS features
- High-resolution timers (`std::chrono`, platform APIs)

> Qt is not a real-time framework — do not depend on it for strict timing guarantees.

### 3. Never Let UI Call Blocking Backend Functions

**Bad:**
```cpp
controller->startMeasurement();  // if it blocks → UI freeze
```

**Good:**
```cpp
// UI emits a signal; backend handles it asynchronously
connect(ui, &UI::startClicked,
        controller, &Controller::startMeasurement);
```

### 4. Use Queued Signals (Decoupled Communication)

Across threads, Qt automatically uses queued connections. This ensures no blocking and no priority inversion.

### 5. Throttle UI Updates

A common hidden problem: the backend floods the UI with updates, overloading the event loop.

```cpp
if (elapsed > 50ms) emit updateUI(data);
```

Send updates at a limited rate (10–30 Hz) and aggregate data before emitting signals.

### 6. Use Lock-Free or Minimal-Lock Design

For deterministic systems, avoid heavy mutex contention. Prefer:
- Lock-free queues
- Ring buffers
- Atomic variables

This prevents UI interaction from delaying backend timing.

### 7. Never Access UI from Backend Thread

UI updates must happen in the main thread only. Violating this causes crashes and undefined behavior.

### 8. Consider Process Separation (Critical Systems)

If determinism is truly critical, run the backend as a separate process.

```
Backend = real-time / high-priority process
UI      = normal Qt app
```

Communicate via sockets, shared memory, or IPC. This gives OS-level isolation — the UI can freeze or crash without affecting control.

### Mental Architecture

**Threaded (minimum isolation):**
```
[ UI Thread ]
     ↓ (signals)
─────────────────────
[ Controller Thread ] ← deterministic loop
     ↓
[ Hardware / Instrument ]
```

**Process-separated (ideal for critical systems):**
```
[ Qt UI Process ]  ← can lag safely
     ↓ IPC
[ Control Process ] ← deterministic
     ↓
[ Hardware ]
```

### Key Takeaways

- UI becomes unresponsive only if the main thread is blocked
- Determinism requires at minimum a dedicated thread; ideally a separate process
- Throttle communication to avoid UI overload
- Never tie control timing to Qt's event loop

---

## Does gRPC Allow Frontend and Backend in Different Application Domains?

**Yes** — that is one of the primary reasons to use gRPC. It cleanly enables separating backend and frontend into different application domains (processes, runtimes, even machines).

```
[ Frontend App ] ←→ [ Backend Service ]
    (client)              (server)
```

They can be separate processes, separate languages, separate machines, or separate OS environments. The only contract between them is the `.proto` interface.

### How gRPC Enables This

**1. Strict service boundary**

```protobuf
service InjectorService {
    rpc StartInjection(StartRequest) returns (Status);
    rpc StreamTelemetry(Empty) returns (stream Telemetry);
}
```

This generates a C++ client, Python server, Go client, etc. — all from the same contract.

**2. Network-transparent communication**

The frontend doesn't care where the backend runs:

```cpp
grpc::CreateChannel("localhost:50051", ...)  // local
grpc::CreateChannel("192.168.1.10:50051", ...)  // remote — no architectural rewrite needed
```

**3. Process isolation**

Instead of UI + logic + control in one process:

```
[ UI Process ]     [ Backend Process ]
      │                    │
      └────── gRPC ─────────┘
```

Benefits: crashes are isolated, memory is isolated, easier debugging, security boundaries.

### In a Commercial Medical Injector System

```
[ UI Application ]
        │
      gRPC
        │
[ Application Service ]
        │
      (IPC)
        │
[ Real-Time Controller ]
```

gRPC separates UI from backend, but not the real-time layer. The real-time boundary uses shared memory, queues, or RT-safe IPC.

### Why This Separation Is Powerful

| Benefit | Detail |
|---|---|
| Independent development | UI team and backend team work separately |
| Replaceable components | Swap Qt UI → Web UI without touching backend logic |
| Deployment flexibility | UI on workstation, backend on device — same gRPC interface |
| Fault containment | UI crash does not affect backend; critical in medical systems |

### What gRPC Does NOT Provide Automatically

- **Real-time guarantees** — still subject to network latency and OS scheduling
- **Safety** — validation layers, command gating, and safety monitors still required
- **OS isolation** — requires process design, CPU isolation, and RTOS for the control layer

### Comparison of Communication Approaches

| Approach | Isolation | Performance | Streaming |
|---|---|---|---|
| In-process calls | None | Fastest | N/A |
| REST | Full | Less efficient | No native |
| gRPC | Full | High | Native |

### Key Insight

gRPC enforces architectural discipline: APIs must be defined explicitly, you cannot reach into another module's memory, and everything is intentional.

**Bottom line:** gRPC is one of the best tools for enforcing clean system boundaries across processes, machines, and languages.
