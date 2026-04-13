#include "control/ControlLoop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace injector::control {

ControlLoop::ControlLoop(std::shared_ptr<hal::IHalInterface> hal,
                         const ControlLoopConfig& config,
                         logging::RingBuffer<TickData>* tickBuffer,
                         state::ControlTargets* targets,
                         comms::CommandQueue* commandQueue,
                         comms::TelemetryBroadcast* telemetryBroadcast)
    : hal_(std::move(hal)),
      config_(config),
      pid_(config.pid),
      tickBuffer_(tickBuffer),
      targets_(targets),
      commandQueue_(commandQueue),
      telemetryBroadcast_(telemetryBroadcast) {}

ControlLoop::~ControlLoop() {
    stop();
}

void ControlLoop::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return;
    }

    running_.store(true, std::memory_order_release);
    pid_.reset();
    cumulativeVolume_.store(0.0, std::memory_order_relaxed);
    totalTicks_.store(0, std::memory_order_relaxed);
    maxTickMs_.store(0.0, std::memory_order_relaxed);
    minTickMs_.store(1e9, std::memory_order_relaxed);
    overrunCount_.store(0, std::memory_order_relaxed);

    startTime_ = std::chrono::steady_clock::now();
    thread_ = std::thread(&ControlLoop::run, this);
}

void ControlLoop::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ControlLoop::setTargetFlowRate(double mlPerSec) {
    targetFlowRate_.store(mlPerSec, std::memory_order_release);
}

double ControlLoop::targetFlowRate() const {
    return targetFlowRate_.load(std::memory_order_acquire);
}

double ControlLoop::cumulativeVolume() const {
    return cumulativeVolume_.load(std::memory_order_acquire);
}

TimingStats ControlLoop::timingStats() const {
    TimingStats stats;
    stats.totalTicks = totalTicks_.load(std::memory_order_acquire);
    stats.maxTickMs = maxTickMs_.load(std::memory_order_acquire);
    stats.minTickMs = minTickMs_.load(std::memory_order_acquire);
    stats.overrunCount = overrunCount_.load(std::memory_order_acquire);
    if (stats.totalTicks > 0) {
        stats.meanTickMs = meanTickMs_.load(std::memory_order_acquire);
    }
    return stats;
}

bool ControlLoop::running() const {
    return running_.load(std::memory_order_acquire);
}

void ControlLoop::pinToCore() {
    if (!config_.pinCore) {
        return;
    }

#ifdef _WIN32
    DWORD_PTR mask = 1;  // Pin to core 0
    SetThreadAffinityMask(GetCurrentThread(), mask);
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

void ControlLoop::run() {
    pinToCore();

    const auto tickPeriod =
        std::chrono::microseconds(config_.tickRateMs * 1000);
    const double dtSeconds = config_.tickRateMs / 1000.0;

    auto nextTick = std::chrono::steady_clock::now();
    double tickMsSum = 0.0;

    // Per-phase volume tracking (local to run thread — no atomics needed)
    int lastPhaseIndex = -1;
    double phaseVolumeDelivered = 0.0;
    bool phaseCompletePosted = false;
    bool decelLatched = false;

    // Elapsed-time tracking: the reported value is "seconds since INJECTING
    // entry", frozen while PAUSED, and zero in IDLE/ARMED. The backend is the
    // authoritative source — the frontend displays snap.elapsedTime verbatim.
    double injectionStartTimestampS = -1.0;
    double frozenElapsedS = 0.0;

    // Per-phase delivered-volume vector streamed over telemetry so the
    // frontend does not need to reconstruct it from total volume + phase
    // targets. Cleared when state returns to IDLE/ARMED.
    std::vector<double> volumePerPhase;
    int lastObservedState = -1;

    while (running_.load(std::memory_order_acquire)) {
        auto tickStart = std::chrono::steady_clock::now();

        // 1. Tick the HAL (advance physics simulation)
        hal_->tick(dtSeconds);

        // 2. Read sensors
        double pressure = hal_->readPressure();
        double motorRpmActual = hal_->readMotorRpm();
        double contrastRemaining =
            hal_->readSyringeVolume(hal::Barrel::Contrast);
        double salineRemaining = hal_->readSyringeVolume(hal::Barrel::Saline);

        // 3. Estimate actual flow from motor RPM (linear model: flow = rpm * flowPerRpm)
        double actualFlowRate = motorRpmActual * config_.flowPerRpm;

        // 4. Determine target and whether to inject
        double target;
        bool injecting;
        if (targets_) {
            target = targets_->targetFlowRate.load(std::memory_order_acquire);
            injecting = targets_->injecting.load(std::memory_order_acquire);

            // Set the appropriate valve based on active fluid channel
            int ch = targets_->activeFluidChannel.load(std::memory_order_acquire);
            hal_->setValve(hal::FluidChannel::Contrast,
                           (injecting && ch == 0) ? hal::ValveState::Open
                                                  : hal::ValveState::Closed);
            hal_->setValve(hal::FluidChannel::Saline,
                           (injecting && ch == 1) ? hal::ValveState::Open
                                                  : hal::ValveState::Closed);
        } else {
            target = targetFlowRate_.load(std::memory_order_acquire);
            injecting = (target > 0.0);
        }

        // 4a. State-change observation: when state returns to IDLE/ARMED we
        //     clear the per-phase volume vector and other per-run state so a
        //     fresh injection starts from a clean slate. COMPLETED/FAULT keep
        //     their final values so the UI can display them.
        if (targets_) {
            int curState = targets_->currentState.load(std::memory_order_acquire);
            if (curState != lastObservedState) {
                auto curStateEnum = static_cast<state::InjectorState>(curState);
                if (curStateEnum == state::InjectorState::Idle ||
                    curStateEnum == state::InjectorState::Armed) {
                    volumePerPhase.clear();
                    lastPhaseIndex = -1;
                    phaseVolumeDelivered = 0.0;
                    phaseCompletePosted = false;
                    decelLatched = false;
                }
                lastObservedState = curState;
            }
        }

        // 4b. Phase-transition detection: reset per-phase state when the
        //     active phase index changes. Flow rate is held constant across
        //     the transition because the end-of-phase decel latch (step 5)
        //     only fires on the final phase; the pressure dip seen on the
        //     trace comes from the pressure model's first-order lag
        //     reacting to the new fluid's resistance after the valve
        //     switches.
        if (targets_ && injecting) {
            int phaseIdx = targets_->activePhaseIndex.load(std::memory_order_acquire);
            if (phaseIdx != lastPhaseIndex) {
                // Freeze the outgoing phase's final delivered volume before
                // resetting. On the first entry (lastPhaseIndex == -1) there
                // is nothing to freeze.
                if (lastPhaseIndex >= 0 &&
                    static_cast<size_t>(lastPhaseIndex) < volumePerPhase.size()) {
                    volumePerPhase[lastPhaseIndex] = phaseVolumeDelivered;
                }
                lastPhaseIndex = phaseIdx;
                phaseVolumeDelivered = 0.0;
                phaseCompletePosted = false;
                decelLatched = false;
                if (phaseIdx >= 0 &&
                    static_cast<size_t>(phaseIdx) >= volumePerPhase.size()) {
                    volumePerPhase.resize(phaseIdx + 1, 0.0);
                }
            }
        }

        // 5. Run PID → commanded RPM
        //    Trigger end-of-phase decel when the remaining volume falls
        //    below the volume that would be delivered if we decelerated
        //    from the current actual flow to 0 right now.
        double commandedRpm = 0.0;
        double decelRateOverride = -1.0;
        if (injecting) {
            double adjustedTarget = target;

            // End-of-injection decel trigger based on current actual flow.
            // The HAL owns the motor model and predicts how much volume
            // would be delivered if we decelerated from the current flow
            // to zero at the given decel rate.
            //
            // Only the FINAL phase decelerates. Intermediate phase boundaries
            // hand flow off continuously — the target is the same on both
            // sides, so there is no reason to ramp down and back up. Gating
            // on `phaseIndex == totalPhases - 1` preserves overshoot
            // protection on the last phase without creating a V-shaped dip
            // at every inter-phase transition.
            //
            // Once triggered we LATCH for the rest of the phase. Without the
            // latch, after trigger `remaining` drops slower than `decelVolume`
            // so a plain inequality check would flip-flop between full target
            // and zero each tick.
            if (targets_) {
                int phaseIndex = targets_->activePhaseIndex.load(std::memory_order_acquire);
                double phaseTarget = targets_->currentPhaseVolumeTarget.load(
                    std::memory_order_acquire);
                int totalPhases = targets_->totalPhases.load(std::memory_order_acquire);
                const bool isFinalPhase =
                    totalPhases > 0 && phaseIndex == totalPhases - 1;
                if (phaseIndex >= 0 && phaseTarget > 0.0 && isFinalPhase) {
                    // Pressure-aware decel: scale the decel rate with pressure
                    // headroom so the ramp engages sooner *and* steeper as
                    // pressure climbs toward the phase limit. Below the
                    // threshold, scale=1 and behavior matches the old fixed
                    // rate exactly.
                    double pLimit = targets_->pressureLimit.load(
                        std::memory_order_acquire);
                    double effectiveDecel = pressureAwareDecelRate(
                        config_.pid.maxAcceleration, pressure, pLimit,
                        config_.pid.decelPressureThreshold,
                        config_.pid.decelPressureGain);

                    if (!decelLatched) {
                        double remaining = phaseTarget - phaseVolumeDelivered;
                        double decelVolume =
                            hal_->predictDecelVolume(effectiveDecel);
                        if (remaining <= decelVolume) {
                            decelLatched = true;
                        }
                    }
                    if (decelLatched) {
                        adjustedTarget = 0.0;
                        decelRateOverride = effectiveDecel;
                    }
                }
            }

            commandedRpm = pid_.compute(adjustedTarget, actualFlowRate, dtSeconds,
                                        decelRateOverride);
        } else {
            pid_.reset();
        }

        // 6. Write motor command
        hal_->setMotorRpm(commandedRpm);

        // 7. Publish to ControlTargets for safety monitor
        if (targets_) {
            targets_->lastCommandedRpm.store(commandedRpm, std::memory_order_release);
            auto nowUs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
            targets_->lastTickTimestampUs.store(nowUs, std::memory_order_release);
        }

        // 8. Integrate volume
        double vol = cumulativeVolume_.load(std::memory_order_relaxed) +
                     actualFlowRate * dtSeconds;
        cumulativeVolume_.store(vol, std::memory_order_relaxed);

        // 8b. Phase volume tracking — detect completion and post PhaseComplete.
        //     Phase index changes and state resets are handled in step 4b.
        if (targets_ && commandQueue_ && injecting) {
            int phaseIndex = targets_->activePhaseIndex.load(std::memory_order_acquire);
            double phaseTarget = targets_->currentPhaseVolumeTarget.load(
                std::memory_order_acquire);

            if (phaseIndex >= 0 && phaseTarget > 0.0 && !phaseCompletePosted) {
                phaseVolumeDelivered += actualFlowRate * dtSeconds;

                // Mirror the live running tally into the streamed vector.
                if (static_cast<size_t>(phaseIndex) < volumePerPhase.size()) {
                    volumePerPhase[phaseIndex] = phaseVolumeDelivered;
                }

                if (phaseVolumeDelivered >= phaseTarget) {
                    phaseCompletePosted = true;  // post once per phase
                    comms::InternalCommand cmd;
                    cmd.type = comms::InternalCommand::Type::PhaseComplete;
                    cmd.phaseIndex = phaseIndex;
                    cmd.volumeDelivered = phaseVolumeDelivered;
                    commandQueue_->push(cmd);
                }
            }
        }

        // 9. Push tick data to ring buffer
        if (tickBuffer_) {
            auto elapsed = tickStart - startTime_;
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          elapsed)
                          .count();

            TickData td;
            td.timestampUs = static_cast<uint64_t>(us);
            td.targetFlowRate = target;
            td.actualFlowRate = actualFlowRate;
            td.pressure = pressure;
            td.motorRpmCommanded = commandedRpm;
            td.motorRpmActual = motorRpmActual;
            td.contrastRemaining = contrastRemaining;
            td.salineRemaining = salineRemaining;
            tickBuffer_->push(td);
        }

        // 9b. Push telemetry snapshot for gRPC streaming
        if (telemetryBroadcast_ && targets_) {
            comms::TelemetrySnapshot snap;
            auto elapsed = tickStart - startTime_;
            snap.timestampS = std::chrono::duration<double>(elapsed).count();
            snap.state = static_cast<state::InjectorState>(
                targets_->currentState.load(std::memory_order_acquire));
            snap.phaseIndex = targets_->activePhaseIndex.load(std::memory_order_acquire);
            snap.targetFlowRate = target;
            snap.actualFlowRate = actualFlowRate;
            snap.pressure = pressure;
            snap.motorRpm = motorRpmActual;
            snap.totalVolumeDelivered = vol;
            snap.contrastRemaining = contrastRemaining;
            snap.salineRemaining = salineRemaining;
            snap.volumePerPhase = volumePerPhase;

            // Compute elapsed time from the current state. This replaces the
            // previous frontend-side reinterpretation in InjectorBridge.
            const auto stateEnum = static_cast<state::InjectorState>(
                targets_->currentState.load(std::memory_order_acquire));
            double elapsedReport = 0.0;
            switch (stateEnum) {
                case state::InjectorState::Injecting:
                case state::InjectorState::Completed:
                case state::InjectorState::Fault:
                    if (injectionStartTimestampS < 0.0) {
                        injectionStartTimestampS = snap.timestampS;
                    }
                    elapsedReport = snap.timestampS - injectionStartTimestampS;
                    frozenElapsedS = elapsedReport;
                    break;
                case state::InjectorState::Paused:
                    elapsedReport = frozenElapsedS;
                    break;
                case state::InjectorState::Idle:
                case state::InjectorState::Armed:
                    injectionStartTimestampS = -1.0;
                    frozenElapsedS = 0.0;
                    elapsedReport = 0.0;
                    break;
            }
            snap.elapsedTime = elapsedReport;
            telemetryBroadcast_->push(std::move(snap));
        }

        // 10. Update timing stats
        auto tickEnd = std::chrono::steady_clock::now();
        double tickMs =
            std::chrono::duration<double, std::milli>(tickEnd - tickStart)
                .count();

        uint64_t ticks =
            totalTicks_.fetch_add(1, std::memory_order_relaxed) + 1;
        tickMsSum += tickMs;
        meanTickMs_.store(tickMsSum / ticks, std::memory_order_relaxed);

        double currentMax = maxTickMs_.load(std::memory_order_relaxed);
        if (tickMs > currentMax) {
            maxTickMs_.store(tickMs, std::memory_order_relaxed);
        }
        double currentMin = minTickMs_.load(std::memory_order_relaxed);
        if (tickMs < currentMin) {
            minTickMs_.store(tickMs, std::memory_order_relaxed);
        }
        if (tickMs > config_.tickRateMs * 2.0) {
            overrunCount_.fetch_add(1, std::memory_order_relaxed);
        }

        // 11. Sleep until next tick
        nextTick += tickPeriod;
        std::this_thread::sleep_until(nextTick);
    }
}

}  // namespace injector::control
