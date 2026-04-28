# Frontend Architecture

The frontend is the application/UI processor — a separate Qt/QML process that connects to the backend via gRPC. It renders the clinical UI, sends commands as unary RPCs, and receives telemetry and events via gRPC server-streaming. It never touches the control loop or safety logic directly. If the frontend crashes, the backend continues running safely.

## High-Level Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                   Frontend Process (Qt/QML)                   │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                   QML Frontend                         │  │
│  │                                                        │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐  │  │
│  │  │   Protocol   │  │  Injection   │  │  Real-Time  │  │  │
│  │  │   Config     │  │  Control     │  │  Dashboard  │  │  │
│  │  │   Panel      │  │  Panel       │  │             │  │  │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬──────┘  │  │
│  │         │                 │                  │         │  │
│  │         └─────────────────┼──────────────────┘         │  │
│  │                           │                            │  │
│  │              Q_PROPERTY bindings (read)                │  │
│  │              Q_INVOKABLE calls (write)                 │  │
│  │                           │                            │  │
│  └───────────────────────────┼────────────────────────────┘  │
│                              │                               │
│  ┌───────────────────────────▼────────────────────────────┐  │
│  │            InjectorBridge (C++ QObject)                 │  │
│  │                                                        │  │
│  │  Q_PROPERTY: injectorState, pressure, flowRate, ...    │  │
│  │  Q_INVOKABLE: arm(), start(), pause(), loadProtocol()  │  │
│  │  NOTIFY signals: telemetryChanged, stateChanged, ...   │  │
│  └───────────────────────────┬────────────────────────────┘  │
│                              │                               │
│  ┌───────────────────────────▼────────────────────────────┐  │
│  │            gRPC Client Service                         │  │
│  │                                                        │  │
│  │  Channel: localhost:50051                               │  │
│  │  SendCommand()   StreamTelemetry()   StreamEvents()    │  │
│  │  LoadProtocol()  GetState()          ExportData()      │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────┼────────────────────────────────┘
                               │ gRPC (HTTP/2 + protobuf)
                               ▼
                    ┌─────────────────────┐
                    │  Backend Process    │
                    │  (C++ gRPC server)  │
                    └─────────────────────┘
```

## Component Boundaries

The frontend is a **Qt/QML application** in its own process. Three UI panels, one C++ bridge object (`InjectorBridge`), one gRPC client connection to the backend.

### gRPC Client Service

C++ class (not a QObject) that manages the gRPC channel and stub. Runs RPC calls on background threads to avoid blocking the Qt event loop.

**Responsibilities:**

- Create and maintain gRPC channel to `localhost:{port}`
- Send unary RPCs for commands (`SendCommand`, `LoadProtocol`, `InjectFault`)
- Manage server-streaming RPCs (`StreamTelemetry`, `StreamEvents`) on dedicated threads
- Request `GetState` snapshot on connect/reconnect to sync UI with backend state
- Detect disconnection and trigger reconnection with exponential backoff (1s, 2s, 4s, max 10s)
- Track connection state: `connecting`, `connected`, `disconnected`, `reconnecting`

**Does NOT:** Render anything, hold UI state, validate commands (that's the bridge's job for button enabling; the backend validates authoritatively).

### InjectorBridge (QML ↔ gRPC boundary)

A `QObject` subclass living on the main thread, exposed to QML as a context property. Translates between QML property bindings and gRPC calls/responses.

```cpp
class InjectorBridge : public QObject {
    Q_OBJECT

    // Connection
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)

    // Injector state
    Q_PROPERTY(QString injectorState READ injectorState NOTIFY injectorStateChanged)

    // Live telemetry (updated at ~20 Hz from gRPC stream)
    Q_PROPERTY(double targetFlowRate READ targetFlowRate NOTIFY telemetryChanged)
    Q_PROPERTY(double actualFlowRate READ actualFlowRate NOTIFY telemetryChanged)
    Q_PROPERTY(double pressure READ pressure NOTIFY telemetryChanged)
    Q_PROPERTY(double motorRpm READ motorRpm NOTIFY telemetryChanged)
    Q_PROPERTY(int phaseIndex READ phaseIndex NOTIFY telemetryChanged)
    Q_PROPERTY(double totalVolumeDelivered READ totalVolumeDelivered NOTIFY telemetryChanged)
    Q_PROPERTY(double totalProgrammedVolume READ totalProgrammedVolume NOTIFY telemetryChanged)
    Q_PROPERTY(double elapsedTime READ elapsedTime NOTIFY telemetryChanged)
    Q_PROPERTY(double contrastRemaining READ contrastRemaining NOTIFY telemetryChanged)
    Q_PROPERTY(double salineRemaining READ salineRemaining NOTIFY telemetryChanged)

    // Protocol
    Q_PROPERTY(QVariantList protocol READ protocol NOTIFY protocolChanged)
    Q_PROPERTY(QVariantList loadedProtocol READ loadedProtocol NOTIFY protocolLoaded)

    // Faults
    Q_PROPERTY(QVariantList activeFaults READ activeFaults NOTIFY faultsChanged)

    // Telemetry history (for timeline chart)
    Q_PROPERTY(QVariantList telemetryHistory READ telemetryHistory NOTIFY telemetryHistoryChanged)

    // Event log
    Q_PROPERTY(QVariantList eventLog READ eventLog NOTIFY eventLogChanged)

public:
    // Commands from QML (dispatch to gRPC on background thread)
    Q_INVOKABLE void arm();
    Q_INVOKABLE void disarm();
    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void emergencyStop();
    Q_INVOKABLE void loadProtocol(const QVariantList& phases);
    Q_INVOKABLE void injectFault(const QString& faultType, const QVariantMap& params);
    Q_INVOKABLE void exportData(const QString& format);

    // Protocol editing (local, pre-load — no gRPC call until loadProtocol)
    Q_INVOKABLE void addPhase(const QString& fluidType, double flowRate, double volume, double pressureLimit);
    Q_INVOKABLE void removePhase(int index);
    Q_INVOKABLE void reorderPhases(int from, int to);
    Q_INVOKABLE void clearProtocol();

signals:
    void connectionStatusChanged();
    void injectorStateChanged();
    void telemetryChanged();
    void telemetryHistoryChanged();
    void protocolChanged();
    void protocolLoaded();
    void faultsChanged();
    void eventLogChanged();

private:
    GrpcClientService* grpcClient_;   // owned, runs streaming on background threads
    // ... cached state from gRPC streams
};
```

**How commands flow:**

1. QML calls `Q_INVOKABLE` (e.g., `injectorBridge.start()`)
2. Bridge dispatches gRPC `SendCommand` RPC on a background thread (`std::async` or `QThreadPool`)
3. Response arrives → bridge updates internal state if needed
4. Streaming RPCs deliver the state change event → bridge updates Q_PROPERTYs → QML auto-updates

**How telemetry flows:**

1. `StreamTelemetry` runs on a dedicated background thread, continuously reading frames
2. Each frame is marshaled to the main thread via `QMetaObject::invokeMethod(bridge, ..., Qt::QueuedConnection)`
3. Bridge stores latest frame, emits `telemetryChanged` at 20 Hz via `QTimer` (coalescing)
4. QML property bindings re-evaluate automatically

### Protocol Configuration Panel

**When visible:** Always (sidebar or tab)
**Purpose:** Define and load injection protocols

**QML Components:**

- `ProtocolPanel.qml` — Container: phase list + syringe display
- `PhaseRow.qml` — Single phase: inputs + validation
- `SyringeIndicator.qml` — Remaining volume display

**Validation (client-side, pre-send):**

All validation ranges mirror the backend's authoritative rules in **04-schema-and-contracts.md, Section 6**. The frontend validates for UX; the backend re-validates for safety.

**State interactions:**

- Reads: `protocol`, `contrastRemaining`, `salineRemaining`, `injectorState`
- Writes: `addPhase()`, `removePhase()`, `reorderPhases()`, `loadProtocol()`
- Disabled when: `injectorState` is not `"Idle"` (cannot modify protocol while armed/injecting)

### Injection Control Panel

**When visible:** Always (prominent, top or right side)
**Purpose:** Control injection lifecycle

**QML Components:**

- `ControlPanel.qml` — State badge + buttons
- `StateIndicator.qml` — Color-coded state display
- `FaultDetail.qml` — Fault info + acknowledge/reset

**Components:**

- State indicator: large, color-coded rectangle showing current state
  - Idle=green, Armed=yellow, Injecting=blue, Paused=orange, Fault=red, Completed=grey
- Arm / Disarm button (toggles based on state)
- Start / Pause / Resume button (context-sensitive label and action)
- Emergency Stop button (large, red, always enabled when connected)
- Reset button (visible only in Fault state, after acknowledgment)
- Acknowledge Fault button (visible only in Fault state, before reset)
- Fault detail panel (type, value, timestamp)

**Button enable/disable logic:**

```qml
readonly property var buttonStates: ({
    "Idle":      { arm: true,  disarm: false, start: false, pause: false, resume: false, reset: false },
    "Armed":     { arm: false, disarm: true,  start: true,  pause: false, resume: false, reset: false },
    "Injecting": { arm: false, disarm: false, start: false, pause: true,  resume: false, reset: false },
    "Paused":    { arm: false, disarm: false, start: false, pause: false, resume: true,  reset: false },
    "Fault":     { arm: false, disarm: false, start: false, pause: false, resume: false, reset: true  },
    "Completed": { arm: false, disarm: false, start: false, pause: false, resume: false, reset: false }
})

// All buttons disabled when connectionStatus !== "connected"
// Emergency Stop: always enabled when connected, regardless of state
```

### Real-Time Dashboard

**When visible:** Always (main content area)
**Purpose:** Live injection monitoring

**QML Components:**

1. **Flow Rate Display** (`FlowRateGauge.qml`)
   - Numeric readout: target and actual (e.g., "4.00 / 3.97 mL/s")
   - Horizontal bar or gauge comparing target vs. actual
   - Updates via property binding to `injectorBridge.actualFlowRate`

2. **Pressure Gauge** (`PressureGauge.qml`)
   - Numeric readout in psi
   - Visual indicator showing proximity to limit (green → yellow → red zones)
   - Pressure limit threshold line

3. **Volume Progress** (`VolumeProgress.qml`)
   - Per-phase progress bars with labels ("Phase 1: Contrast — 42.1 / 80.0 mL")
   - Total volume: "110.0 mL total — 72.1 mL delivered (65%)"
   - Active phase highlighted

4. **Timeline Chart** (`TimelineChart.qml`)
   - Uses QML `Canvas` element with JavaScript drawing, or Qt Charts `ChartView`
   - X-axis: elapsed time (scrolling window, last 30 seconds, or full injection if completed)
   - Y-axis (left): flow rate (mL/s)
   - Y-axis (right): pressure (psi)
   - Two line series: actual flow rate + pressure
   - Phase boundary markers (vertical dashed lines)
   - Fault event markers (red vertical line with label)
   - Data source: `injectorBridge.telemetryHistory` (rolling QVariantList)

5. **Elapsed Time** (`ElapsedTimer.qml`)
   - Bound to `injectorBridge.elapsedTime`
   - Formatted as MM:SS.s

6. **Connection Status** (`ConnectionIndicator.qml`)
   - Green dot = connected, red = disconnected, yellow = reconnecting
   - Bound to `injectorBridge.connectionStatus`
   - When disconnected: all controls disabled, overlay message "Connecting to backend..."

**Performance considerations:**

- Telemetry arrives at 20 Hz from the gRPC stream (configurable via `StreamConfig.rate_ms`)
- Bridge coalesces and emits `telemetryChanged` at 20 Hz — QML never updates faster
- Timeline chart batches data points, repaints at 10 Hz max via `Timer { interval: 100 }`
- QML's scene graph renderer handles efficient partial repaints automatically
- Telemetry history: rolling window of last 6000 frames (5 minutes at 20 Hz)

## Data Flow

### Command flow (UI → Backend)

```
User clicks QML button
  → QML calls injectorBridge.start() (Q_INVOKABLE, main thread)
  → Bridge dispatches SendCommand RPC on background thread
  → gRPC serializes protobuf, sends to backend
  → Backend processes, responds with CommandResponse
  → Independently: StreamEvents delivers state change event
  → Bridge receives event on streaming thread
  → Marshals to main thread via QueuedConnection
  → Updates Q_PROPERTY injectorState
  → QML property bindings auto-update UI
```

### Telemetry flow (Backend → UI)

```
Backend StreamTelemetry sends TelemetryFrame (every 50 ms / 20 Hz)
  → gRPC client streaming thread receives protobuf frame
  → Marshals to main thread via QMetaObject::invokeMethod
  → Bridge stores latest frame, QTimer coalesces at 20 Hz
  → Emits telemetryChanged signal
  → QML property bindings re-evaluate
  → Flow rate, pressure, volume displays update
  → Timeline chart appends data point
```

### Event flow (Backend → UI)

```
Backend StreamEvents sends SystemEvent
  → gRPC client streaming thread receives protobuf event
  → Marshals to main thread
  → Bridge updates: injectorState, activeFaults, eventLog Q_PROPERTYs
  → QML bindings update: control panel buttons, state indicator
  → If fault: fault detail panel becomes visible
```

### Reconnection flow

```
gRPC stream breaks (backend restart, network issue)
  → Streaming threads detect error
  → Bridge sets connectionStatus = "reconnecting"
  → Exponential backoff retry (1s, 2s, 4s, max 10s)
  → On reconnect: calls GetState RPC for full state snapshot
  → Bridge repopulates all Q_PROPERTYs from snapshot
  → Restarts StreamTelemetry and StreamEvents
  → connectionStatus = "connected"
  → QML re-enables all controls
```

## Tech Decision: UI Framework — QML (Qt Quick)

**Chosen:** QML with Qt Quick Controls for the UI layer

**Why QML:**

- **Declarative and reactive** — Property bindings mean the UI automatically reflects state changes. `Text { text: injectorBridge.pressure.toFixed(1) + " psi" }` just works.
- **Built for real-time dashboards** — QML's scene graph renderer uses GPU-accelerated compositing. Canvas elements and Qt Charts handle frequent repaints efficiently.
- **Clean separation from backend** — QML only talks to the `InjectorBridge` QObject. The bridge handles all gRPC complexity. QML has no knowledge of protobuf, networking, or threading.
- **Faithful to the domain** — Real medical injector systems use embedded UIs, not web browsers. QML on a single-board computer is a realistic deployment target.

**Alternatives considered:**

| Alternative            | Why not                                                                                                                                     |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| React + grpc-web       | Requires a grpc-web proxy (Envoy) between browser and backend. Adds deployment complexity. QML is more natural for a desktop instrument UI. |
| Qt Widgets (C++)       | Imperative UI code. No property bindings — every telemetry update would need manual widget.setText() calls. More code, harder to maintain.  |
| Electron               | Heavy (Chromium). Defeats the purpose of a native UI. Would still need grpc-web or a custom bridge.                                         |
| Web UI (any framework) | Browser-based UIs cannot do gRPC natively (requires proxy). Adds unnecessary infrastructure for a desktop application.                      |

## Tech Decision: Charting — QML Canvas or Qt Charts

**Chosen:** QML `Canvas` element with custom JavaScript drawing for the timeline chart. Qt Charts `ChartView` is an alternative if more features are needed.

**Why Canvas:**

- **Lightweight** — No additional Qt module dependency
- **Full control** — Custom drawing for phase boundary markers, fault indicators, dual Y-axis
- **Efficient for streaming data** — Only redraws the new portion of the chart
- **Familiar API** — HTML5 Canvas-compatible drawing API

**Chart configuration:**

- 2 series: flow rate (blue line, left Y-axis), pressure (orange line, right Y-axis)
- X-axis: seconds elapsed, auto-scrolling window (30s during injection, full range after completion)
- Vertical markers for phase boundaries and faults
- Update rate: buffer incoming telemetry, repaint at 10 Hz via `Timer { interval: 100 }`

## Tech Decision: Styling — QML Theming

**Chosen:** Custom QML styling with a centralized `Theme.qml` singleton.

```qml
// Theme.qml (singleton)
pragma Singleton
import QtQuick

QtObject {
    // State colors
    readonly property color idle: "#22c55e"
    readonly property color armed: "#eab308"
    readonly property color injecting: "#3b82f6"
    readonly property color paused: "#f97316"
    readonly property color fault: "#ef4444"
    readonly property color completed: "#6b7280"

    // Connection status colors
    readonly property color connected: "#22c55e"
    readonly property color disconnected: "#ef4444"
    readonly property color reconnecting: "#eab308"

    // UI colors
    readonly property color background: "#1e1e2e"
    readonly property color surface: "#2a2a3e"
    readonly property color text: "#e0e0e0"
    readonly property color textSecondary: "#a0a0b0"

    // Spacing
    readonly property int spacingSmall: 4
    readonly property int spacingMedium: 8
    readonly property int spacingLarge: 16

    // Fonts
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeMedium: 14
    readonly property int fontSizeLarge: 18
    readonly property int fontSizeTitle: 24
}
```

## UI Layout

```
┌─────────────────────────────────────────────────────────────┐
│  Connection: ●  Medical Injector Simulator                  │
├───────────────────┬─────────────────────────────────────────┤
│                   │                                         │
│  Protocol Config  │  ┌───────────────────────────────────┐  │
│  ───────────────  │  │  State: ██ INJECTING              │  │
│                   │  │                                    │  │
│  Phase 1:         │  │  [Pause]  [■ EMERGENCY STOP]      │  │
│  ☐ Contrast       │  └───────────────────────────────────┘  │
│  4.0 mL/s         │                                         │
│  80 mL            │  Flow Rate    3.97 / 4.00 mL/s         │
│  325 psi limit    │  ████████████████████░░  99%            │
│                   │                                         │
│  Phase 2:         │  Pressure     198 psi (limit: 325)      │
│  ☐ Saline         │  ██████████░░░░░░░░░░░  61%            │
│  2.0 mL/s         │                                         │
│  30 mL            │  Volume                                 │
│  200 psi limit    │  Phase 1: ██████████████░░  42/80 mL   │
│                   │  Phase 2: ░░░░░░░░░░░░░░░░   0/30 mL   │
│  [+ Add Phase]    │  Total:   42.1 / 110.0 mL (38%)        │
│                   │                                         │
│  ───────────────  │  Timeline                               │
│  Contrast: 100 mL │  ┌─────────────────────────────────┐   │
│  Saline:   50 mL  │  │ ╱──────────────                  │   │
│                   │  │╱              flow rate           │   │
│  [Load Protocol]  │  │  ╱─────────────                  │   │
│                   │  │ ╱             pressure            │   │
│                   │  └─────────────────────────────────┘   │
│                   │  Elapsed: 00:10.5                       │
├───────────────────┴─────────────────────────────────────────┤
│  Event Log: Armed 00:00 → Injecting 00:01 → ...            │
└─────────────────────────────────────────────────────────────┘
```

**Layout approach (QML):**

- Root: `ApplicationWindow` with `RowLayout`
- Left: `ProtocolPanel` (fixed width 300px, scrollable `ListView` for phases)
- Right: `ColumnLayout` containing:
  - `ControlPanel` (state badge + buttons)
  - `Dashboard` (gauges + volume progress)
  - `TimelineChart` (Canvas or ChartView, fills remaining height)
- Bottom: `EventLog` (collapsible `ListView`, scrollable)

**Responsive:** Not a priority for MVP. Designed for desktop (1280px+ width). Real medical injectors have fixed-size touchscreens — desktop-only is faithful to the domain.

## Project Structure

The frontend has three directories under `frontend/`:

- **`src/bridge/`** — `InjectorBridge` QObject (QML ↔ gRPC boundary)
- **`src/grpc/`** — `GrpcClientService` (channel, stub, streaming threads)
- **`qml/components/`** — All QML visual components (14 files: gauges, panels, chart, indicators)

**Full file listing:** See **06-repo-structure.md, Section 1**. The proto file lives at `proto/injector.proto` (repo root, shared with backend — not copied into the frontend directory).

## External Dependencies

| Component              | Purpose                                 | Version Strategy      |
| ---------------------- | --------------------------------------- | --------------------- |
| Qt 6 Quick             | QML engine + scene graph rendering      | Latest LTS (6.5+)     |
| Qt 6 Quick Controls    | Buttons, text fields, layouts           | Same as Qt            |
| Qt 6 Charts (optional) | Timeline chart (if Canvas insufficient) | Same as Qt            |
| `grpc++`               | gRPC client + generated stubs           | Latest stable (1.60+) |
| `protobuf`             | Protocol Buffer serialization           | Matching grpc version |
| CMake                  | Build system                            | 3.22+                 |

## QML ↔ Bridge Type Mapping

QML sees only Qt/JavaScript types. The bridge converts between protobuf and QVariant:

| Protobuf Type        | Bridge C++ Type | QML Type          | Mechanism                                      |
| -------------------- | --------------- | ----------------- | ---------------------------------------------- |
| `string`             | `QString`       | `string`          | Automatic                                      |
| `double`             | `double`        | `real`            | Automatic                                      |
| `int32`              | `int`           | `int`             | Automatic                                      |
| `bool`               | `bool`          | `bool`            | Automatic                                      |
| `repeated T`         | `QVariantList`  | `var` (JS array)  | Manual conversion in bridge                    |
| `InjectorState` enum | `QString`       | `string`          | Bridge maps enum → string                      |
| `TelemetryFrame`     | `QVariantMap`   | `var` (JS object) | Bridge converts in streaming handler           |
| `Phase`              | `QVariantMap`   | `var` (JS object) | `{fluidType, flowRate, volume, pressureLimit}` |
| `FaultInfo`          | `QVariantMap`   | `var` (JS object) | `{type, value, threshold, timestamp}`          |
| `SystemEvent`        | `QVariantMap`   | `var` (JS object) | `{timestamp, type, details}`                   |

## Startup Sequence

1. `main()` creates `QGuiApplication` and `QQmlApplicationEngine`
2. Create `GrpcClientService` with backend address from config/CLI args
3. Create `InjectorBridge`, pass gRPC client reference
4. Register bridge: `engine.rootContext()->setContextProperty("injectorBridge", &bridge)`
5. Load QML: `engine.load("qrc:/qml/main.qml")`
6. QML renders with `connectionStatus = "connecting"`, all controls disabled
7. gRPC client connects to backend, calls `GetState` for snapshot
8. Bridge populates Q_PROPERTYs from snapshot, starts streaming RPCs
9. `connectionStatus = "connected"`, controls enabled, dashboard live

**Dev workflow:**

```
# Terminal 1: start backend
cd backend && cmake --build . && ./injector-backend

# Terminal 2: start frontend
cd frontend && cmake --build . && ./injector-frontend
```

Frontend can be started before backend — it will show "Connecting..." and auto-connect when the backend becomes available.

## Frontend Configuration

Minimal config — the frontend only needs to know where the backend is:

```json
{
  "backend": {
    "address": "localhost:50051"
  },
  "ui": {
    "telemetryRateMs": 50,
    "chartHistorySeconds": 300
  }
}
```

Or via command-line: `./injector-frontend --backend=localhost:50051`
