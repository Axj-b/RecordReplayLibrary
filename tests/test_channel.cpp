/// @file tests/test_channel.cpp
/// @brief Unit tests for ChannelDef / ChannelConfig helpers.

#include <recplay/channel.hpp>
#include <gtest/gtest.h>

using namespace recplay;

TEST(ChannelDef, IsRawPacket) {
    ChannelDef ch{};
    ch.Layer = CaptureLayer::L3L4;
    EXPECT_TRUE(ch.IsRawPacket());

    ch.Layer = CaptureLayer::L7;
    EXPECT_FALSE(ch.IsRawPacket());
}

TEST(ChannelDef, IsCompressed) {
    ChannelDef ch{};
    ch.Compression = CompressionCodec::LZ4;
    EXPECT_TRUE(ch.IsCompressed());

    ch.Compression = CompressionCodec::None;
    EXPECT_FALSE(ch.IsCompressed());
}

TEST(ChannelConfig, Defaults) {
    ChannelConfig cfg;
    EXPECT_EQ(cfg.Layer,       CaptureLayer::L7);
    EXPECT_EQ(cfg.Compression, CompressionCodec::LZ4);
    EXPECT_GT(cfg.ChunkSizeBytes, 0u);
    EXPECT_GT(cfg.ChunkFlushIntervalMs, 0u);
    EXPECT_TRUE(cfg.Schema.empty());
    EXPECT_TRUE(cfg.UserMetadata.empty());
}
