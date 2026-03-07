/// @file tests/test_writer.cpp
/// @brief Unit tests for RecorderSession (write side).

#include <recplay/writer.hpp>
#include <gtest/gtest.h>
#include <filesystem>

using namespace recplay;
namespace fs = std::filesystem;

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

TEST(RecorderSession, WriteNullDataWithNonZeroLength_ReturnsInvalidArg) {
    const fs::path dir = fs::temp_directory_path() / "recplay_test_writer_null_data";
    fs::remove_all(dir);
    fs::create_directories(dir);

    SessionConfig cfg;
    cfg.OutputDir = dir.string();

    RecorderSession rec;
    ASSERT_EQ(rec.Open(cfg), Status::Ok);

    ChannelId id = INVALID_CHANNEL_ID;
    ASSERT_EQ(rec.DefineChannel(ChannelConfig{"test/ch", CaptureLayer::L7, CompressionCodec::None}, id),
              Status::Ok);
    ASSERT_NE(id, INVALID_CHANNEL_ID);

    EXPECT_EQ(rec.Write(id, 123ull, nullptr, 8u), Status::ErrorInvalidArg);
    EXPECT_EQ(rec.Close(), Status::Ok);

    fs::remove_all(dir);
}

TEST(RecorderSession, AnnotateNullMetadataWithNonZeroLength_ReturnsInvalidArg) {
    const fs::path dir = fs::temp_directory_path() / "recplay_test_writer_bad_annotate";
    fs::remove_all(dir);
    fs::create_directories(dir);

    SessionConfig cfg;
    cfg.OutputDir = dir.string();

    RecorderSession rec;
    ASSERT_EQ(rec.Open(cfg), Status::Ok);
    EXPECT_EQ(rec.Annotate(123ull, "evt", nullptr, 4u), Status::ErrorInvalidArg);
    EXPECT_EQ(rec.Close(), Status::Ok);

    fs::remove_all(dir);
}
