#include "comms/GrpcServer.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace injector::comms {

// ── Helpers: map between C++ enums and proto enums ──────────────────────────

namespace {

comms::InternalCommand::Type protoCommandToInternal(::injector::CommandType ct) {
    switch (ct) {
        case ::injector::ARM:            return InternalCommand::Type::Arm;
        case ::injector::DISARM:         return InternalCommand::Type::Disarm;
        case ::injector::START:          return InternalCommand::Type::Start;
        case ::injector::PAUSE:          return InternalCommand::Type::Pause;
        case ::injector::RESUME:         return InternalCommand::Type::Resume;
        case ::injector::RESET:          return InternalCommand::Type::Reset;
        case ::injector::EMERGENCY_STOP: return InternalCommand::Type::EmergencyStop;
        default:                         return InternalCommand::Type::Arm;
    }
}

::injector::InjectorState stateToProto(state::InjectorState s) {
    switch (s) {
        case state::InjectorState::Idle:      return ::injector::IDLE;
        case state::InjectorState::Armed:     return ::injector::ARMED;
        case state::InjectorState::Injecting: return ::injector::INJECTING;
        case state::InjectorState::Paused:    return ::injector::PAUSED;
        case state::InjectorState::Completed: return ::injector::COMPLETED;
        case state::InjectorState::Fault:     return ::injector::FAULT;
    }
    return ::injector::IDLE;
}

::injector::EventType eventTypeToProto(state::EventType et) {
    switch (et) {
        case state::EventType::StateTransition: return ::injector::STATE_TRANSITION;
        case state::EventType::FaultDetected:   return ::injector::FAULT_DETECTED;
        case state::EventType::PhaseTransition: return ::injector::PHASE_TRANSITION;
        case state::EventType::ProtocolLoaded:  return ::injector::PROTOCOL_LOADED;
        case state::EventType::CommandRejected: return ::injector::COMMAND_REJECTED;
    }
    return ::injector::STATE_TRANSITION;
}

::injector::FaultType faultTypeToProto(state::FaultType ft) {
    switch (ft) {
        case state::FaultType::Overpressure:     return ::injector::OVERPRESSURE;
        case state::FaultType::AirBubble:        return ::injector::AIR_BUBBLE;
        case state::FaultType::MotorStall:       return ::injector::MOTOR_STALL;
        case state::FaultType::PartialOcclusion: return ::injector::PARTIAL_OCCLUSION;
        case state::FaultType::TimingDelay:      return ::injector::TIMING_DELAY;
    }
    return ::injector::OVERPRESSURE;
}

hal::SimulatedFault::Type protoFaultToHal(::injector::FaultType ft) {
    switch (ft) {
        case ::injector::OVERPRESSURE:      return hal::SimulatedFault::Type::Overpressure;
        case ::injector::AIR_BUBBLE:        return hal::SimulatedFault::Type::AirBubble;
        case ::injector::MOTOR_STALL:       return hal::SimulatedFault::Type::MotorStall;
        case ::injector::PARTIAL_OCCLUSION: return hal::SimulatedFault::Type::PartialOcclusion;
        case ::injector::TIMING_DELAY:      return hal::SimulatedFault::Type::TimingDelay;
        default:                            return hal::SimulatedFault::Type::Overpressure;
    }
}

void fillTelemetryFrame(::injector::TelemetryFrame* frame,
                        const TelemetrySnapshot& snap) {
    frame->set_timestamp(snap.timestampS);
    frame->set_state(stateToProto(snap.state));
    frame->set_phase_index(snap.phaseIndex);
    frame->set_target_flow_rate(snap.targetFlowRate);
    frame->set_actual_flow_rate(snap.actualFlowRate);
    frame->set_pressure(snap.pressure);
    frame->set_motor_rpm(snap.motorRpm);
    for (double v : snap.volumePerPhase) {
        frame->add_volume_delivered_per_phase(v);
    }
    frame->set_total_volume_delivered(snap.totalVolumeDelivered);
    frame->set_total_programmed_volume(snap.totalProgrammedVolume);
    frame->set_elapsed_time(snap.elapsedTime);
    frame->set_contrast_remaining(snap.contrastRemaining);
    frame->set_saline_remaining(snap.salineRemaining);
}

}  // namespace

// ── GrpcServiceImpl ─────────────────────────────────────────────────────────

GrpcServiceImpl::GrpcServiceImpl(const GrpcDependencies& deps)
    : deps_(deps) {}

grpc::Status GrpcServiceImpl::SendCommand(
    grpc::ServerContext* /*ctx*/,
    const ::injector::CommandRequest* request,
    ::injector::CommandResponse* response) {

    InternalCommand cmd;
    cmd.type = protoCommandToInternal(request->command());

    // Push to queue and let the state machine process it synchronously for
    // immediate validation feedback.
    auto result = deps_.stateMachine->processCommand(cmd);
    response->set_success(result.success);
    response->set_error(result.error);
    return grpc::Status::OK;
}

grpc::Status GrpcServiceImpl::LoadProtocol(
    grpc::ServerContext* /*ctx*/,
    const ::injector::LoadProtocolRequest* request,
    ::injector::CommandResponse* response) {

    // Convert proto phases to internal Protocol
    state::Protocol proto;
    for (const auto& pp : request->phases()) {
        state::Phase phase;
        phase.fluidType = (pp.fluid_type() == ::injector::SALINE)
                              ? state::FluidType::Saline
                              : state::FluidType::Contrast;
        phase.flowRate = pp.flow_rate();
        phase.volume = pp.volume();
        phase.pressureLimit = pp.pressure_limit();
        proto.phases.push_back(phase);
    }

    InternalCommand cmd;
    cmd.type = InternalCommand::Type::LoadProtocol;
    cmd.protocol = std::move(proto);

    auto result = deps_.stateMachine->processCommand(cmd);
    response->set_success(result.success);
    response->set_error(result.error);
    return grpc::Status::OK;
}

grpc::Status GrpcServiceImpl::InjectFault(
    grpc::ServerContext* /*ctx*/,
    const ::injector::InjectFaultRequest* request,
    ::injector::CommandResponse* response) {

    // Validate per spec
    auto ft = request->fault_type();
    if (ft == ::injector::OVERPRESSURE && request->target_psi() <= 0.0) {
        response->set_success(false);
        response->set_error("target_psi must be positive");
        return grpc::Status::OK;
    }
    if (ft == ::injector::MOTOR_STALL && request->max_rpm() < 0.0) {
        response->set_success(false);
        response->set_error("max_rpm must be non-negative");
        return grpc::Status::OK;
    }
    if (ft == ::injector::PARTIAL_OCCLUSION && request->resistance_multiplier() <= 0.0) {
        response->set_success(false);
        response->set_error("resistance_multiplier must be positive");
        return grpc::Status::OK;
    }
    if (ft == ::injector::TIMING_DELAY && request->delay_ms() <= 0.0) {
        response->set_success(false);
        response->set_error("delay_ms must be positive");
        return grpc::Status::OK;
    }

    hal::SimulatedFault fault;
    fault.type = protoFaultToHal(ft);
    fault.targetPsi = request->target_psi();
    fault.maxRpm = request->max_rpm();
    fault.resistanceMultiplier = request->resistance_multiplier();
    fault.delayMs = request->delay_ms();

    InternalCommand cmd;
    cmd.type = InternalCommand::Type::InjectFault;
    cmd.fault = fault;

    auto result = deps_.stateMachine->processCommand(cmd);
    response->set_success(result.success);
    response->set_error(result.error);
    return grpc::Status::OK;
}

grpc::Status GrpcServiceImpl::ExportData(
    grpc::ServerContext* /*ctx*/,
    const ::injector::ExportRequest* request,
    ::injector::ExportResponse* response) {

    const auto& fmt = request->format();
    if (fmt == "csv") {
        response->set_data(deps_.logger->exportCsv());
    } else if (fmt == "json") {
        response->set_data(deps_.logger->exportJson());
    } else {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Unsupported format: " + fmt + ". Use 'csv' or 'json'");
    }
    return grpc::Status::OK;
}

grpc::Status GrpcServiceImpl::GetState(
    grpc::ServerContext* /*ctx*/,
    const ::injector::Empty* /*request*/,
    ::injector::StateSnapshot* response) {

    // Current state
    response->set_state(stateToProto(deps_.stateMachine->currentState()));

    // Loaded protocol
    auto proto = deps_.stateMachine->loadedProtocol();
    for (const auto& phase : proto.phases) {
        auto* pp = response->add_loaded_protocol();
        pp->set_fluid_type(phase.fluidType == state::FluidType::Saline
                               ? ::injector::SALINE
                               : ::injector::CONTRAST);
        pp->set_flow_rate(phase.flowRate);
        pp->set_volume(phase.volume);
        pp->set_pressure_limit(phase.pressureLimit);
    }

    // Latest telemetry — drain the broadcast for the most recent snapshot
    TelemetrySnapshot latestSnap;
    bool hasSnap = false;
    while (auto snap = deps_.telemetry->pop()) {
        latestSnap = std::move(*snap);
        hasSnap = true;
    }
    if (hasSnap) {
        auto* frame = response->mutable_latest_telemetry();
        fillTelemetryFrame(frame, latestSnap);
    }

    // Recent events (last 100)
    auto allEvents = deps_.events->getEventsSince(0);
    size_t start = allEvents.size() > 100 ? allEvents.size() - 100 : 0;
    for (size_t i = start; i < allEvents.size(); ++i) {
        auto* ev = response->add_recent_events();
        ev->set_timestamp(static_cast<double>(allEvents[i].timestampUs) / 1e6);
        ev->set_type(eventTypeToProto(allEvents[i].type));
        ev->set_details(allEvents[i].detailsJson);
    }

    // Active faults
    auto faults = deps_.stateMachine->activeFaults();
    for (const auto& f : faults) {
        auto* fi = response->add_active_faults();
        fi->set_type(faultTypeToProto(f.type));
        fi->set_value(f.measuredValue);
        fi->set_threshold(f.threshold);
        fi->set_timestamp(static_cast<double>(f.timestampUs) / 1e6);
    }

    // Syringe levels
    if (deps_.hal) {
        response->set_contrast_remaining(
            deps_.hal->readSyringeVolume(hal::Barrel::Contrast));
        response->set_saline_remaining(
            deps_.hal->readSyringeVolume(hal::Barrel::Saline));
    }

    return grpc::Status::OK;
}

// ── Server-streaming RPCs ───────────────────────────────────────────────────

grpc::Status GrpcServiceImpl::StreamTelemetry(
    grpc::ServerContext* ctx,
    const ::injector::StreamConfig* request,
    grpc::ServerWriter<::injector::TelemetryFrame>* writer) {

    // Clamp rate_ms per spec
    int rateMs = request->rate_ms();
    if (rateMs <= 0) rateMs = deps_.defaultTelemetryRateMs;
    rateMs = std::clamp(rateMs, 20, 1000);

    auto interval = std::chrono::milliseconds(rateMs);

    spdlog::info("StreamTelemetry client connected (rate={}ms)", rateMs);

    while (!ctx->IsCancelled()) {
        // Drain the broadcast, keep only the latest snapshot
        TelemetrySnapshot latest;
        bool hasData = false;
        while (auto snap = deps_.telemetry->pop()) {
            latest = std::move(*snap);
            hasData = true;
        }

        if (hasData) {
            ::injector::TelemetryFrame frame;
            fillTelemetryFrame(&frame, latest);
            if (!writer->Write(frame)) {
                break;  // client disconnected
            }
        }

        std::this_thread::sleep_for(interval);
    }

    spdlog::info("StreamTelemetry client disconnected");
    return grpc::Status::OK;
}

grpc::Status GrpcServiceImpl::StreamEvents(
    grpc::ServerContext* ctx,
    const ::injector::Empty* /*request*/,
    grpc::ServerWriter<::injector::SystemEvent>* writer) {

    spdlog::info("StreamEvents client connected");

    size_t sequence = 0;
    while (!ctx->IsCancelled()) {
        auto events = deps_.events->waitForEvents(
            sequence, std::chrono::milliseconds(100));

        for (const auto& ev : events) {
            ::injector::SystemEvent protoEvent;
            protoEvent.set_timestamp(static_cast<double>(ev.timestampUs) / 1e6);
            protoEvent.set_type(eventTypeToProto(ev.type));
            protoEvent.set_details(ev.detailsJson);
            if (!writer->Write(protoEvent)) {
                spdlog::info("StreamEvents client disconnected");
                return grpc::Status::OK;
            }
        }
    }

    spdlog::info("StreamEvents client disconnected");
    return grpc::Status::OK;
}

// ── GrpcServer ──────────────────────────────────────────────────────────────

GrpcServer::GrpcServer(const std::string& address, const GrpcDependencies& deps)
    : address_(address), deps_(deps) {}

GrpcServer::~GrpcServer() {
    stop();
}

void GrpcServer::start() {
    if (running_.load()) return;

    thread_ = std::thread([this]() {
        GrpcServiceImpl service(deps_);

        grpc::ServerBuilder builder;
        builder.AddListeningPort(address_, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);

        server_ = builder.BuildAndStart();
        if (!server_) {
            spdlog::error("Failed to start gRPC server on {}", address_);
            return;
        }

        running_.store(true);
        spdlog::info("gRPC server listening on {}", address_);
        server_->Wait();
        running_.store(false);
    });

    // Wait briefly for the server to start
    using namespace std::chrono_literals;
    for (int i = 0; i < 50 && !running_.load(); ++i) {
        std::this_thread::sleep_for(10ms);
    }
}

void GrpcServer::stop() {
    if (server_) {
        server_->Shutdown();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    server_.reset();
    running_.store(false);
}

}  // namespace injector::comms
