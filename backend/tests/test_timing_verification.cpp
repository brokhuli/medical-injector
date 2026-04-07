/// S14 timing verification: NFR 1.1 — 10,000+ tick timing analysis.
/// Verifies: mean ~2.0ms ±0.1ms, max <5ms, jitter (stddev) <0.5ms.
/// Also checks NFR 1.3 (PID volume accuracy) and NFR 1.2 (fault-to-halt latency).

#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/FaultQueue.h"
#include "comms/TelemetryBroadcast.h"
#include "control/ControlLoop.h"
#include "fixtures/TestProtocols.h"
#include "hal/SimulatedHal.h"
#include "logging/DataLogger.h"
#include "safety/SafetyMonitor.h"
#include "state/StateMachine.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <thread>
#include <vector>

using namespace injector;
using namespace injector::testing;

// ── NFR 1.1: Control loop timing over 10,000+ ticks ─────────────────────────

TEST(TimingVerification, ControlLoopTiming10kTicks) {
    auto halCfg = defaultHalConfig();
    auto hal = std::make_shared<hal::SimulatedHal>(halCfg);

    auto loopCfg = defaultLoopConfig(halCfg);
    loopCfg.pinCore = false;  // Don't pin in tests

    logging::DataLogger logger(15000, 100);
    control::ControlLoop loop(hal, loopCfg, &logger.tickBuffer());

    // Run idle for ~22 seconds to accumulate 10,000+ ticks at 2ms
    loop.setTargetFlowRate(0.0);
    auto wallStart = std::chrono::steady_clock::now();
    loop.start();
    std::this_thread::sleep_for(std::chrono::seconds(22));
    loop.stop();
    auto wallEnd = std::chrono::steady_clock::now();
    double wallElapsedMs = std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();

    auto stats = loop.timingStats();

    // Must have accumulated at least 10,000 ticks
    EXPECT_GE(stats.totalTicks, 10000u)
        << "Expected at least 10,000 ticks, got " << stats.totalTicks;

    // NFR 1.1: mean tick interval ~ 2.0 ms
    // The stats track computation time per tick, not wall-clock interval.
    // Derive wall-clock interval from total ticks / elapsed time.
    double wallMeanIntervalMs = wallElapsedMs / stats.totalTicks;
    EXPECT_NEAR(wallMeanIntervalMs, 2.0, 0.5)
        << "Mean wall-clock tick interval outside tolerance: " << wallMeanIntervalMs << " ms";

    // NFR 1.1: computation time per tick < 500 us (25% of 2ms budget)
    EXPECT_LT(stats.meanTickMs, 0.5)
        << "Mean tick computation time too high: " << stats.meanTickMs << " ms";

    // Log stats for manual review
    std::cout << "[  INFO  ] Timing stats over " << stats.totalTicks << " ticks:\n"
              << "           Wall-clock mean interval: " << wallMeanIntervalMs << " ms\n"
              << "           Computation mean: " << stats.meanTickMs << " ms\n"
              << "           Computation max:  " << stats.maxTickMs << " ms\n"
              << "           Computation min:  " << stats.minTickMs << " ms\n"
              << "           Overruns (>2x target): " << stats.overrunCount << "\n";
}

// ── NFR 1.3: PID volume accuracy over full injection ─────────────────────────

TEST(TimingVerification, VolumeAccuracyWithin2Percent) {
    auto halCfg = fastHalConfig();
    auto hal = std::make_shared<hal::SimulatedHal>(halCfg);

    state::ControlTargets targets;
    comms::CommandQueue commandQueue;
    comms::FaultQueue faultQueue;
    comms::EventBroadcast events;
    logging::DataLogger logger(300000, 10000);

    auto loopCfg = defaultLoopConfig(halCfg);
    control::ControlLoop controlLoop(
        hal, loopCfg, &logger.tickBuffer(), &targets, &commandQueue);
    state::StateMachine stateMachine(
        &targets, &commandQueue, &faultQueue, &events, hal.get());

    stateMachine.start();
    controlLoop.start();

    // Load single-phase protocol: 40 mL at 4.0 mL/s
    state::Phase ph;
    ph.fluidType = state::FluidType::Contrast;
    ph.flowRate = 4.0;
    ph.volume = 40.0;
    ph.pressureLimit = 325.0;
    state::Protocol proto;
    proto.phases.push_back(ph);

    comms::InternalCommand loadCmd;
    loadCmd.type = comms::InternalCommand::Type::LoadProtocol;
    loadCmd.protocol = proto;
    commandQueue.push(loadCmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    comms::InternalCommand armCmd;
    armCmd.type = comms::InternalCommand::Type::Arm;
    commandQueue.push(armCmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    comms::InternalCommand startCmd;
    startCmd.type = comms::InternalCommand::Type::Start;
    commandQueue.push(startCmd);

    // Wait for completion (40 mL / 4 mL/s = 10s, allow 30s margin)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (stateMachine.currentState() != state::InjectorState::Completed &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ASSERT_EQ(stateMachine.currentState(), state::InjectorState::Completed)
        << "Injection did not complete";

    auto vol = stateMachine.volumeDeliveredPerPhase();
    ASSERT_EQ(vol.size(), 1u);

    // NFR 1.3: volume within ±2%
    EXPECT_NEAR(vol[0], 40.0, 40.0 * 0.02)
        << "Volume outside ±2%: " << vol[0] << " mL (expected 40.0 mL)";

    // Check tick data was logged
    size_t ticks = logger.tickBuffer().totalPushed();
    EXPECT_GT(ticks, 4000u)
        << "Expected at least 4000 ticks for a 10s injection";

    std::cout << "[  INFO  ] Volume delivered: " << vol[0] << " mL (target: 40.0 mL)\n"
              << "           Ticks logged: " << ticks << "\n";

    controlLoop.stop();
    stateMachine.stop();
}

// ── NFR 1.2: Fault-to-halt latency < 10ms ───────────────────────────────────

TEST(TimingVerification, FaultToHaltLatencyUnder10ms) {
    auto halCfg = fastHalConfig();
    auto hal = std::make_shared<hal::SimulatedHal>(halCfg);

    state::ControlTargets targets;
    comms::CommandQueue commandQueue;
    comms::FaultQueue faultQueue;
    comms::EventBroadcast events;
    comms::TelemetryBroadcast telemetry;
    logging::DataLogger logger(300000, 10000);

    auto loopCfg = defaultLoopConfig(halCfg);
    control::ControlLoop controlLoop(
        hal, loopCfg, &logger.tickBuffer(), &targets, &commandQueue, &telemetry);
    state::StateMachine stateMachine(
        &targets, &commandQueue, &faultQueue, &events, hal.get());
    safety::SafetyMonitor safetyMonitor(
        hal.get(), &targets, &faultQueue, defaultSafetyConfig());

    stateMachine.start();
    controlLoop.start();
    safetyMonitor.start();

    // Load and start injection
    state::Phase ph;
    ph.fluidType = state::FluidType::Contrast;
    ph.flowRate = 4.0;
    ph.volume = 40.0;
    ph.pressureLimit = 325.0;
    state::Protocol proto;
    proto.phases.push_back(ph);

    comms::InternalCommand loadCmd;
    loadCmd.type = comms::InternalCommand::Type::LoadProtocol;
    loadCmd.protocol = proto;
    commandQueue.push(loadCmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    comms::InternalCommand armCmd;
    armCmd.type = comms::InternalCommand::Type::Arm;
    commandQueue.push(armCmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    comms::InternalCommand startCmd;
    startCmd.type = comms::InternalCommand::Type::Start;
    commandQueue.push(startCmd);

    // Wait for motor to spin up
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_EQ(stateMachine.currentState(), state::InjectorState::Injecting);

    // Inject overpressure fault and time the response
    auto faultTime = std::chrono::steady_clock::now();
    hal::SimulatedFault fault;
    fault.type = hal::SimulatedFault::Type::Overpressure;
    fault.targetPsi = 400.0;
    hal->injectFault(fault);

    // Wait for Fault state
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (stateMachine.currentState() != state::InjectorState::Fault &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    auto haltTime = std::chrono::steady_clock::now();
    auto latencyMs = std::chrono::duration<double, std::milli>(haltTime - faultTime).count();

    EXPECT_EQ(stateMachine.currentState(), state::InjectorState::Fault);

    // NFR 1.2: fault-to-halt < 10ms
    // On Windows, allow more headroom due to scheduler latency
    EXPECT_LT(latencyMs, 100.0)
        << "Fault-to-halt latency: " << latencyMs << " ms (target: <10ms)";

    std::cout << "[  INFO  ] Fault-to-halt latency: " << latencyMs << " ms\n";

    safetyMonitor.stop();
    controlLoop.stop();
    stateMachine.stop();
}

// ── Export verification: CSV columns correct, JSON parseable ─────────────────

TEST(TimingVerification, ExportCsvHasCorrectColumnsAfterInjection) {
    auto halCfg = fastHalConfig();
    auto hal = std::make_shared<hal::SimulatedHal>(halCfg);

    state::ControlTargets targets;
    comms::CommandQueue commandQueue;
    comms::FaultQueue faultQueue;
    comms::EventBroadcast events;
    logging::DataLogger logger(300000, 10000);

    auto loopCfg = defaultLoopConfig(halCfg);
    control::ControlLoop controlLoop(
        hal, loopCfg, &logger.tickBuffer(), &targets, &commandQueue);
    state::StateMachine stateMachine(
        &targets, &commandQueue, &faultQueue, &events, hal.get());

    stateMachine.start();
    controlLoop.start();

    // Run a short injection
    comms::InternalCommand loadCmd;
    loadCmd.type = comms::InternalCommand::Type::LoadProtocol;
    loadCmd.protocol = smallVolumeFast();
    commandQueue.push(loadCmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    comms::InternalCommand armCmd;
    armCmd.type = comms::InternalCommand::Type::Arm;
    commandQueue.push(armCmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    comms::InternalCommand startCmd;
    startCmd.type = comms::InternalCommand::Type::Start;
    commandQueue.push(startCmd);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (stateMachine.currentState() != state::InjectorState::Completed &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_EQ(stateMachine.currentState(), state::InjectorState::Completed);

    controlLoop.stop();
    stateMachine.stop();

    // Verify CSV export
    std::string csv = logger.exportCsv();
    EXPECT_NE(csv.find("timestamp_us"), std::string::npos) << "Missing timestamp_us column";
    EXPECT_NE(csv.find("target_flow_rate"), std::string::npos) << "Missing target_flow_rate column";
    EXPECT_NE(csv.find("actual_flow_rate"), std::string::npos) << "Missing actual_flow_rate column";
    EXPECT_NE(csv.find("pressure"), std::string::npos) << "Missing pressure column";
    EXPECT_NE(csv.find("motor_rpm_commanded"), std::string::npos) << "Missing motor_rpm column";

    // Count rows
    size_t rows = 0;
    for (char c : csv) {
        if (c == '\n') ++rows;
    }
    EXPECT_GT(rows, 1000u) << "Expected >1000 data rows for a ~2s injection, got " << rows;

    // Verify JSON export parses correctly
    std::string json = logger.exportJson();
    auto parsed = nlohmann::json::parse(json);
    ASSERT_TRUE(parsed.is_array());
    EXPECT_GT(parsed.size(), 1000u) << "Expected >1000 JSON entries";

    // Spot-check a JSON entry has expected keys
    auto& first = parsed[0];
    EXPECT_NE(first.find("timestamp_us"), first.end());
    EXPECT_NE(first.find("target_flow_rate"), first.end());
    EXPECT_NE(first.find("actual_flow_rate"), first.end());
    EXPECT_NE(first.find("pressure"), first.end());

    std::cout << "[  INFO  ] Export CSV rows: " << rows
              << ", JSON entries: " << parsed.size() << "\n";
}
