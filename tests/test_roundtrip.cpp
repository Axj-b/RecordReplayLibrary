/// @file tests/test_roundtrip.cpp
/// @brief End-to-end round-trip test: write a session, read it back, compare byte-for-byte.

#include <recplay/recplay.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace recplay;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string TempDir(const char* suffix) {
    auto path = fs::temp_directory_path() / ("recplay_test_" + std::string(suffix));
    fs::remove_all(path);
    fs::create_directories(path);
    return path.string();
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class RoundtripTest : public ::testing::Test {
protected:
    void SetUp() override { dir_ = TempDir("roundtrip"); }
    void TearDown() override { fs::remove_all(dir_); }
    std::string dir_;
};

TEST_F(RoundtripTest, SingleChannelNoCompression) {
    // --- Write ---
    SessionConfig wcfg;
    wcfg.OutputDir       = dir_;
    wcfg.MaxSegmentBytes = 1u * 1024 * 1024; // 1 MiB

    RecorderSession writer;
    ASSERT_EQ(writer.Open(wcfg), Status::Ok);

    ChannelId ch = writer.DefineChannel({
        "test/channel", CaptureLayer::L7, CompressionCodec::None
    });
    ASSERT_NE(ch, INVALID_CHANNEL_ID);

    // Write 1000 messages with known content
    std::vector<std::pair<Timestamp, std::vector<uint8_t>>> written;
    for (uint32_t i = 0; i < 1000; ++i) {
        Timestamp ts = 1'000'000'000ull * i;
        std::vector<uint8_t> payload(16, static_cast<uint8_t>(i & 0xFF));
        ASSERT_EQ(writer.Write(ch, ts, payload.data(), static_cast<uint32_t>(payload.size())),
                  Status::Ok);
        written.push_back({ts, payload});
    }

    std::string sessionPath = writer.SessionPath();   // capture BEFORE close
    ASSERT_EQ(writer.Close(), Status::Ok);

    // --- Read back ---
    ReaderSession reader;
    ASSERT_EQ(reader.Open(sessionPath), Status::Ok);

    ASSERT_FALSE(reader.Channels().empty());
    EXPECT_EQ(reader.Channels()[0].Name, "test/channel");

    size_t idx = 0;
    MessageView msg;
    while (reader.ReadNext(msg)) {
        ASSERT_LT(idx, written.size());
        EXPECT_EQ(msg.Channel,     ch);
        EXPECT_EQ(msg.TimestampNs, written[idx].first);
        ASSERT_EQ(msg.Length,      static_cast<uint32_t>(written[idx].second.size()));
        EXPECT_EQ(std::memcmp(msg.Data, written[idx].second.data(), msg.Length), 0)
            << "Payload mismatch at message " << idx;
        ++idx;
    }

    EXPECT_EQ(idx, written.size()) << "Message count mismatch";
    EXPECT_EQ(reader.CorruptRecordCount(), 0u);
    ASSERT_EQ(reader.Close(), Status::Ok);
}

TEST_F(RoundtripTest, MultiChannelWithRotation) {
    SessionConfig wcfg;
    wcfg.OutputDir       = dir_;
    // Force a segment rotation after ~20 KiB so we test multi-segment reads
    wcfg.MaxSegmentBytes = 20u * 1024u;

    RecorderSession writer;
    ASSERT_EQ(writer.Open(wcfg), Status::Ok);

    ChannelId chA = writer.DefineChannel({"chan/A", CaptureLayer::L7, CompressionCodec::None});
    ChannelId chB = writer.DefineChannel({"chan/B", CaptureLayer::L7, CompressionCodec::None});
    ASSERT_NE(chA, INVALID_CHANNEL_ID);
    ASSERT_NE(chB, INVALID_CHANNEL_ID);

    const uint32_t N = 500;
    std::vector<std::tuple<ChannelId, Timestamp, uint8_t>> written;
    for (uint32_t i = 0; i < N; ++i) {
        Timestamp ts = 1'000'000'000ull * i;
        uint8_t   val = static_cast<uint8_t>(i & 0xFF);
        std::vector<uint8_t> payA(32, val);
        std::vector<uint8_t> payB(32, static_cast<uint8_t>(~val));

        ASSERT_EQ(writer.Write(chA, ts,     payA.data(), 32), Status::Ok);
        ASSERT_EQ(writer.Write(chB, ts + 1, payB.data(), 32), Status::Ok);

        written.emplace_back(chA, ts,     val);
        written.emplace_back(chB, ts + 1, static_cast<uint8_t>(~val));
    }

    std::string sessionPath = writer.SessionPath();
    ASSERT_EQ(writer.Close(), Status::Ok);

    // Should have multiple segments
    ReaderSession reader;
    ASSERT_EQ(reader.Open(sessionPath), Status::Ok);
    EXPECT_GE(reader.Manifest().Segments.size(), 2u);

    size_t idx = 0;
    MessageView msg;
    while (reader.ReadNext(msg)) {
        ASSERT_LT(idx, written.size());
        auto [expCh, expTs, expVal] = written[idx];
        EXPECT_EQ(msg.Channel,     expCh);
        EXPECT_EQ(msg.TimestampNs, expTs);
        ASSERT_EQ(msg.Length,      32u);
        EXPECT_EQ(static_cast<const uint8_t*>(msg.Data)[0], expVal);
        ++idx;
    }
    EXPECT_EQ(idx, written.size());
    EXPECT_EQ(reader.CorruptRecordCount(), 0u);
    ASSERT_EQ(reader.Close(), Status::Ok);
}

TEST_F(RoundtripTest, MuxSplitMergeRoundtrip) {
    std::string srcDir  = dir_ + "/src";
    std::string spltDir = dir_ + "/split";
    std::string mrgDir  = dir_ + "/merged";
    fs::create_directories(spltDir);
    fs::create_directories(mrgDir);

    // --- Record a two-channel muxed session ---
    SessionConfig wcfg;
    wcfg.OutputDir = srcDir;

    RecorderSession writer;
    ASSERT_EQ(writer.Open(wcfg), Status::Ok);

    ChannelId chX = writer.DefineChannel({"x", CaptureLayer::L7, CompressionCodec::None});
    ChannelId chY = writer.DefineChannel({"y", CaptureLayer::L7, CompressionCodec::None});

    const uint32_t M = 200;
    for (uint32_t i = 0; i < M; ++i) {
        Timestamp ts = 1'000'000ull * i;
        uint8_t vx = static_cast<uint8_t>(i);
        uint8_t vy = static_cast<uint8_t>(i + 100u);
        ASSERT_EQ(writer.Write(chX, ts,     &vx, 1), Status::Ok);
        ASSERT_EQ(writer.Write(chY, ts + 1, &vy, 1), Status::Ok);
    }

    std::string srcSession = writer.SessionPath();
    ASSERT_EQ(writer.Close(), Status::Ok);

    // --- Split into per-channel sessions ---
    SplitResult sr;
    ASSERT_EQ(Splitter::Split(srcSession, spltDir, sr), Status::Ok);
    ASSERT_EQ(sr.Outputs.size(), 2u);

    for (const auto& e : sr.Outputs)
        EXPECT_EQ(e.MessageCount, static_cast<uint64_t>(M));

    // --- Merge back ---
    std::vector<std::string> splitPaths;
    for (const auto& e : sr.Outputs)
        splitPaths.push_back(e.SessionPath);

    MergeResult mr;
    ASSERT_EQ(Splitter::Merge(splitPaths, mrgDir, mr), Status::Ok);
    EXPECT_EQ(mr.TotalMessageCount, static_cast<uint64_t>(M * 2));
    EXPECT_EQ(mr.ChannelCount,      2u);

    // --- Validate the merged session ---
    uint64_t corrupt = 0;
    auto valSt = Splitter::Validate(mr.SessionPath, &corrupt);
    EXPECT_EQ(valSt, Status::Ok);
    EXPECT_EQ(corrupt, 0u);
}

