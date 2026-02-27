/// @file tests/test_session.cpp
/// @brief Unit tests for SessionManifest helpers and SessionConfig defaults.

#include <recplay/session.hpp>
#include <gtest/gtest.h>

using namespace recplay;

TEST(SessionConfig, Defaults) {
    SessionConfig cfg;
    EXPECT_GT(cfg.MaxSegmentBytes, 0u);
    EXPECT_EQ(cfg.MaxSegmentDurationNs, 0u);
    EXPECT_TRUE(cfg.CrcEnabled);
    EXPECT_GT(cfg.IndexIntervalNs, 0u);
}

TEST(SessionManifest, EmptyManifest) {
    SessionManifest m;
    EXPECT_EQ(m.TotalSizeBytes(), 0u);
    EXPECT_EQ(m.StartNs(),        0u);
    EXPECT_EQ(m.EndNs(),          0u);
    EXPECT_EQ(m.DurationNs(),     0u);
    EXPECT_EQ(m.FindChannel("any"), nullptr);
    EXPECT_EQ(m.FindChannel(static_cast<ChannelId>(0)), nullptr);
}
