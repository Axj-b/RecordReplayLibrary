/// @file tests/test_writer.cpp
/// @brief Unit tests for RecorderSession (write side).

#include <recplay/writer.hpp>
#include <gtest/gtest.h>

using namespace recplay;

TEST(RecorderSession, DefaultConstructed_NotOpen) {
    RecorderSession rec;
    EXPECT_FALSE(rec.IsOpen());
}

TEST(RecorderSession, CloseWhenNotOpen_ReturnsOk) {
    RecorderSession rec;
    EXPECT_EQ(rec.Close(), Status::Ok);
}

TEST(RecorderSession, WriteWhenNotOpen_ReturnsError) {
    RecorderSession rec;
    uint8_t buf[4]{};
    auto s = rec.Write(0, 0, buf, sizeof(buf));
    EXPECT_NE(s, Status::Ok);
}

TEST(RecorderSession, DefineChannelWhenNotOpen_ReturnsError) {
    RecorderSession rec;
    ChannelId id = INVALID_CHANNEL_ID;
    auto s = rec.DefineChannel(ChannelConfig{"test"}, id);
    EXPECT_NE(s, Status::Ok);
    EXPECT_EQ(id, INVALID_CHANNEL_ID);
}

// TODO: add tests that open a real session against a temp directory once implemented
