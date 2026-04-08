#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "grpc/GrpcClientService.h"

namespace injector::frontend {

/// QObject bridge between QML UI and GrpcClientService.
/// Exposes all injector state, telemetry, protocol editing, commands, and history to QML.
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

    // Protocol (local editing state, pre-load)
    Q_PROPERTY(QVariantList protocol READ protocol NOTIFY protocolChanged)
    Q_PROPERTY(QVariantList loadedProtocol READ loadedProtocol NOTIFY protocolLoaded)

    // Faults
    Q_PROPERTY(QVariantList activeFaults READ activeFaults NOTIFY faultsChanged)

    // Telemetry history (rolling window for timeline chart)
    Q_PROPERTY(QVariantList telemetryHistory READ telemetryHistory NOTIFY telemetryHistoryChanged)

    // Event log
    Q_PROPERTY(QVariantList eventLog READ eventLog NOTIFY eventLogChanged)

public:
    explicit InjectorBridge(GrpcClientService* grpcClient, QObject* parent = nullptr);
    ~InjectorBridge() override;

    // ── Property readers ────────────────────────────────────────
    QString connectionStatus() const;
    QString injectorState() const;
    double targetFlowRate() const;
    double actualFlowRate() const;
    double pressure() const;
    double motorRpm() const;
    int phaseIndex() const;
    double totalVolumeDelivered() const;
    double totalProgrammedVolume() const;
    double elapsedTime() const;
    double contrastRemaining() const;
    double salineRemaining() const;
    QVariantList protocol() const;
    QVariantList loadedProtocol() const;
    QVariantList activeFaults() const;
    QVariantList telemetryHistory() const;
    QVariantList eventLog() const;

    // ── Commands (dispatch to gRPC on background thread) ────────
    Q_INVOKABLE void arm();
    Q_INVOKABLE void disarm();
    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void emergencyStop();

    // ── Protocol editing (local, no gRPC until loadProtocol) ────
    Q_INVOKABLE void addPhase(const QString& fluidType, double flowRate,
                              double volume, double pressureLimit);
    Q_INVOKABLE void updatePhase(int index, const QString& fluidType, double flowRate,
                                 double volume, double pressureLimit);
    Q_INVOKABLE void removePhase(int index);
    Q_INVOKABLE void reorderPhases(int from, int to);
    Q_INVOKABLE void clearProtocol();
    Q_INVOKABLE void loadProtocol(const QVariantList& phases);

    // ── Fault injection + export ────────────────────────────────
    Q_INVOKABLE void injectFault(const QString& faultType, const QVariantMap& params);
    Q_INVOKABLE void exportData(const QString& format);

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
    void onConnectionStateChanged(ConnectionState state);
    void onTelemetryFrame(const ::injector::TelemetryFrame& frame);
    void onSystemEvent(const ::injector::SystemEvent& event);
    void dispatchCommand(::injector::CommandType cmd);

    // Called via QMetaObject::invokeMethod from background threads
    Q_INVOKABLE void handleConnectionState(int state);
    Q_INVOKABLE void handleTelemetry(
        int state, int phaseIndex,
        double targetFlow, double actualFlow, double pressure, double motorRpm,
        double totalVolDelivered, double totalVolProgrammed, double elapsed,
        double contrastRemain, double salineRemain);
    Q_INVOKABLE void handleEvent(double timestamp, int type, const QString& details);

    GrpcClientService* grpcClient_;
    QTimer telemetryCoalesceTimer_;

    // ── Cached telemetry state ──────────────────────────────────
    QString connectionStatus_{"connecting"};
    QString injectorState_{"Idle"};
    double targetFlowRate_ = 0.0;
    double actualFlowRate_ = 0.0;
    double pressure_ = 0.0;
    double motorRpm_ = 0.0;
    int phaseIndex_ = 0;
    double totalVolumeDelivered_ = 0.0;
    double totalProgrammedVolume_ = 0.0;
    double elapsedTime_ = 0.0;
    double injectionStartTime_ = -1.0;  // backend timestamp when injection started; -1 = not started
    double contrastRemaining_ = 0.0;
    double salineRemaining_ = 0.0;
    bool telemetryDirty_ = false;

    // ── Protocol editing state ──────────────────────────────────
    QVariantList protocol_;        // Local editing buffer
    QVariantList loadedProtocol_;  // Last protocol sent to backend

    // ── Faults ──────────────────────────────────────────────────
    QVariantList activeFaults_;

    // ── Telemetry history (rolling window, max 6000 frames) ─────
    static constexpr int kMaxHistoryFrames = 6000;
    QVariantList telemetryHistory_;

    // ── Event log ───────────────────────────────────────────────
    QVariantList eventLog_;
};

} // namespace injector::frontend
