#include "logging/RingBuffer.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using namespace injector::logging;

TEST(RingBufferTest, EmptyBufferReturnsNullopt) {
    RingBuffer<int> rb(16);
    EXPECT_EQ(rb.pop(), std::nullopt);
    EXPECT_EQ(rb.available(), 0u);
    EXPECT_EQ(rb.totalPushed(), 0u);
}

TEST(RingBufferTest, PushPopSingle) {
    RingBuffer<int> rb(16);
    rb.push(42);
    EXPECT_EQ(rb.available(), 1u);

    auto val = rb.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);
    EXPECT_EQ(rb.available(), 0u);
}

TEST(RingBufferTest, FifoOrder) {
    RingBuffer<int> rb(16);
    for (int i = 0; i < 5; ++i) {
        rb.push(i * 10);
    }
    EXPECT_EQ(rb.available(), 5u);

    for (int i = 0; i < 5; ++i) {
        auto val = rb.pop();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, i * 10);
    }
    EXPECT_EQ(rb.pop(), std::nullopt);
}

TEST(RingBufferTest, OverwriteOldest) {
    RingBuffer<int> rb(4);
    // Push 6 items into capacity-4 buffer
    for (int i = 0; i < 6; ++i) {
        rb.push(i);
    }
    EXPECT_EQ(rb.totalPushed(), 6u);
    EXPECT_EQ(rb.available(), 4u);

    // Should get items 2,3,4,5 (oldest 0,1 overwritten)
    for (int i = 2; i < 6; ++i) {
        auto val = rb.pop();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, i);
    }
}

TEST(RingBufferTest, Capacity) {
    RingBuffer<int> rb(128);
    EXPECT_EQ(rb.capacity(), 128u);
}

TEST(RingBufferTest, SnapshotReturnsAllStored) {
    RingBuffer<int> rb(8);
    for (int i = 0; i < 5; ++i) {
        rb.push(i);
    }

    auto snap = rb.snapshot();
    ASSERT_EQ(snap.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(snap[i], i);
    }
}

TEST(RingBufferTest, SnapshotAfterOverwrite) {
    RingBuffer<int> rb(4);
    for (int i = 0; i < 10; ++i) {
        rb.push(i);
    }

    auto snap = rb.snapshot();
    ASSERT_EQ(snap.size(), 4u);
    // Should contain 6,7,8,9
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(snap[i], 6 + i);
    }
}

TEST(RingBufferTest, SnapshotDoesNotConsume) {
    RingBuffer<int> rb(8);
    rb.push(1);
    rb.push(2);

    (void)rb.snapshot();  // should not affect pop
    EXPECT_EQ(rb.available(), 2u);
    auto val = rb.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 1);
}

TEST(RingBufferTest, ConcurrentProducerConsumer) {
    constexpr size_t COUNT = 100000;
    RingBuffer<uint64_t> rb(4096);

    std::atomic<size_t> consumeCount{0};

    std::thread producer([&] {
        for (uint64_t i = 0; i < COUNT; ++i) {
            rb.push(i);
        }
    });

    std::thread consumer([&] {
        while (rb.totalPushed() < COUNT || rb.available() > 0) {
            auto val = rb.pop();
            if (val.has_value()) {
                consumeCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    // Verify: no crashes, consumer got data, producer pushed all items
    EXPECT_GT(consumeCount.load(), 0u);
    EXPECT_EQ(rb.totalPushed(), COUNT);
}

TEST(RingBufferTest, StructType) {
    struct TelemetryFrame {
        double pressure = 0.0;
        double flowRate = 0.0;
        uint64_t timestamp = 0;
    };

    RingBuffer<TelemetryFrame> rb(16);
    rb.push({10.5, 4.0, 1000});
    rb.push({11.0, 4.1, 2000});

    auto val = rb.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(val->pressure, 10.5);
    EXPECT_EQ(val->timestamp, 1000u);
}
