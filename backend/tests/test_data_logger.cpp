#include "logging/DataLogger.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

using namespace injector::logging;
using namespace injector::control;

TEST(DataLoggerTest, DefaultConstruction) {
    DataLogger logger;
    EXPECT_EQ(logger.eventCount(), 0u);
}

TEST(DataLoggerTest, LogEvents) {
    DataLogger logger;
    logger.logEvent("state", "Transition: IDLE -> ARMED");
    logger.logEvent("command", "ARM received");

    EXPECT_EQ(logger.eventCount(), 2u);

    auto events = logger.events();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].category, "state");
    EXPECT_EQ(events[0].message, "Transition: IDLE -> ARMED");
    EXPECT_GT(events[0].timestampUs, 0u);
    EXPECT_EQ(events[1].category, "command");
}

TEST(DataLoggerTest, EventsAreCopied) {
    DataLogger logger;
    logger.logEvent("test", "event1");

    auto events1 = logger.events();
    logger.logEvent("test", "event2");
    auto events2 = logger.events();

    EXPECT_EQ(events1.size(), 1u);
    EXPECT_EQ(events2.size(), 2u);
}

TEST(DataLoggerTest, MaxEventsDropsOldest) {
    DataLogger logger(1024, 3);  // max 3 events

    logger.logEvent("a", "first");
    logger.logEvent("b", "second");
    logger.logEvent("c", "third");
    logger.logEvent("d", "fourth");

    auto events = logger.events();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].message, "second");
    EXPECT_EQ(events[2].message, "fourth");
}

TEST(DataLoggerTest, TickBufferAccessible) {
    DataLogger logger(128);
    auto& buffer = logger.tickBuffer();

    TickData td;
    td.timestampUs = 1000;
    td.targetFlowRate = 4.0;
    td.actualFlowRate = 3.9;
    td.pressure = 200.0;
    buffer.push(td);

    EXPECT_EQ(buffer.totalPushed(), 1u);
}

TEST(DataLoggerTest, ExportCsvFormat) {
    DataLogger logger(128);

    TickData td;
    td.timestampUs = 1000000;
    td.targetFlowRate = 4.0;
    td.actualFlowRate = 3.97;
    td.pressure = 208.5;
    td.motorRpmCommanded = 400;
    td.motorRpmActual = 397;
    td.contrastValve = 1;
    td.salineValve = 0;
    td.state = 2;
    td.phaseIndex = 0;
    td.contrastRemaining = 58.2;
    td.salineRemaining = 50.0;
    logger.tickBuffer().push(td);

    std::string csv = logger.exportCsv();

    // Verify header
    EXPECT_NE(csv.find("timestamp_us,target_flow_rate,actual_flow_rate"),
              std::string::npos);

    // Verify data row contains expected values
    EXPECT_NE(csv.find("1000000"), std::string::npos);
    EXPECT_NE(csv.find("208.5"), std::string::npos);
}

TEST(DataLoggerTest, ExportJsonValid) {
    DataLogger logger(128);

    TickData td;
    td.timestampUs = 2000000;
    td.targetFlowRate = 4.0;
    td.actualFlowRate = 4.01;
    td.pressure = 210.0;
    td.motorRpmCommanded = 401;
    td.motorRpmActual = 400;
    td.contrastValve = 1;
    td.salineValve = 0;
    td.state = 2;
    td.phaseIndex = 0;
    td.contrastRemaining = 50.0;
    td.salineRemaining = 50.0;
    logger.tickBuffer().push(td);

    std::string json = logger.exportJson();

    // Parse as JSON to verify validity
    auto parsed = nlohmann::json::parse(json);
    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0]["timestamp_us"], 2000000u);
    EXPECT_DOUBLE_EQ(parsed[0]["target_flow_rate"].get<double>(), 4.0);
    EXPECT_EQ(parsed[0]["contrast_valve"], 1);
}

TEST(DataLoggerTest, ExportEmptyDataReturnsValidFormat) {
    DataLogger logger(128);

    std::string csv = logger.exportCsv();
    EXPECT_NE(csv.find("timestamp_us"), std::string::npos);
    // Only header, no data rows
    auto lines = 0;
    for (char c : csv) {
        if (c == '\n') ++lines;
    }
    EXPECT_EQ(lines, 1);  // just the header

    std::string json = logger.exportJson();
    auto parsed = nlohmann::json::parse(json);
    EXPECT_TRUE(parsed.is_array());
    EXPECT_EQ(parsed.size(), 0u);
}
