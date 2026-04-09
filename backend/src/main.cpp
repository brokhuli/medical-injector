#include "comms/CommandQueue.h"
#include "comms/EventBroadcast.h"
#include "comms/FaultQueue.h"
#include "comms/GrpcServer.h"
#include "comms/TelemetryBroadcast.h"
#include "config/Config.h"
#include "control/ControlLoop.h"
#include "hal/SimulatedHal.h"
#include "logging/DataLogger.h"
#include "safety/SafetyMonitor.h"
#include "state/Protocol.h"
#include "state/StateMachine.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <csignal>
#include <thread>

namespace {
std::atomic<bool> g_shutdown{false};
void signalHandler(int) { g_shutdown.store(true); }
}  // namespace

int main(int argc, char* argv[]) {
    spdlog::info("Medical Injector Simulator — backend starting");

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Load config (optional file path as first argument)
    auto cfg = (argc > 1) ? injector::config::Config::loadFromFile(argv[1])
                          : injector::config::Config::defaults();

    // ── HAL ──────────────────────────────────────────────────────────────────
    injector::hal::HalConfig halCfg;
    halCfg.flowPerRpm         = cfg.hal.flowPerRpm;
    halCfg.tubingResistance   = cfg.hal.tubingResistance;
    halCfg.baselinePressure   = cfg.hal.baselinePressure;
    halCfg.motorTimeConstantMs = cfg.hal.motorTimeConstantMs;
    halCfg.motorMaxRpm        = cfg.hal.motorMaxRpm;
    halCfg.contrastVolumeMl   = cfg.syringe.contrastVolumeMl;
    halCfg.salineVolumeMl     = cfg.syringe.salineVolumeMl;

    auto hal = std::make_shared<injector::hal::SimulatedHal>(halCfg);

    spdlog::info("HAL initialized:");
    spdlog::info("  Pressure:  {:.1f} psi", hal->readPressure());
    spdlog::info("  Motor RPM: {:.1f}",     hal->readMotorRpm());
    spdlog::info("  Contrast:  {:.1f} mL",
                 hal->readSyringeVolume(injector::hal::Barrel::Contrast));
    spdlog::info("  Saline:    {:.1f} mL",
                 hal->readSyringeVolume(injector::hal::Barrel::Saline));

    // ── Shared inter-thread channels ─────────────────────────────────────────
    injector::state::ControlTargets targets;
    injector::comms::CommandQueue    commandQueue;
    injector::comms::FaultQueue      faultQueue;
    injector::comms::EventBroadcast  events;
    injector::comms::TelemetryBroadcast telemetry;

    // ── Data logger ───────────────────────────────────────────────────────────
    injector::logging::DataLogger logger(
        static_cast<size_t>(cfg.logging.ringBufferSize),
        static_cast<size_t>(cfg.logging.maxEvents));

    // ── Control loop ──────────────────────────────────────────────────────────
    injector::control::ControlLoopConfig loopCfg;
    loopCfg.tickRateMs      = cfg.control.tickRateMs;
    loopCfg.pinCore         = cfg.control.pinCore;
    loopCfg.flowPerRpm      = cfg.hal.flowPerRpm;
    loopCfg.pid.kp          = cfg.pid.kp;
    loopCfg.pid.ki          = cfg.pid.ki;
    loopCfg.pid.kd          = cfg.pid.kd;
    loopCfg.pid.iTermMax    = cfg.pid.iTermMax;
    loopCfg.pid.maxRpm      = cfg.pid.maxRpm;
    loopCfg.pid.maxAcceleration = cfg.pid.maxAcceleration;
    loopCfg.motorTimeConstantS  = cfg.hal.motorTimeConstantMs / 1000.0;

    injector::control::ControlLoop controlLoop(
        hal, loopCfg, &logger.tickBuffer(), &targets, &commandQueue, &telemetry);

    // ── Safety monitor ────────────────────────────────────────────────────────
    injector::config::SafetyConfig safetyCfg = cfg.safety;
    injector::safety::SafetyMonitor safetyMonitor(
        hal.get(), &targets, &faultQueue, safetyCfg);

    // ── State machine ─────────────────────────────────────────────────────────
    injector::state::StateMachine stateMachine(
        &targets, &commandQueue, &faultQueue, &events, hal.get());

    // ── Start threads ─────────────────────────────────────────────────────────
    spdlog::info("Starting state machine, safety monitor, control loop...");
    stateMachine.start();
    safetyMonitor.start();
    controlLoop.start();

    // ── gRPC server ──────────────────────────────────────────────────────────
    std::string address = "0.0.0.0:" + std::to_string(cfg.server.port);

    injector::comms::GrpcDependencies deps;
    deps.commandQueue = &commandQueue;
    deps.events       = &events;
    deps.telemetry    = &telemetry;
    deps.stateMachine = &stateMachine;
    deps.hal          = hal.get();
    deps.logger       = &logger;

    injector::comms::GrpcServer grpcServer(address, deps);
    grpcServer.start();

    if (!grpcServer.running()) {
        spdlog::error("gRPC server failed to start");
        safetyMonitor.stop();
        controlLoop.stop();
        stateMachine.stop();
        return 1;
    }

    spdlog::info("Backend ready. Ctrl+C to shut down.");

    // ── Wait for shutdown signal ─────────────────────────────────────────────
    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    spdlog::info("Shutting down...");
    grpcServer.stop();
    safetyMonitor.stop();
    controlLoop.stop();
    stateMachine.stop();
    spdlog::info("Done.");

    return 0;
}
