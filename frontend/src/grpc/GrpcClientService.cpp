#include "GrpcClientService.h"
#include <chrono>
#include <algorithm>

namespace injector::frontend {

GrpcClientService::GrpcClientService(const std::string& address)
    : address_(address) {}

GrpcClientService::~GrpcClientService() {
    disconnect();
}

void GrpcClientService::setTelemetryCallback(TelemetryCallback cb) {
    telemetryCallback_ = std::move(cb);
}

void GrpcClientService::setEventCallback(EventCallback cb) {
    eventCallback_ = std::move(cb);
}

void GrpcClientService::setConnectionStateCallback(StateCallback cb) {
    stateCallback_ = std::move(cb);
}

void GrpcClientService::connect() {
    if (running_.load()) return;
    running_ = true;
    connectionThread_ = std::thread(&GrpcClientService::connectionLoop, this);
}

void GrpcClientService::disconnect() {
    running_ = false;

    // Cancel active streaming contexts
    {
        std::lock_guard<std::mutex> lock(contextMutex_);
        if (telemetryContext_) telemetryContext_->TryCancel();
        if (eventContext_) eventContext_->TryCancel();
    }

    if (telemetryThread_.joinable()) telemetryThread_.join();
    if (eventThread_.joinable()) eventThread_.join();
    if (connectionThread_.joinable()) connectionThread_.join();

    setConnectionState(ConnectionState::Disconnected);
}

ConnectionState GrpcClientService::connectionState() const {
    return connectionState_.load();
}

void GrpcClientService::setConnectionState(ConnectionState state) {
    connectionState_ = state;
    if (stateCallback_) stateCallback_(state);
}

// ── Connection loop with exponential backoff ────────────────────

void GrpcClientService::connectionLoop() {
    int backoffMs = 1000;
    constexpr int maxBackoffMs = 10000;

    while (running_) {
        setConnectionState(connectionState() == ConnectionState::Disconnected
                           ? ConnectionState::Connecting
                           : ConnectionState::Reconnecting);

        channel_ = grpc::CreateChannel(address_, grpc::InsecureChannelCredentials());
        stub_ = ::injector::InjectorService::NewStub(channel_);

        // Try to connect by issuing GetState
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
        ::injector::Empty empty;
        ::injector::StateSnapshot snapshot;
        auto status = stub_->GetState(&ctx, empty, &snapshot);

        if (status.ok()) {
            setConnectionState(ConnectionState::Connected);
            backoffMs = 1000; // Reset backoff on success

            // Start streaming threads
            telemetryThread_ = std::thread(&GrpcClientService::telemetryLoop, this);
            eventThread_ = std::thread(&GrpcClientService::eventLoop, this);

            // Wait for streaming threads to exit (means connection lost)
            if (telemetryThread_.joinable()) telemetryThread_.join();
            if (eventThread_.joinable()) eventThread_.join();

            if (!running_) break;

            setConnectionState(ConnectionState::Reconnecting);
        }

        if (!running_) break;

        // Backoff before retry
        auto sleepUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(backoffMs);
        while (running_ && std::chrono::steady_clock::now() < sleepUntil) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        backoffMs = std::min(backoffMs * 2, maxBackoffMs);
    }
}

// ── Telemetry streaming loop ────────────────────────────────────

void GrpcClientService::telemetryLoop() {
    {
        std::lock_guard<std::mutex> lock(contextMutex_);
        telemetryContext_ = std::make_unique<grpc::ClientContext>();
    }

    ::injector::StreamConfig config;
    config.set_rate_ms(50); // 20 Hz

    auto reader = stub_->StreamTelemetry(telemetryContext_.get(), config);

    ::injector::TelemetryFrame frame;
    while (reader->Read(&frame)) {
        if (!running_) break;
        if (telemetryCallback_) telemetryCallback_(frame);
    }

    {
        std::lock_guard<std::mutex> lock(contextMutex_);
        telemetryContext_.reset();
    }
}

// ── Event streaming loop ────────────────────────────────────────

void GrpcClientService::eventLoop() {
    {
        std::lock_guard<std::mutex> lock(contextMutex_);
        eventContext_ = std::make_unique<grpc::ClientContext>();
    }

    ::injector::Empty empty;
    auto reader = stub_->StreamEvents(eventContext_.get(), empty);

    ::injector::SystemEvent event;
    while (reader->Read(&event)) {
        if (!running_) break;
        if (eventCallback_) eventCallback_(event);
    }

    {
        std::lock_guard<std::mutex> lock(contextMutex_);
        eventContext_.reset();
    }
}

// ── Unary RPCs ──────────────────────────────────────────────────

::injector::CommandResponse GrpcClientService::sendCommand(::injector::CommandType cmd) {
    ::injector::CommandRequest req;
    req.set_command(cmd);

    ::injector::CommandResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    auto status = stub_->SendCommand(&ctx, req, &resp);

    if (!status.ok()) {
        resp.set_success(false);
        resp.set_error(status.error_message());
    }
    return resp;
}

::injector::CommandResponse GrpcClientService::loadProtocol(const ::injector::LoadProtocolRequest& req) {
    ::injector::CommandResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    auto status = stub_->LoadProtocol(&ctx, req, &resp);

    if (!status.ok()) {
        resp.set_success(false);
        resp.set_error(status.error_message());
    }
    return resp;
}

::injector::CommandResponse GrpcClientService::injectFault(const ::injector::InjectFaultRequest& req) {
    ::injector::CommandResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    auto status = stub_->InjectFault(&ctx, req, &resp);

    if (!status.ok()) {
        resp.set_success(false);
        resp.set_error(status.error_message());
    }
    return resp;
}

::injector::StateSnapshot GrpcClientService::getState() {
    ::injector::Empty empty;
    ::injector::StateSnapshot snapshot;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    stub_->GetState(&ctx, empty, &snapshot);
    return snapshot;
}

::injector::ExportResponse GrpcClientService::exportData(const std::string& format) {
    ::injector::ExportRequest req;
    req.set_format(format);

    ::injector::ExportResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    stub_->ExportData(&ctx, req, &resp);
    return resp;
}

} // namespace injector::frontend
