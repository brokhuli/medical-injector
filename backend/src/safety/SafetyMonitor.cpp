#include "safety/SafetyMonitor.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

namespace injector::safety {

namespace {

uint64_t steadyNowUs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace

SafetyMonitor::SafetyMonitor(hal::IHalInterface* hal,
                              const state::ControlTargets* targets,
                              comms::FaultQueue* faultQueue,
                              const config::SafetyConfig& cfg)
    : hal_(hal), targets_(targets), faultQueue_(faultQueue), cfg_(cfg) {}

SafetyMonitor::~SafetyMonitor() {
    stop();
}

std::vector<state::FaultEvent> SafetyMonitor::checkOnce(uint64_t nowUs) {
    std::vector<state::FaultEvent> faults;

    // ── 1. Overpressure ──────────────────────────────────────────────────────
    double pressure = hal_->readPressure();
    double pressureLimit = cfg_.defaultPressureLimitPsi;
    if (targets_) {
        pressureLimit = targets_->pressureLimit.load(std::memory_order_acquire);
    }
    if (pressure > pressureLimit) {
        state::FaultEvent fe;
        fe.type = state::FaultType::Overpressure;
        fe.measuredValue = pressure;
        fe.threshold = pressureLimit;
        fe.timestampUs = nowUs;
        faults.push_back(fe);
    }

    // ── 2. Air bubble ────────────────────────────────────────────────────────
    if (hal_->readAirDetector()) {
        state::FaultEvent fe;
        fe.type = state::FaultType::AirBubble;
        fe.measuredValue = 1.0;
        fe.threshold = 0.0;
        fe.timestampUs = nowUs;
        faults.push_back(fe);
    }

    // ── 3. Motor divergence ──────────────────────────────────────────────────
    double commandedRpm = 0.0;
    if (targets_) {
        commandedRpm = targets_->lastCommandedRpm.load(std::memory_order_acquire);
    }
    double actualRpm = hal_->readMotorRpm();
    double divergence = std::abs(commandedRpm - actualRpm);

    if (divergence > cfg_.motorDivergenceThreshold) {
        ++motorDivergenceCount_;
        if (motorDivergenceCount_ >= cfg_.motorDivergenceTicks) {
            state::FaultEvent fe;
            fe.type = state::FaultType::MotorStall;
            fe.measuredValue = divergence;
            fe.threshold = cfg_.motorDivergenceThreshold;
            fe.timestampUs = nowUs;
            faults.push_back(fe);
        }
    } else {
        motorDivergenceCount_ = 0;
    }

    // ── 4. Control loop timing ───────────────────────────────────────────────
    // Only check when injecting to avoid false positives during idle.
    bool injecting = targets_ && targets_->injecting.load(std::memory_order_acquire);
    if (injecting && targets_) {
        uint64_t lastTickUs = targets_->lastTickTimestampUs.load(std::memory_order_acquire);
        if (lastTickUs > 0 && lastTickUs <= nowUs) {
            double elapsedMs = static_cast<double>(nowUs - lastTickUs) / 1000.0;
            double maxAllowedMs =
                static_cast<double>(cfg_.controlLoopTickRateMs) + cfg_.jitterToleranceMs;
            if (elapsedMs > maxAllowedMs) {
                state::FaultEvent fe;
                fe.type = state::FaultType::TimingDelay;
                fe.measuredValue = elapsedMs;
                fe.threshold = maxAllowedMs;
                fe.timestampUs = nowUs;
                faults.push_back(fe);
            }
        }
    }

    return faults;
}

void SafetyMonitor::resetCounters() {
    motorDivergenceCount_ = 0;
    faultReported_ = false;
}

void SafetyMonitor::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return;
    }
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&SafetyMonitor::run, this);
}

void SafetyMonitor::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool SafetyMonitor::running() const {
    return running_.load(std::memory_order_acquire);
}

void SafetyMonitor::setHighPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#else
    struct sched_param param {};
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
#endif
}

void SafetyMonitor::run() {
    setHighPriority();

    const auto tickPeriod =
        std::chrono::microseconds(cfg_.tickRateMs * 1000);
    auto nextTick = std::chrono::steady_clock::now();

    int lastObservedState = -1;

    while (running_.load(std::memory_order_acquire)) {
        // Auto-reset fault latch when state machine returns to Idle or Armed so
        // that each new injection run gets a clean slate. Without this, faultReported_
        // stays true after the first fault and the monitor never fires again.
        if (targets_) {
            int st = targets_->currentState.load(std::memory_order_acquire);
            if (st != lastObservedState) {
                auto stEnum = static_cast<state::InjectorState>(st);
                if (stEnum == state::InjectorState::Idle ||
                    stEnum == state::InjectorState::Armed) {
                    resetCounters();
                }
                lastObservedState = st;
            }
        }

        uint64_t nowUs = steadyNowUs();
        auto faults = checkOnce(nowUs);

        if (!faults.empty() && !faultReported_) {
            faultReported_ = true;

            for (const auto& fault : faults) {
                spdlog::warn("Safety fault: {} (value={:.2f}, threshold={:.2f})",
                             toString(fault.type), fault.measuredValue,
                             fault.threshold);

                // Emergency stop first, then notify state machine
                hal_->emergencyStop();

                if (faultQueue_) {
                    faultQueue_->push(fault);
                }
            }
        }

        nextTick += tickPeriod;
        std::this_thread::sleep_until(nextTick);
    }
}

}  // namespace injector::safety
