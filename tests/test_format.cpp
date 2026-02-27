/// @file tests/test_format.cpp
/// @brief Unit tests for binary format struct sizes, magic bytes, and layout constants.

#include <recplay/format.hpp>
#include <recplay/types.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>

using namespace recplay;
using namespace recplay::format;

// ---------------------------------------------------------------------------
// Struct size guarantees (must match the documented on-disk layout)
// ---------------------------------------------------------------------------

TEST(FormatSizes, FileHeaderIs64Bytes) {
    EXPECT_EQ(sizeof(FileHeader), 64u);
}

TEST(FormatSizes, RecordEnvelopeIs20Bytes) {
    EXPECT_EQ(sizeof(RecordEnvelope), 20u);
}

TEST(FormatSizes, ChunkHeaderIs9Bytes) {
    EXPECT_EQ(sizeof(ChunkHeader), 9u);
}

TEST(FormatSizes, IndexEntryIs18Bytes) {
    EXPECT_EQ(sizeof(IndexEntry), 18u);
}

TEST(FormatSizes, IndexHeaderIs4Bytes) {
    EXPECT_EQ(sizeof(IndexHeader), 4u);
}

TEST(FormatSizes, FileFooterIs32Bytes) {
    EXPECT_EQ(sizeof(FileFooter), 32u);
}

// ---------------------------------------------------------------------------
// Magic bytes
// ---------------------------------------------------------------------------

TEST(FormatMagic, MagicBytesContent) {
    EXPECT_EQ(MAGIC[0], 0x89u);
    EXPECT_EQ(MAGIC[1], static_cast<uint8_t>('R'));
    EXPECT_EQ(MAGIC[2], static_cast<uint8_t>('E'));
    EXPECT_EQ(MAGIC[3], static_cast<uint8_t>('C'));
    EXPECT_EQ(MAGIC[4], static_cast<uint8_t>('\r'));
    EXPECT_EQ(MAGIC[5], static_cast<uint8_t>('\n'));
    EXPECT_EQ(MAGIC[6], 0x1Au);
    EXPECT_EQ(MAGIC[7], static_cast<uint8_t>('\n'));
}

TEST(FormatMagic, FileHeaderMagicOffset) {
    // Magic must be at offset 0 in FileHeader
    FileHeader hdr{};
    EXPECT_EQ(offsetof(FileHeader, Magic), 0u);
    std::memcpy(hdr.Magic, MAGIC, 8);
    EXPECT_EQ(std::memcmp(hdr.Magic, MAGIC, 8), 0);
}

TEST(FormatMagic, FileFooterMagicOffset) {
    EXPECT_EQ(offsetof(FileFooter, Magic), 0u);
}

// ---------------------------------------------------------------------------
// Field offsets (selected — catches accidental reordering)
// ---------------------------------------------------------------------------

TEST(FormatOffsets, FileHeader_VersionMajorAt8) {
    EXPECT_EQ(offsetof(FileHeader, VersionMajor), 8u);
}

TEST(FormatOffsets, FileHeader_SessionIdAt10) {
    EXPECT_EQ(offsetof(FileHeader, SessionId), 10u);
}

TEST(FormatOffsets, FileHeader_SegmentIndexAt26) {
    EXPECT_EQ(offsetof(FileHeader, SegmentIndex), 26u);
}

TEST(FormatOffsets, FileHeader_CreatedAtAt30) {
    EXPECT_EQ(offsetof(FileHeader, CreatedAtNs), 30u);
}

TEST(FormatOffsets, FileHeader_FlagsAt38) {
    EXPECT_EQ(offsetof(FileHeader, Flags), 38u);
}

TEST(FormatOffsets, RecordEnvelope_OpAt0) {
    EXPECT_EQ(offsetof(RecordEnvelope, Op), 0u);
}

TEST(FormatOffsets, RecordEnvelope_TimestampAt8) {
    EXPECT_EQ(offsetof(RecordEnvelope, TimestampNs), 8u);
}

TEST(FormatOffsets, RecordEnvelope_CrcAt16) {
    EXPECT_EQ(offsetof(RecordEnvelope, Crc32), 16u);
}

TEST(FormatOffsets, FileFooter_IndexOffsetAt8) {
    EXPECT_EQ(offsetof(FileFooter, IndexOffset), 8u);
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

TEST(FormatConstants, DefaultChunkBytesIs256KiB) {
    EXPECT_EQ(DEFAULT_CHUNK_BYTES, 256u * 1024u);
}

TEST(FormatConstants, DefaultMaxSegmentIs1GiB) {
    EXPECT_EQ(DEFAULT_MAX_SEGMENT_BYTES, 1ull * 1024 * 1024 * 1024);
}

TEST(FormatConstants, NoIndexSentinel) {
    EXPECT_EQ(NO_INDEX, UINT64_MAX);
}

// ---------------------------------------------------------------------------
// RecordOp values
// ---------------------------------------------------------------------------

TEST(RecordOp, Values) {
    EXPECT_EQ(static_cast<uint8_t>(RecordOp::SessionStart), 0x01u);
    EXPECT_EQ(static_cast<uint8_t>(RecordOp::ChannelDef),   0x02u);
    EXPECT_EQ(static_cast<uint8_t>(RecordOp::Data),         0x03u);
    EXPECT_EQ(static_cast<uint8_t>(RecordOp::Chunk),        0x04u);
    EXPECT_EQ(static_cast<uint8_t>(RecordOp::Index),        0x05u);
    EXPECT_EQ(static_cast<uint8_t>(RecordOp::Annotation),   0x06u);
    EXPECT_EQ(static_cast<uint8_t>(RecordOp::SessionEnd),   0x07u);
}
