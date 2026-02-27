/// @file tests/test_reader.cpp
/// @brief Unit tests for ReaderSession (read side).

#include <recplay/reader.hpp>
#include <gtest/gtest.h>

using namespace recplay;

TEST(ReaderSession, DefaultConstructed_NotOpen) {
    ReaderSession reader;
    EXPECT_FALSE(reader.IsOpen());
}

TEST(ReaderSession, CloseWhenNotOpen_ReturnsOk) {
    ReaderSession reader;
    EXPECT_EQ(reader.Close(), Status::Ok);
}

TEST(ReaderSession, ReadNextWhenNotOpen_ReturnsFalse) {
    ReaderSession reader;
    MessageView msg;
    EXPECT_FALSE(reader.ReadNext(msg));
}

TEST(MessageView, DefaultInvalid) {
    MessageView msg;
    EXPECT_FALSE(msg.IsValid());
    EXPECT_EQ(msg.Channel, INVALID_CHANNEL_ID);
    EXPECT_EQ(msg.Data,    nullptr);
    EXPECT_EQ(msg.Length,  0u);
}

// TODO: add tests that open a real session against a temp directory once implemented
