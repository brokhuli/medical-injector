#include "comms/CommandQueue.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using namespace injector::comms;
using namespace injector::state;
using Type = InternalCommand::Type;

TEST(CommandQueue, EmptyOnConstruction) {
    CommandQueue q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(CommandQueue, PopOnEmptyReturnsNullopt) {
    CommandQueue q;
    EXPECT_EQ(q.pop(), std::nullopt);
}

TEST(CommandQueue, PushSingleCommand) {
    CommandQueue q;
    InternalCommand cmd;
    cmd.type = Type::Arm;
    q.push(cmd);

    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1u);
}

TEST(CommandQueue, PopReturnsCommand) {
    CommandQueue q;
    InternalCommand cmd;
    cmd.type = Type::Start;
    q.push(cmd);

    auto result = q.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Type::Start);
    EXPECT_TRUE(q.empty());
}

TEST(CommandQueue, FifoOrdering) {
    CommandQueue q;

    InternalCommand cmd1;
    cmd1.type = Type::Arm;
    InternalCommand cmd2;
    cmd2.type = Type::Start;
    InternalCommand cmd3;
    cmd3.type = Type::Pause;

    q.push(cmd1);
    q.push(cmd2);
    q.push(cmd3);

    EXPECT_EQ(q.size(), 3u);
    EXPECT_EQ(q.pop()->type, Type::Arm);
    EXPECT_EQ(q.pop()->type, Type::Start);
    EXPECT_EQ(q.pop()->type, Type::Pause);
    EXPECT_TRUE(q.empty());
}

TEST(CommandQueue, SizeDecrementsOnPop) {
    CommandQueue q;
    InternalCommand cmd;
    cmd.type = Type::Reset;

    q.push(cmd);
    q.push(cmd);
    EXPECT_EQ(q.size(), 2u);

    (void)q.pop();
    EXPECT_EQ(q.size(), 1u);

    (void)q.pop();
    EXPECT_EQ(q.size(), 0u);
}

TEST(CommandQueue, LoadProtocolCommandCarriesProtocol) {
    CommandQueue q;

    InternalCommand cmd;
    cmd.type = Type::LoadProtocol;
    cmd.protocol.name = "TestProtocol";
    Phase p;
    p.fluidType = FluidType::Contrast;
    p.flowRate = 4.0;
    p.volume = 80.0;
    p.pressureLimit = 300.0;
    cmd.protocol.phases.push_back(p);

    q.push(cmd);

    auto result = q.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Type::LoadProtocol);
    EXPECT_EQ(result->protocol.name, "TestProtocol");
    ASSERT_EQ(result->protocol.phases.size(), 1u);
    EXPECT_DOUBLE_EQ(result->protocol.phases[0].flowRate, 4.0);
    EXPECT_DOUBLE_EQ(result->protocol.phases[0].volume, 80.0);
}

TEST(CommandQueue, InjectFaultCommandCarriesFault) {
    CommandQueue q;

    InternalCommand cmd;
    cmd.type = Type::InjectFault;
    cmd.fault.type = injector::hal::SimulatedFault::Type::Overpressure;
    cmd.fault.targetPsi = 400.0;

    q.push(cmd);

    auto result = q.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Type::InjectFault);
    EXPECT_EQ(result->fault.type, injector::hal::SimulatedFault::Type::Overpressure);
    EXPECT_DOUBLE_EQ(result->fault.targetPsi, 400.0);
}

TEST(CommandQueue, ConcurrentPushFromMultipleThreads) {
    CommandQueue q;
    const int kThreads = 4;
    const int kCommandsPerThread = 100;

    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&q, t, kCommandsPerThread] {
            for (int i = 0; i < kCommandsPerThread; ++i) {
                InternalCommand cmd;
                cmd.type = (t % 2 == 0) ? Type::Arm : Type::Disarm;
                q.push(cmd);
            }
        });
    }

    for (auto& th : producers) {
        th.join();
    }

    EXPECT_EQ(q.size(), static_cast<size_t>(kThreads * kCommandsPerThread));

    // Drain and count
    size_t count = 0;
    while (q.pop().has_value()) {
        ++count;
    }
    EXPECT_EQ(count, static_cast<size_t>(kThreads * kCommandsPerThread));
}

TEST(CommandQueue, AllCommandTypesRoundTrip) {
    CommandQueue q;
    const std::vector<Type> types = {
        Type::Arm, Type::Disarm, Type::Start, Type::Pause,
        Type::Resume, Type::Reset, Type::EmergencyStop,
        Type::LoadProtocol, Type::InjectFault
    };

    for (auto t : types) {
        InternalCommand cmd;
        cmd.type = t;
        q.push(cmd);
    }

    for (auto expected : types) {
        auto result = q.pop();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->type, expected);
    }
}
