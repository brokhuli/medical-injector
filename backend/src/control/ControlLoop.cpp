#include "control/ControlLoop.h"

#include <spdlog/spdlog.h>

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace injector::control {

ControlLoop::ControlLoop(std::shared_ptr<hal::IHalInterface> hal,
                         const ControlLoopConfig& config,
                         logging::RingBuffer<TickData>* tickBuffer)
    : hal_(std::move(hal)),
      config_(config),
      pid_(config.pid),
      tickBuffer_(tickBuffer) {}

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

        // 4. Run PID → commanded RPM
        double target = targetFlowRate_.load(std::memory_order_acquire);
        double commandedRpm = pid_.compute(target, actualFlowRate, dtSeconds);

        // 5. Write motor command
        hal_->setMotorRpm(commandedRpm);

        // 6. Integrate volume
        double vol = cumulativeVolume_.load(std::memory_order_relaxed) +
                     actualFlowRate * dtSeconds;
        cumulativeVolume_.store(vol, std::memory_order_relaxed);

        // 7. Push tick data to ring buffer
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

        // 8. Update timing stats
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

        // 9. Sleep until next tick
        nextTick += tickPeriod;
        std::this_thread::sleep_until(nextTick);
    }
}

}  // namespace injector::control
