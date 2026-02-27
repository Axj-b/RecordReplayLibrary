/// @file tests/test_file_map.cpp
/// @brief Unit tests for the MappedFile read/write abstraction.

// local (private) includes
#include "detail/file_map.hpp"

// system includes
#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace recplay::detail;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Fixture — creates a temp directory, cleans up on teardown
// ---------------------------------------------------------------------------

class FileMapTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_Dir = fs::temp_directory_path() / "recplay_test_file_map";
        fs::create_directories(m_Dir);
    }
    void TearDown() override {
        fs::remove_all(m_Dir);
    }

    std::string TempPath(const std::string& name) const {
        return (m_Dir / name).string();
    }

    fs::path m_Dir;
};

// ---------------------------------------------------------------------------
// OpenWrite + Close: file must exist and have the correct size
// ---------------------------------------------------------------------------

TEST_F(FileMapTest, OpenWrite_CreatesFile) {
    auto path = TempPath("create.rec");
    {
        auto f = MappedFile::OpenWrite(path);
        ASSERT_TRUE(f.IsOpen());
        EXPECT_GT(f.Available(), 0u);
        // Write 4 bytes
        ASSERT_TRUE(f.EnsureAvailable(4));
        std::memcpy(f.WritePtr(), "TEST", 4);
        f.Advance(4);
        EXPECT_EQ(f.BytesWritten(), 4u);
    }  // destructor calls Close() which truncates

    EXPECT_TRUE(fs::exists(path));
    EXPECT_EQ(fs::file_size(path), 4u);
}

// ---------------------------------------------------------------------------
// WriteThenRead: content written in write mode is readable in read mode
// ---------------------------------------------------------------------------

TEST_F(FileMapTest, WriteThenRead_ContentMatches) {
    const std::string payload = "Hello, RecPlay!";
    auto path = TempPath("roundtrip.rec");

    // --- write ---
    {
        auto f = MappedFile::OpenWrite(path);
        ASSERT_TRUE(f.IsOpen());
        ASSERT_TRUE(f.EnsureAvailable(payload.size()));
        std::memcpy(f.WritePtr(), payload.data(), payload.size());
        f.Advance(static_cast<uint64_t>(payload.size()));
    }

    // --- read back ---
    auto r = MappedFile::OpenRead(path);
    ASSERT_TRUE(r.IsOpen());
    EXPECT_EQ(r.FileSize(), payload.size());
    ASSERT_NE(r.Data(), nullptr);
    EXPECT_EQ(std::memcmp(r.Data(), payload.data(), payload.size()), 0);
}

// ---------------------------------------------------------------------------
// MultipleWrites: several Advance() calls accumulate correctly
// ---------------------------------------------------------------------------

TEST_F(FileMapTest, MultipleWrites_BytesAccumulate) {
    auto path = TempPath("multi.rec");

    constexpr uint32_t N = 1000;
    {
        auto f = MappedFile::OpenWrite(path);
        ASSERT_TRUE(f.IsOpen());
        for (uint32_t i = 0; i < N; ++i) {
            ASSERT_TRUE(f.EnsureAvailable(sizeof(uint32_t)));
            std::memcpy(f.WritePtr(), &i, sizeof(i));
            f.Advance(sizeof(uint32_t));
        }
        EXPECT_EQ(f.BytesWritten(), static_cast<uint64_t>(N) * sizeof(uint32_t));
    }

    EXPECT_EQ(fs::file_size(path), static_cast<uint64_t>(N) * sizeof(uint32_t));

    auto r = MappedFile::OpenRead(path);
    ASSERT_TRUE(r.IsOpen());
    const auto* data = reinterpret_cast<const uint32_t*>(r.Data());
    for (uint32_t i = 0; i < N; ++i)
        EXPECT_EQ(data[i], i) << "mismatch at index " << i;
}

// ---------------------------------------------------------------------------
// Grow: writing more than the initial reservation triggers an internal grow
// ---------------------------------------------------------------------------

TEST_F(FileMapTest, Write_TriggersGrow) {
    auto path = TempPath("grow.rec");

    // Reserve only the smallest possible granularity, then write 2x that amount
    // by writing in small chunks so EnsureAvailable forces at least one grow.
    const uint64_t chunkSize   = 4u * 1024u;   // 4 KiB per write
    const uint64_t totalWrites = (FILE_MAP_GROW_BYTES / chunkSize) * 2u;
    std::vector<uint8_t> chunk(chunkSize, 0xAB);

    {
        auto f = MappedFile::OpenWrite(path, FILE_MAP_GROW_BYTES);
        ASSERT_TRUE(f.IsOpen());
        for (uint64_t i = 0; i < totalWrites; ++i) {
            ASSERT_TRUE(f.EnsureAvailable(chunkSize))
                << "EnsureAvailable failed at write " << i;
            std::memcpy(f.WritePtr(), chunk.data(), chunkSize);
            f.Advance(chunkSize);
        }
        EXPECT_EQ(f.BytesWritten(), totalWrites * chunkSize);
    }

    EXPECT_EQ(fs::file_size(path), totalWrites * chunkSize);
}

// ---------------------------------------------------------------------------
// OpenRead_NonExistent: returns an invalid file handle
// ---------------------------------------------------------------------------

TEST_F(FileMapTest, OpenRead_NonExistent_ReturnsInvalid) {
    auto f = MappedFile::OpenRead(TempPath("does_not_exist.rec"));
    EXPECT_FALSE(f.IsOpen());
    EXPECT_EQ(f.Data(), nullptr);
    EXPECT_EQ(f.FileSize(), 0u);
}

// ---------------------------------------------------------------------------
// Move semantics: moving transfers ownership, source becomes invalid
// ---------------------------------------------------------------------------

TEST_F(FileMapTest, MoveSemantics_SourceBecomesInvalid) {
    auto path = TempPath("move.rec");
    auto f1   = MappedFile::OpenWrite(path);
    ASSERT_TRUE(f1.IsOpen());

    auto f2 = std::move(f1);
    EXPECT_FALSE(f1.IsOpen());   // NOLINT — intentional post-move check
    EXPECT_TRUE(f2.IsOpen());

    ASSERT_TRUE(f2.EnsureAvailable(4));
    std::memcpy(f2.WritePtr(), "MOVE", 4);
    f2.Advance(4);
}

// ---------------------------------------------------------------------------
// ExplicitClose: calling Close() makes IsOpen() return false
// ---------------------------------------------------------------------------

TEST_F(FileMapTest, ExplicitClose_IsOpen_ReturnsFalse) {
    auto path = TempPath("close.rec");
    auto f    = MappedFile::OpenWrite(path);
    ASSERT_TRUE(f.IsOpen());
    f.Close();
    EXPECT_FALSE(f.IsOpen());
    // Second Close() must be a no-op (no crash/double-free)
    EXPECT_NO_THROW(f.Close());
}
