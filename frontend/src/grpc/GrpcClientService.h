#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>
#include "injector.grpc.pb.h"

namespace injector::frontend {

enum class ConnectionState {
    Connecting,
    Connected,
    Disconnected,
    Reconnecting
};

/// Non-QObject gRPC client that manages channel, stub, and streaming threads.
/// Callbacks are invoked on background threads — the bridge must marshal to main thread.
class GrpcClientService {
public:
    using TelemetryCallback = std::function<void(const ::injector::TelemetryFrame&)>;
    using EventCallback     = std::function<void(const ::injector::SystemEvent&)>;
    using StateCallback     = std::function<void(ConnectionState)>;

    explicit GrpcClientService(const std::string& address);
    ~GrpcClientService();

    // Non-copyable
    GrpcClientService(const GrpcClientService&) = delete;
    GrpcClientService& operator=(const GrpcClientService&) = delete;

    /// Set callbacks before calling connect().
    void setTelemetryCallback(TelemetryCallback cb);
    void setEventCallback(EventCallback cb);
    void setConnectionStateCallback(StateCallback cb);

    /// Begin connection + streaming on background threads.
    void connect();

    /// Gracefully shut down all streams and threads.
    void disconnect();

    ConnectionState connectionState() const;

    // ── Unary RPCs (blocking, call from background thread) ──────
    ::injector::CommandResponse sendCommand(::injector::CommandType cmd);
    ::injector::CommandResponse loadProtocol(const ::injector::LoadProtocolRequest& req);
    ::injector::CommandResponse injectFault(const ::injector::InjectFaultRequest& req);
    ::injector::StateSnapshot   getState();
    ::injector::ExportResponse  exportData(const std::string& format);

private:
    void connectionLoop();
    void telemetryLoop();
    void eventLoop();
    void setConnectionState(ConnectionState state);

    std::string address_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<::injector::InjectorService::Stub> stub_;

    std::atomic<bool> running_{false};
    std::atomic<ConnectionState> connectionState_{ConnectionState::Disconnected};

    std::thread connectionThread_;
    std::thread telemetryThread_;
    std::thread eventThread_;

    // Stream cancellation contexts
    std::mutex contextMutex_;
    std::unique_ptr<grpc::ClientContext> telemetryContext_;
    std::unique_ptr<grpc::ClientContext> eventContext_;

    TelemetryCallback telemetryCallback_;
    EventCallback eventCallback_;
    StateCallback stateCallback_;
};

} // namespace injector::frontend
