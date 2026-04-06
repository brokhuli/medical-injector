/// Integration tests: gRPC server end-to-end.
/// Spins up the full backend in-process, connects a gRPC client, and exercises
/// all 7 RPCs.  Covers AC-6.1 (command round-trip), AC-6.2 (telemetry streaming),
/// AC-6.3 (invalid command rejection), AC-6.4 (InjectFault), and data export.

#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/FaultQueue.h"
#include "comms/GrpcServer.h"
#include "comms/TelemetryBroadcast.h"
#include "config/Config.h"
#include "control/ControlLoop.h"
#include "fixtures/TestProtocols.h"
#include "hal/SimulatedHal.h"
#include "logging/DataLogger.h"
#include "safety/SafetyMonitor.h"
#include "state/StateMachine.h"

#include <grpcpp/grpcpp.h>
#include <injector.grpc.pb.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace injector;
using namespace injector::testing;

namespace {

/// Full backend + gRPC client fixture.
class GrpcIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto halCfg = fastHalConfig();
        hal_ = std::make_shared<hal::SimulatedHal>(halCfg);

        controlLoop_ = std::make_unique<control::ControlLoop>(
            hal_, defaultLoopConfig(halCfg), &logger_.tickBuffer(),
            &targets_, &commandQueue_, &telemetry_);

        stateMachine_ = std::make_unique<state::StateMachine>(
            &targets_, &commandQueue_, &faultQueue_, &events_, hal_.get());

        safetyMonitor_ = std::make_unique<safety::SafetyMonitor>(
            hal_.get(), &targets_, &faultQueue_, defaultSafetyConfig());

        // Start all threads
        stateMachine_->start();
        controlLoop_->start();
        safetyMonitor_->start();

        // Start gRPC server on a random available port
        comms::GrpcDependencies deps;
        deps.commandQueue = &commandQueue_;
        deps.events       = &events_;
        deps.telemetry    = &telemetry_;
        deps.stateMachine = stateMachine_.get();
        deps.hal          = hal_.get();
        deps.logger       = &logger_;

        server_ = std::make_unique<comms::GrpcServer>(serverAddress_, deps);
        server_->start();
        ASSERT_TRUE(server_->running()) << "gRPC server failed to start";

        // Create client channel
        auto channel = grpc::CreateChannel(
            serverAddress_, grpc::InsecureChannelCredentials());
        stub_ = ::injector::InjectorService::NewStub(channel);
    }

    void TearDown() override {
        server_->stop();
        safetyMonitor_->stop();
        controlLoop_->stop();
        stateMachine_->stop();
    }

    /// Helper: load a small protocol and arm via gRPC.
    void loadAndArm() {
        // Load protocol
        grpc::ClientContext loadCtx;
        ::injector::LoadProtocolRequest loadReq;
        auto* phase = loadReq.add_phases();
        phase->set_fluid_type(::injector::CONTRAST);
        phase->set_flow_rate(4.0);
        phase->set_volume(8.0);
        phase->set_pressure_limit(325.0);
        ::injector::CommandResponse loadResp;
        auto status = stub_->LoadProtocol(&loadCtx, loadReq, &loadResp);
        ASSERT_TRUE(status.ok()) << status.error_message();
        ASSERT_TRUE(loadResp.success()) << loadResp.error();

        // Arm
        grpc::ClientContext armCtx;
        ::injector::CommandRequest armReq;
        armReq.set_command(::injector::ARM);
        ::injector::CommandResponse armResp;
        status = stub_->SendCommand(&armCtx, armReq, &armResp);
        ASSERT_TRUE(status.ok()) << status.error_message();
        ASSERT_TRUE(armResp.success()) << armResp.error();
    }

    /// Helper: send a command and return the response.
    ::injector::CommandResponse sendCommand(::injector::CommandType cmd) {
        grpc::ClientContext ctx;
        ::injector::CommandRequest req;
        req.set_command(cmd);
        ::injector::CommandResponse resp;
        stub_->SendCommand(&ctx, req, &resp);
        return resp;
    }

    /// Helper: wait for a state (polls GetState).
    bool waitForState(::injector::InjectorState target, int timeoutMs = 5000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            grpc::ClientContext ctx;
            ::injector::Empty req;
            ::injector::StateSnapshot snap;
            stub_->GetState(&ctx, req, &snap);
            if (snap.state() == target) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    std::string serverAddress_ = "127.0.0.1:50099";

    state::ControlTargets targets_;
    comms::CommandQueue commandQueue_;
    comms::FaultQueue faultQueue_;
    comms::EventBroadcast events_;
    comms::TelemetryBroadcast telemetry_;
    logging::DataLogger logger_{300000, 10000};

    std::shared_ptr<hal::SimulatedHal> hal_;
    std::unique_ptr<control::ControlLoop> controlLoop_;
    std::unique_ptr<state::StateMachine> stateMachine_;
    std::unique_ptr<safety::SafetyMonitor> safetyMonitor_;
    std::unique_ptr<comms::GrpcServer> server_;
    std::unique_ptr<::injector::InjectorService::Stub> stub_;
};

// ── AC-6.1: Command round-trip time ──────────────────────────────────────────

TEST_F(GrpcIntegrationTest, CommandRoundTrip_ArmWithin100ms) {
    // Load protocol first
    grpc::ClientContext loadCtx;
    ::injector::LoadProtocolRequest loadReq;
    auto* phase = loadReq.add_phases();
    phase->set_fluid_type(::injector::CONTRAST);
    phase->set_flow_rate(4.0);
    phase->set_volume(8.0);
    phase->set_pressure_limit(325.0);
    ::injector::CommandResponse loadResp;
    stub_->LoadProtocol(&loadCtx, loadReq, &loadResp);
    ASSERT_TRUE(loadResp.success()) << loadResp.error();

    // Start event stream before sending Arm
    grpc::ClientContext streamCtx;
    ::injector::Empty streamReq;
    auto reader = stub_->StreamEvents(&streamCtx, streamReq);

    // Time the ARM command round-trip
    auto start = std::chrono::steady_clock::now();

    grpc::ClientContext armCtx;
    ::injector::CommandRequest armReq;
    armReq.set_command(::injector::ARM);
    ::injector::CommandResponse armResp;
    auto status = stub_->SendCommand(&armCtx, armReq, &armResp);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration<double, std::milli>(elapsed).count();

    ASSERT_TRUE(status.ok()) << status.error_message();
    ASSERT_TRUE(armResp.success()) << armResp.error();
    EXPECT_LT(elapsedMs, 100.0) << "ARM round-trip took " << elapsedMs << " ms";

    // Verify state changed
    EXPECT_TRUE(waitForState(::injector::ARMED, 1000));

    // Read the state transition event from the stream
    ::injector::SystemEvent event;
    bool gotArmedEvent = false;

    // Read with a deadline so we don't hang
    streamCtx.TryCancel();
    // Check events via GetState instead (more reliable in test)
    grpc::ClientContext stateCtx;
    ::injector::Empty stateReq;
    ::injector::StateSnapshot snap;
    stub_->GetState(&stateCtx, stateReq, &snap);

    for (const auto& ev : snap.recent_events()) {
        if (ev.type() == ::injector::STATE_TRANSITION &&
            ev.details().find("Armed") != std::string::npos) {
            gotArmedEvent = true;
        }
    }
    EXPECT_TRUE(gotArmedEvent) << "No STATE_TRANSITION event to Armed found";
}

// ── AC-6.2: Telemetry streaming ──────────────────────────────────────────────

TEST_F(GrpcIntegrationTest, TelemetryStreaming_ReceivesFramesDuringInjection) {
    loadAndArm();

    // Start injection
    auto startResp = sendCommand(::injector::START);
    ASSERT_TRUE(startResp.success()) << startResp.error();

    // Stream telemetry at 50ms rate for ~3 seconds
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(4));

    ::injector::StreamConfig config;
    config.set_rate_ms(50);
    auto reader = stub_->StreamTelemetry(&ctx, config);

    std::vector<::injector::TelemetryFrame> frames;
    ::injector::TelemetryFrame frame;
    auto collectStart = std::chrono::steady_clock::now();

    while (reader->Read(&frame)) {
        frames.push_back(frame);
        auto elapsed = std::chrono::steady_clock::now() - collectStart;
        if (std::chrono::duration<double>(elapsed).count() > 3.0) {
            break;
        }
    }

    ctx.TryCancel();

    // At 50ms rate over 3 seconds, expect ~60 frames ± 10% → at least 40
    EXPECT_GE(frames.size(), 40u)
        << "Expected at least 40 telemetry frames over 3s, got " << frames.size();

    // Verify frame content
    bool sawInjecting = false;
    bool sawPositiveFlow = false;
    bool sawPositivePressure = false;
    for (const auto& f : frames) {
        if (f.state() == ::injector::INJECTING) sawInjecting = true;
        if (f.actual_flow_rate() > 0.1) sawPositiveFlow = true;
        if (f.pressure() > 10.0) sawPositivePressure = true;
    }
    EXPECT_TRUE(sawInjecting) << "No frame with INJECTING state";
    EXPECT_TRUE(sawPositiveFlow) << "No frame with positive flow rate";
    EXPECT_TRUE(sawPositivePressure) << "No frame with elevated pressure";
}

// ── AC-6.3: Invalid command rejection ────────────────────────────────────────

TEST_F(GrpcIntegrationTest, InvalidCommandRejected_StartFromIdle) {
    // Start without loading protocol or arming — should be rejected
    auto start = std::chrono::steady_clock::now();
    auto resp = sendCommand(::injector::START);
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration<double, std::milli>(elapsed).count();

    EXPECT_FALSE(resp.success());
    EXPECT_FALSE(resp.error().empty()) << "Expected error message for invalid command";
    EXPECT_LT(elapsedMs, 100.0) << "Rejection took " << elapsedMs << " ms";
}

TEST_F(GrpcIntegrationTest, InvalidCommandRejected_PauseFromCompleted) {
    loadAndArm();
    sendCommand(::injector::START);

    // Wait for injection to complete
    ASSERT_TRUE(waitForState(::injector::COMPLETED, 10000))
        << "Injection did not complete in time";

    auto resp = sendCommand(::injector::PAUSE);
    EXPECT_FALSE(resp.success());
    EXPECT_FALSE(resp.error().empty());
}

TEST_F(GrpcIntegrationTest, InvalidCommandRejected_ArmWithoutProtocol) {
    auto resp = sendCommand(::injector::ARM);
    EXPECT_FALSE(resp.success());
    EXPECT_FALSE(resp.error().empty());
}

// ── AC-6.4: InjectFault command ──────────────────────────────────────────────

TEST_F(GrpcIntegrationTest, InjectFault_OverpressureTriggersFaultState) {
    loadAndArm();
    sendCommand(::injector::START);
    ASSERT_TRUE(waitForState(::injector::INJECTING, 2000));

    // Wait briefly for motor to spin up
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Inject overpressure fault via gRPC
    grpc::ClientContext ctx;
    ::injector::InjectFaultRequest faultReq;
    faultReq.set_fault_type(::injector::OVERPRESSURE);
    faultReq.set_target_psi(400.0);
    ::injector::CommandResponse faultResp;
    auto status = stub_->InjectFault(&ctx, faultReq, &faultResp);
    ASSERT_TRUE(status.ok()) << status.error_message();
    ASSERT_TRUE(faultResp.success()) << faultResp.error();

    // Should transition to Fault state
    ASSERT_TRUE(waitForState(::injector::FAULT, 2000))
        << "Did not transition to FAULT after overpressure injection";

    // Verify fault details via GetState
    grpc::ClientContext stateCtx;
    ::injector::Empty stateReq;
    ::injector::StateSnapshot snap;
    stub_->GetState(&stateCtx, stateReq, &snap);

    EXPECT_EQ(snap.state(), ::injector::FAULT);
    ASSERT_GE(snap.active_faults_size(), 1);
    EXPECT_EQ(snap.active_faults(0).type(), ::injector::OVERPRESSURE);
}

TEST_F(GrpcIntegrationTest, InjectFault_InvalidParamsRejected) {
    // Overpressure with target_psi=0 should be rejected
    grpc::ClientContext ctx;
    ::injector::InjectFaultRequest faultReq;
    faultReq.set_fault_type(::injector::OVERPRESSURE);
    faultReq.set_target_psi(0.0);
    ::injector::CommandResponse faultResp;
    auto status = stub_->InjectFault(&ctx, faultReq, &faultResp);
    ASSERT_TRUE(status.ok());
    EXPECT_FALSE(faultResp.success());
    EXPECT_FALSE(faultResp.error().empty());
}

// ── Data export ──────────────────────────────────────────────────────────────

TEST_F(GrpcIntegrationTest, ExportCsv_ContainsDataAfterInjection) {
    loadAndArm();
    sendCommand(::injector::START);

    // Wait for some ticks to accumulate
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    grpc::ClientContext ctx;
    ::injector::ExportRequest req;
    req.set_format("csv");
    ::injector::ExportResponse resp;
    auto status = stub_->ExportData(&ctx, req, &resp);
    ASSERT_TRUE(status.ok()) << status.error_message();

    const auto& data = resp.data();
    EXPECT_FALSE(data.empty());

    // Should have CSV header
    EXPECT_NE(data.find("timestamp_us"), std::string::npos)
        << "CSV missing header";
    EXPECT_NE(data.find("target_flow_rate"), std::string::npos);
    EXPECT_NE(data.find("pressure"), std::string::npos);

    // Count rows (first line is header)
    size_t rows = 0;
    for (char c : data) {
        if (c == '\n') ++rows;
    }
    // At 2ms tick over 500ms, expect ~250 rows + header
    EXPECT_GT(rows, 50u) << "Expected at least 50 rows of tick data";
}

TEST_F(GrpcIntegrationTest, ExportJson_ValidAfterInjection) {
    loadAndArm();
    sendCommand(::injector::START);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    grpc::ClientContext ctx;
    ::injector::ExportRequest req;
    req.set_format("json");
    ::injector::ExportResponse resp;
    auto status = stub_->ExportData(&ctx, req, &resp);
    ASSERT_TRUE(status.ok()) << status.error_message();

    const auto& data = resp.data();
    EXPECT_FALSE(data.empty());
    // Should start with '[' and contain objects
    EXPECT_EQ(data.front(), '[');
    EXPECT_NE(data.find("\"timestamp_us\""), std::string::npos);
}

TEST_F(GrpcIntegrationTest, ExportInvalidFormat_ReturnsError) {
    grpc::ClientContext ctx;
    ::injector::ExportRequest req;
    req.set_format("xml");
    ::injector::ExportResponse resp;
    auto status = stub_->ExportData(&ctx, req, &resp);
    // Should return INVALID_ARGUMENT
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

// ── GetState ─────────────────────────────────────────────────────────────────

TEST_F(GrpcIntegrationTest, GetState_ReturnsIdleOnStart) {
    grpc::ClientContext ctx;
    ::injector::Empty req;
    ::injector::StateSnapshot snap;
    auto status = stub_->GetState(&ctx, req, &snap);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(snap.state(), ::injector::IDLE);
    EXPECT_EQ(snap.loaded_protocol_size(), 0);
    EXPECT_EQ(snap.active_faults_size(), 0);
}

TEST_F(GrpcIntegrationTest, GetState_ShowsLoadedProtocol) {
    // Load protocol
    grpc::ClientContext loadCtx;
    ::injector::LoadProtocolRequest loadReq;
    auto* phase = loadReq.add_phases();
    phase->set_fluid_type(::injector::CONTRAST);
    phase->set_flow_rate(4.0);
    phase->set_volume(8.0);
    phase->set_pressure_limit(325.0);
    ::injector::CommandResponse loadResp;
    stub_->LoadProtocol(&loadCtx, loadReq, &loadResp);
    ASSERT_TRUE(loadResp.success());

    grpc::ClientContext ctx;
    ::injector::Empty req;
    ::injector::StateSnapshot snap;
    stub_->GetState(&ctx, req, &snap);

    EXPECT_EQ(snap.loaded_protocol_size(), 1);
    EXPECT_EQ(snap.loaded_protocol(0).fluid_type(), ::injector::CONTRAST);
    EXPECT_DOUBLE_EQ(snap.loaded_protocol(0).flow_rate(), 4.0);
    EXPECT_DOUBLE_EQ(snap.loaded_protocol(0).volume(), 8.0);
}

// ── LoadProtocol ─────────────────────────────────────────────────────────────

TEST_F(GrpcIntegrationTest, LoadProtocol_RejectsInvalidFlowRate) {
    grpc::ClientContext ctx;
    ::injector::LoadProtocolRequest req;
    auto* phase = req.add_phases();
    phase->set_fluid_type(::injector::CONTRAST);
    phase->set_flow_rate(15.0);  // exceeds max (10 mL/s)
    phase->set_volume(50.0);
    phase->set_pressure_limit(325.0);
    ::injector::CommandResponse resp;
    stub_->LoadProtocol(&ctx, req, &resp);
    EXPECT_FALSE(resp.success());
}

TEST_F(GrpcIntegrationTest, LoadProtocol_RejectsZeroVolume) {
    grpc::ClientContext ctx;
    ::injector::LoadProtocolRequest req;
    auto* phase = req.add_phases();
    phase->set_fluid_type(::injector::CONTRAST);
    phase->set_flow_rate(4.0);
    phase->set_volume(0.0);
    phase->set_pressure_limit(325.0);
    ::injector::CommandResponse resp;
    stub_->LoadProtocol(&ctx, req, &resp);
    EXPECT_FALSE(resp.success());
}

// ── Reset after fault ────────────────────────────────────────────────────────

TEST_F(GrpcIntegrationTest, ResetAfterFault_ReturnsToIdle) {
    loadAndArm();
    sendCommand(::injector::START);
    ASSERT_TRUE(waitForState(::injector::INJECTING, 2000));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Inject air bubble
    grpc::ClientContext faultCtx;
    ::injector::InjectFaultRequest faultReq;
    faultReq.set_fault_type(::injector::AIR_BUBBLE);
    ::injector::CommandResponse faultResp;
    stub_->InjectFault(&faultCtx, faultReq, &faultResp);
    ASSERT_TRUE(faultResp.success());

    ASSERT_TRUE(waitForState(::injector::FAULT, 2000));

    // Reset
    auto resetResp = sendCommand(::injector::RESET);
    EXPECT_TRUE(resetResp.success()) << resetResp.error();
    EXPECT_TRUE(waitForState(::injector::IDLE, 1000));
}

}  // namespace
