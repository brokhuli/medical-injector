#include "InjectorBridge.h"
#include <QMetaObject>
#include <QThreadPool>

namespace injector::frontend {

namespace {

QString stateToString(::injector::InjectorState state) {
    switch (state) {
        case ::injector::IDLE:      return QStringLiteral("Idle");
        case ::injector::ARMED:     return QStringLiteral("Armed");
        case ::injector::INJECTING: return QStringLiteral("Injecting");
        case ::injector::PAUSED:    return QStringLiteral("Paused");
        case ::injector::COMPLETED: return QStringLiteral("Completed");
        case ::injector::FAULT:     return QStringLiteral("Fault");
        default:                    return QStringLiteral("Unknown");
    }
}

QString connectionStateToString(ConnectionState state) {
    switch (state) {
        case ConnectionState::Connecting:   return QStringLiteral("connecting");
        case ConnectionState::Connected:    return QStringLiteral("connected");
        case ConnectionState::Disconnected: return QStringLiteral("disconnected");
        case ConnectionState::Reconnecting: return QStringLiteral("reconnecting");
        default:                            return QStringLiteral("disconnected");
    }
}

::injector::FaultType faultTypeFromString(const QString& str) {
    if (str == "overpressure")      return ::injector::OVERPRESSURE;
    if (str == "air_bubble")        return ::injector::AIR_BUBBLE;
    if (str == "motor_stall")       return ::injector::MOTOR_STALL;
    if (str == "partial_occlusion") return ::injector::PARTIAL_OCCLUSION;
    if (str == "timing_delay")      return ::injector::TIMING_DELAY;
    return ::injector::OVERPRESSURE;
}

} // anonymous namespace

InjectorBridge::InjectorBridge(GrpcClientService* grpcClient, QObject* parent)
    : QObject(parent)
    , grpcClient_(grpcClient)
{
    // Coalesce telemetry emissions at 20 Hz
    telemetryCoalesceTimer_.setInterval(50);
    telemetryCoalesceTimer_.setSingleShot(false);
    QObject::connect(&telemetryCoalesceTimer_, &QTimer::timeout, this, [this]() {
        if (telemetryDirty_) {
            telemetryDirty_ = false;
            emit telemetryChanged();
        }
    });
    telemetryCoalesceTimer_.start();

    // Wire gRPC callbacks (these fire on background threads)
    grpcClient_->setConnectionStateCallback(
        [this](ConnectionState s) { onConnectionStateChanged(s); });
    grpcClient_->setTelemetryCallback(
        [this](const ::injector::TelemetryFrame& f) { onTelemetryFrame(f); });
    grpcClient_->setEventCallback(
        [this](const ::injector::SystemEvent& e) { onSystemEvent(e); });
}

InjectorBridge::~InjectorBridge() = default;

// ── Property readers ────────────────────────────────────────────

QString InjectorBridge::connectionStatus() const { return connectionStatus_; }
QString InjectorBridge::injectorState() const { return injectorState_; }
double InjectorBridge::targetFlowRate() const { return targetFlowRate_; }
double InjectorBridge::actualFlowRate() const { return actualFlowRate_; }
double InjectorBridge::pressure() const { return pressure_; }
double InjectorBridge::motorRpm() const { return motorRpm_; }
int InjectorBridge::phaseIndex() const { return phaseIndex_; }
double InjectorBridge::totalVolumeDelivered() const { return totalVolumeDelivered_; }
double InjectorBridge::totalProgrammedVolume() const { return totalProgrammedVolume_; }
double InjectorBridge::elapsedTime() const { return elapsedTime_; }
double InjectorBridge::contrastRemaining() const { return contrastRemaining_; }
double InjectorBridge::salineRemaining() const { return salineRemaining_; }
QVariantList InjectorBridge::protocol() const { return protocol_; }
QVariantList InjectorBridge::loadedProtocol() const { return loadedProtocol_; }
QVariantList InjectorBridge::activeFaults() const { return activeFaults_; }
QVariantList InjectorBridge::telemetryHistory() const { return telemetryHistory_; }
QVariantList InjectorBridge::eventLog() const { return eventLog_; }

// ── Command methods ─────────────────────────────────────────────

void InjectorBridge::dispatchCommand(::injector::CommandType cmd) {
    auto* client = grpcClient_;
    QThreadPool::globalInstance()->start([client, cmd]() {
        client->sendCommand(cmd);
    });
}

void InjectorBridge::arm()           { dispatchCommand(::injector::ARM); }
void InjectorBridge::disarm()        { dispatchCommand(::injector::DISARM); }
void InjectorBridge::start()         { dispatchCommand(::injector::START); }
void InjectorBridge::pause()         { dispatchCommand(::injector::PAUSE); }
void InjectorBridge::resume()        { dispatchCommand(::injector::RESUME); }
void InjectorBridge::reset()         { dispatchCommand(::injector::RESET); }
void InjectorBridge::emergencyStop() { dispatchCommand(::injector::EMERGENCY_STOP); }

// ── Protocol editing (local state, no gRPC) ─────────────────────

void InjectorBridge::addPhase(const QString& fluidType, double flowRate,
                              double volume, double pressureLimit) {
    QVariantMap phase;
    phase["fluidType"] = fluidType;
    phase["flowRate"] = flowRate;
    phase["volume"] = volume;
    phase["pressureLimit"] = pressureLimit;
    protocol_.append(phase);
    emit protocolChanged();
}

void InjectorBridge::removePhase(int index) {
    if (index >= 0 && index < protocol_.size()) {
        protocol_.removeAt(index);
        emit protocolChanged();
    }
}

void InjectorBridge::reorderPhases(int from, int to) {
    if (from >= 0 && from < protocol_.size() &&
        to >= 0 && to < protocol_.size() && from != to) {
        auto item = protocol_.takeAt(from);
        protocol_.insert(to, item);
        emit protocolChanged();
    }
}

void InjectorBridge::clearProtocol() {
    if (!protocol_.isEmpty()) {
        protocol_.clear();
        emit protocolChanged();
    }
}

void InjectorBridge::loadProtocol(const QVariantList& phases) {
    // Build protobuf request from QVariantList
    ::injector::LoadProtocolRequest req;
    for (const auto& phaseVar : phases) {
        auto phaseMap = phaseVar.toMap();
        auto* phase = req.add_phases();

        QString fluidType = phaseMap["fluidType"].toString();
        phase->set_fluid_type(fluidType.toLower() == "saline"
                              ? ::injector::SALINE
                              : ::injector::CONTRAST);
        phase->set_flow_rate(phaseMap["flowRate"].toDouble());
        phase->set_volume(phaseMap["volume"].toDouble());
        phase->set_pressure_limit(phaseMap["pressureLimit"].toDouble());
    }

    // Save what we're sending as loaded protocol
    loadedProtocol_ = phases;

    auto* client = grpcClient_;
    QThreadPool::globalInstance()->start([client, req]() {
        client->loadProtocol(req);
    });

    emit protocolLoaded();
}

// ── Fault injection ─────────────────────────────────────────────

void InjectorBridge::injectFault(const QString& faultType, const QVariantMap& params) {
    ::injector::InjectFaultRequest req;
    req.set_fault_type(faultTypeFromString(faultType));
    req.set_target_psi(params.value("targetPsi").toDouble());
    req.set_max_rpm(params.value("maxRpm").toDouble());
    req.set_resistance_multiplier(params.value("resistanceMultiplier").toDouble());
    req.set_delay_ms(params.value("delayMs").toDouble());

    auto* client = grpcClient_;
    QThreadPool::globalInstance()->start([client, req]() {
        client->injectFault(req);
    });
}

// ── Export ───────────────────────────────────────────────────────

void InjectorBridge::exportData(const QString& format) {
    auto* client = grpcClient_;
    auto fmt = format.toStdString();
    QThreadPool::globalInstance()->start([client, fmt]() {
        client->exportData(fmt);
    });
}

// ── Background thread callbacks → marshal to main thread ────────

void InjectorBridge::onConnectionStateChanged(ConnectionState state) {
    QMetaObject::invokeMethod(this, "handleConnectionState",
        Qt::QueuedConnection, Q_ARG(int, static_cast<int>(state)));
}

void InjectorBridge::onTelemetryFrame(const ::injector::TelemetryFrame& frame) {
    QMetaObject::invokeMethod(this, "handleTelemetry",
        Qt::QueuedConnection,
        Q_ARG(int, static_cast<int>(frame.state())),
        Q_ARG(int, frame.phase_index()),
        Q_ARG(double, frame.target_flow_rate()),
        Q_ARG(double, frame.actual_flow_rate()),
        Q_ARG(double, frame.pressure()),
        Q_ARG(double, frame.motor_rpm()),
        Q_ARG(double, frame.total_volume_delivered()),
        Q_ARG(double, frame.total_programmed_volume()),
        Q_ARG(double, frame.elapsed_time()),
        Q_ARG(double, frame.contrast_remaining()),
        Q_ARG(double, frame.saline_remaining()));
}

void InjectorBridge::onSystemEvent(const ::injector::SystemEvent& event) {
    QMetaObject::invokeMethod(this, "handleEvent",
        Qt::QueuedConnection,
        Q_ARG(double, event.timestamp()),
        Q_ARG(int, static_cast<int>(event.type())),
        Q_ARG(QString, QString::fromStdString(event.details())));
}

// ── Main-thread handlers ────────────────────────────────────────

void InjectorBridge::handleConnectionState(int state) {
    auto newStatus = connectionStateToString(static_cast<ConnectionState>(state));
    if (connectionStatus_ != newStatus) {
        connectionStatus_ = newStatus;
        emit connectionStatusChanged();
    }
}

void InjectorBridge::handleTelemetry(
    int state, int phaseIdx,
    double targetFlow, double actualFlow, double pres, double rpm,
    double totalVolDelivered, double totalVolProgrammed, double elapsed,
    double contrastRemain, double salineRemain)
{
    auto newState = stateToString(static_cast<::injector::InjectorState>(state));
    if (injectorState_ != newState) {
        injectorState_ = newState;
        emit injectorStateChanged();
    }

    targetFlowRate_ = targetFlow;
    actualFlowRate_ = actualFlow;
    pressure_ = pres;
    motorRpm_ = rpm;
    phaseIndex_ = phaseIdx;
    totalVolumeDelivered_ = totalVolDelivered;
    totalProgrammedVolume_ = totalVolProgrammed;
    elapsedTime_ = elapsed;
    contrastRemaining_ = contrastRemain;
    salineRemaining_ = salineRemain;

    telemetryDirty_ = true; // Timer will emit telemetryChanged

    // Append to telemetry history (rolling window)
    QVariantMap historyEntry;
    historyEntry["timestamp"] = elapsed;
    historyEntry["targetFlowRate"] = targetFlow;
    historyEntry["actualFlowRate"] = actualFlow;
    historyEntry["pressure"] = pres;
    historyEntry["motorRpm"] = rpm;
    historyEntry["phaseIndex"] = phaseIdx;
    historyEntry["state"] = newState;

    telemetryHistory_.append(historyEntry);
    if (telemetryHistory_.size() > kMaxHistoryFrames) {
        telemetryHistory_.removeFirst();
    }
    emit telemetryHistoryChanged();
}

void InjectorBridge::handleEvent(double timestamp, int type, const QString& details) {
    // Append to event log
    QVariantMap entry;
    entry["timestamp"] = timestamp;
    entry["type"] = type;
    entry["details"] = details;

    QString typeStr;
    switch (static_cast<::injector::EventType>(type)) {
        case ::injector::STATE_TRANSITION: typeStr = "state_transition"; break;
        case ::injector::FAULT_DETECTED:   typeStr = "fault_detected"; break;
        case ::injector::PHASE_TRANSITION: typeStr = "phase_transition"; break;
        case ::injector::PROTOCOL_LOADED:  typeStr = "protocol_loaded"; break;
        case ::injector::COMMAND_REJECTED: typeStr = "command_rejected"; break;
        default:                           typeStr = "unknown"; break;
    }
    entry["typeStr"] = typeStr;

    eventLog_.append(entry);
    emit eventLogChanged();

    // Update faults from fault events
    if (type == static_cast<int>(::injector::FAULT_DETECTED)) {
        QVariantMap faultInfo;
        faultInfo["details"] = details;
        faultInfo["timestamp"] = timestamp;
        activeFaults_.append(faultInfo);
        emit faultsChanged();
    }

    // Clear faults on state transition away from Fault state
    if (type == static_cast<int>(::injector::STATE_TRANSITION)) {
        if (!activeFaults_.isEmpty() && !details.contains("Fault", Qt::CaseInsensitive)) {
            activeFaults_.clear();
            emit faultsChanged();
        }
    }
}

} // namespace injector::frontend
