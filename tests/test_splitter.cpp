/// @file tests/test_splitter.cpp
/// @brief Unit tests for Splitter / Merger utilities.

#include <recplay/splitter.hpp>
#include <gtest/gtest.h>

using namespace recplay;

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

// TODO: full split/merge round-trip tests against a temp directory once implemented
