#pragma once

#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/TelemetryBroadcast.h"
#include "hal/IHalInterface.h"
#include "logging/DataLogger.h"
#include "state/StateMachine.h"

#include <grpcpp/grpcpp.h>
#include <injector.grpc.pb.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace injector::comms {

/// All the backend subsystems that the gRPC server needs access to.
struct GrpcDependencies {
    CommandQueue* commandQueue = nullptr;
    EventBroadcast* events = nullptr;
    TelemetryBroadcast* telemetry = nullptr;
    state::StateMachine* stateMachine = nullptr;
    hal::IHalInterface* hal = nullptr;
    logging::DataLogger* logger = nullptr;
    int defaultTelemetryRateMs = 50;
};

/// gRPC service implementation.  Translates proto requests into InternalCommands,
/// reads from TelemetryBroadcast / EventBroadcast for streaming RPCs.
class GrpcServiceImpl final : public ::injector::InjectorService::Service {
public:
    explicit GrpcServiceImpl(const GrpcDependencies& deps);

    // ── Unary RPCs ──────────────────────────────────────────────
    grpc::Status SendCommand(grpc::ServerContext* ctx,
                             const ::injector::CommandRequest* request,
                             ::injector::CommandResponse* response) override;

    grpc::Status LoadProtocol(grpc::ServerContext* ctx,
                              const ::injector::LoadProtocolRequest* request,
                              ::injector::CommandResponse* response) override;

    grpc::Status InjectFault(grpc::ServerContext* ctx,
                             const ::injector::InjectFaultRequest* request,
                             ::injector::CommandResponse* response) override;

    grpc::Status ExportData(grpc::ServerContext* ctx,
                            const ::injector::ExportRequest* request,
                            ::injector::ExportResponse* response) override;

    grpc::Status GetState(grpc::ServerContext* ctx,
                          const ::injector::Empty* request,
                          ::injector::StateSnapshot* response) override;

    // ── Server-streaming RPCs ───────────────────────────────────
    grpc::Status StreamTelemetry(grpc::ServerContext* ctx,
                                 const ::injector::StreamConfig* request,
                                 grpc::ServerWriter<::injector::TelemetryFrame>* writer) override;

    grpc::Status StreamEvents(grpc::ServerContext* ctx,
                              const ::injector::Empty* request,
                              grpc::ServerWriter<::injector::SystemEvent>* writer) override;

private:
    GrpcDependencies deps_;
};

/// Manages the gRPC server lifecycle.
class GrpcServer {
public:
    /// @param address  Listening address, e.g. "0.0.0.0:50051".
    GrpcServer(const std::string& address, const GrpcDependencies& deps);
    ~GrpcServer();

    /// Start listening (non-blocking — spawns a background thread).
    void start();

    /// Shut down the server and join the background thread.
    void stop();

    [[nodiscard]] bool running() const { return running_.load(); }

private:
    std::string address_;
    GrpcDependencies deps_;
    std::unique_ptr<grpc::Server> server_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace injector::comms
