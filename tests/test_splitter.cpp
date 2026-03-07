/// @file tests/test_splitter.cpp
/// @brief Unit tests for Splitter / Merger utilities.

#include <recplay/splitter.hpp>
#include <recplay/writer.hpp>
#include <recplay/reader.hpp>
#include <gtest/gtest.h>
#include <filesystem>
#include <vector>

using namespace recplay;
namespace fs = std::filesystem;

TEST(SplitOptions, Defaults) {
    SplitOptions opts;
    EXPECT_TRUE(opts.ChannelIds.empty());
    EXPECT_TRUE(opts.OneDirPerChannel);
    EXPECT_TRUE(opts.IncludeAnnotations);
    EXPECT_EQ(opts.MaxSegmentBytes, 0u);
}

TEST(MergeOptions, Defaults) {
    MergeOptions opts;
    EXPECT_EQ(opts.MaxSegmentBytes, format::DEFAULT_MAX_SEGMENT_BYTES);
    EXPECT_FALSE(opts.MergeDuplicateChannelNames);
    EXPECT_TRUE(opts.IncludeAnnotations);
}

TEST(Splitter, ValidateNonExistentSession_ReturnsError) {
    auto s = Splitter::Validate("/does/not/exist");
    EXPECT_NE(s, Status::Ok);
}

TEST(Splitter, MergeDuplicateChannelNames_PreservesAllMessages) {
    const fs::path root = fs::temp_directory_path() / "recplay_test_merge_dupe_channels";
    fs::remove_all(root);
    fs::create_directories(root);

    auto make_source = [&](const fs::path& outDir,
                           const std::vector<std::pair<Timestamp, uint8_t>>& samples,
                           std::string& outSessionPath) {
        SessionConfig cfg;
        cfg.OutputDir = outDir.string();

        RecorderSession rec;
        ASSERT_EQ(rec.Open(cfg), Status::Ok);
        ChannelId ch = INVALID_CHANNEL_ID;
        ASSERT_EQ(rec.DefineChannel(ChannelConfig{"shared", CaptureLayer::L7, CompressionCodec::None}, ch),
                  Status::Ok);
        for (const auto& s : samples) {
            ASSERT_EQ(rec.Write(ch, s.first, &s.second, 1u), Status::Ok);
        }
        outSessionPath = rec.SessionPath();
        ASSERT_EQ(rec.Close(), Status::Ok);
    };

    std::string src1;
    std::string src2;
    make_source(root / "src1", {{10ull, 1u}, {30ull, 3u}, {50ull, 5u}}, src1);
    make_source(root / "src2", {{20ull, 2u}, {40ull, 4u}, {60ull, 6u}}, src2);

    MergeOptions opts;
    opts.MergeDuplicateChannelNames = true;

    MergeResult mr;
    ASSERT_EQ(Splitter::Merge({src1, src2}, (root / "merged").string(), opts, mr), Status::Ok);
    EXPECT_EQ(mr.ChannelCount, 1u);
    EXPECT_EQ(mr.TotalMessageCount, 6u);

    ReaderSession r;
    ASSERT_EQ(r.Open(mr.SessionPath), Status::Ok);
    ASSERT_EQ(r.Channels().size(), 1u);
    EXPECT_EQ(r.Channels()[0].Name, "shared");

    std::vector<uint8_t> values;
    MessageView mv;
    while (r.ReadNext(mv)) {
        ASSERT_EQ(mv.Length, 1u);
        values.push_back(static_cast<const uint8_t*>(mv.Data)[0]);
    }
    EXPECT_EQ(values, (std::vector<uint8_t>{1u, 2u, 3u, 4u, 5u, 6u}));
    EXPECT_EQ(r.Close(), Status::Ok);

    fs::remove_all(root);
}

TEST(Splitter, SplitPatternAndOneDirPerChannel_AreApplied) {
    const fs::path root = fs::temp_directory_path() / "recplay_test_split_pattern";
    fs::remove_all(root);
    fs::create_directories(root);

    SessionConfig cfg;
    cfg.OutputDir = (root / "source").string();

    RecorderSession rec;
    ASSERT_EQ(rec.Open(cfg), Status::Ok);
    ChannelId ch = INVALID_CHANNEL_ID;
    ASSERT_EQ(rec.DefineChannel(ChannelConfig{"cam/front", CaptureLayer::L7, CompressionCodec::None}, ch),
              Status::Ok);
    const uint8_t payload = 0x7f;
    ASSERT_EQ(rec.Write(ch, 1ull, &payload, 1u), Status::Ok);
    const std::string sourceSession = rec.SessionPath();
    ASSERT_EQ(rec.Close(), Status::Ok);

    SplitOptions opts;
    opts.OneDirPerChannel = true;
    opts.OutputDirPattern = "{original_session_name}__{channel_name}";

    SplitResult result;
    ASSERT_EQ(Splitter::Split(sourceSession, (root / "split").string(), opts, result), Status::Ok);
    ASSERT_EQ(result.Outputs.size(), 1u);

    const std::string sourceBase = fs::path(sourceSession).filename().string();
    const fs::path outPath = result.Outputs[0].SessionPath;
    EXPECT_EQ(outPath.parent_path(), root / "split" / "cam_front");
    EXPECT_EQ(outPath.filename().string(), sourceBase + "__cam_front");

    fs::remove_all(root);
}
