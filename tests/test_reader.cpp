/// @file tests/test_reader.cpp
/// @brief Unit tests for ReaderSession (read side).

#include <recplay/reader.hpp>
#include <recplay/writer.hpp>
#include <gtest/gtest.h>
#include <filesystem>
#include <vector>

using namespace recplay;
namespace fs = std::filesystem;

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

TEST(ReaderSession, TotalMessageCount_ReportsSessionTotalsBeforeRead) {
    const fs::path dir = fs::temp_directory_path() / "recplay_test_reader_totals";
    fs::remove_all(dir);
    fs::create_directories(dir);

    SessionConfig cfg;
    cfg.OutputDir = dir.string();

    RecorderSession writer;
    ASSERT_EQ(writer.Open(cfg), Status::Ok);

    ChannelId ch = INVALID_CHANNEL_ID;
    ASSERT_EQ(writer.DefineChannel(ChannelConfig{"cam/front", CaptureLayer::L7, CompressionCodec::None}, ch),
              Status::Ok);
    ASSERT_NE(ch, INVALID_CHANNEL_ID);

    const uint8_t p0[2] = {1, 2};
    const uint8_t p1[2] = {3, 4};
    ASSERT_EQ(writer.Write(ch, 10ull, p0, sizeof(p0)), Status::Ok);
    ASSERT_EQ(writer.Write(ch, 20ull, p1, sizeof(p1)), Status::Ok);
    const std::string sessionPath = writer.SessionPath();
    ASSERT_EQ(writer.Close(), Status::Ok);

    ReaderSession reader;
    ASSERT_EQ(reader.Open(sessionPath), Status::Ok);
    EXPECT_EQ(reader.TotalMessageCount(), 2u);
    EXPECT_EQ(reader.MessageCount(ch), 2u);

    MessageView msg;
    ASSERT_TRUE(reader.ReadNext(msg));
    EXPECT_EQ(reader.TotalMessageCount(), 2u);
    EXPECT_EQ(reader.MessageCount(ch), 2u);
    EXPECT_EQ(reader.Close(), Status::Ok);

    fs::remove_all(dir);
}

TEST(ReaderSession, OpenWithMissingAllSegmentFiles_ReturnsNotFound) {
    const fs::path dir = fs::temp_directory_path() / "recplay_test_reader_missing_segments";
    fs::remove_all(dir);
    fs::create_directories(dir);

    SessionConfig cfg;
    cfg.OutputDir = dir.string();

    RecorderSession writer;
    ASSERT_EQ(writer.Open(cfg), Status::Ok);

    ChannelId ch = INVALID_CHANNEL_ID;
    ASSERT_EQ(writer.DefineChannel(ChannelConfig{"imu", CaptureLayer::L7, CompressionCodec::None}, ch),
              Status::Ok);
    const uint8_t p[1] = {0x42};
    ASSERT_EQ(writer.Write(ch, 1ull, p, sizeof(p)), Status::Ok);
    const std::string sessionPath = writer.SessionPath();
    ASSERT_EQ(writer.Close(), Status::Ok);

    for (const auto& entry : fs::directory_iterator(sessionPath)) {
        if (entry.path().extension() == ".rec")
            fs::remove(entry.path());
    }

    ReaderSession reader;
    EXPECT_EQ(reader.Open(sessionPath), Status::ErrorNotFound);

    fs::remove_all(dir);
}
