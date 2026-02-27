#pragma once

/// @file detail/file_map.hpp
/// @brief Memory-mapped file abstraction — internal use only.
///
/// Provides a unified read/write interface over OS-level file mappings:
///   - Windows : CreateFileMapping / MapViewOfFile
///   - POSIX   : mmap / ftruncate
///
/// Write mode pre-allocates space in configurable increments and grows the
/// mapping automatically on demand. On Close() the file is truncated to the
/// exact number of bytes written.
///
/// Read mode maps the entire file into memory at Open() time. The data
/// pointer remains valid until Close() or destruction.
///
/// Move-only. Always call Close() explicitly or let the destructor do it.

// local includes
#include "platform.hpp"

// system includes
#include <cstdint>
#include <string>

// Keep heavy OS headers out of the class declaration by storing handles as
// intptr_t / void* and casting inside the .cpp translation unit.

namespace recplay {
namespace detail {

/// Growth granularity for write-mode files (64 MiB).
static constexpr uint64_t FILE_MAP_GROW_BYTES = 64ull * 1024ull * 1024ull;

/// Memory-mapped file.
///
/// Read mode:
///   auto f = MappedFile::OpenRead("/path/to/file.rec");
///   const uint8_t* p = f.Data();      // base of mapping
///   uint64_t       n = f.FileSize();  // byte count
///
/// Write mode:
///   auto f = MappedFile::OpenWrite("/path/to/file.rec");
///   f.EnsureAvailable(sizeof(MyStruct));
///   std::memcpy(f.WritePtr(), &myStruct, sizeof(myStruct));
///   f.Advance(sizeof(MyStruct));
///   f.Close(); // truncates to BytesWritten()
class MappedFile final {
public:
    MappedFile() noexcept = default;
    ~MappedFile();

    MappedFile(MappedFile&&) noexcept;
    MappedFile& operator=(MappedFile&&) noexcept;

    MappedFile(const MappedFile&)             = delete;
    MappedFile& operator=(const MappedFile&)  = delete;

    // ------------------------------------------------------------------
    // Factory
    // ------------------------------------------------------------------

    /// Open an existing file for reading. Maps the entire file.
    /// Returns an invalid MappedFile (IsOpen() == false) on error.
    static MappedFile OpenRead(const std::string& path);

    /// Create (or truncate) a file for writing.
    /// @param path          Target file path.
    /// @param reserveBytes  Initial file allocation (rounded up to FILE_MAP_GROW_BYTES).
    ///                      Defaults to FILE_MAP_GROW_BYTES (64 MiB).
    /// Returns an invalid MappedFile on error.
    static MappedFile OpenWrite(const std::string& path,
                                uint64_t reserveBytes = FILE_MAP_GROW_BYTES);

    // ------------------------------------------------------------------
    // Common
    // ------------------------------------------------------------------

    bool IsOpen() const noexcept;

    /// Flush dirty pages and close OS handles.
    /// Write-mode: truncates the file to BytesWritten() before closing.
    void Close();

    // ------------------------------------------------------------------
    // Read mode
    // ------------------------------------------------------------------

    /// Pointer to the start of the mapped file data (read mode).
    const uint8_t* Data() const noexcept;

    /// Size of the mapped file in bytes (read mode).
    uint64_t FileSize() const noexcept;

    // ------------------------------------------------------------------
    // Write mode
    // ------------------------------------------------------------------

    /// Pointer to the current write position in the mapped region.
    uint8_t* WritePtr() noexcept;

    /// Bytes available from WritePtr() to the end of the current reservation.
    uint64_t Available() const noexcept;

    /// Total bytes committed via Advance() since the file was opened.
    uint64_t BytesWritten() const noexcept;

    /// Ensure at least `needed` bytes are available from WritePtr().
    /// Grows the file mapping if required (re-maps; WritePtr() may change).
    /// @return false on failure (disk full or OS error).
    bool EnsureAvailable(uint64_t needed);

    /// Move the write cursor forward by `bytes`.
    /// Must be called after writing `bytes` bytes into WritePtr().
    void Advance(uint64_t bytes) noexcept;

private:
    enum class Mode : uint8_t { Read, Write };

    Mode     m_Mode      = Mode::Read;
    uint8_t* m_Base      = nullptr; ///< Base address of the current mapping
    uint64_t m_Reserved  = 0;       ///< Bytes currently reserved on disk (mapped region)
    uint64_t m_Cursor    = 0;       ///< Write-cursor offset from m_Base
    uint64_t m_FileSize  = 0;       ///< Actual file size (read mode)

#if RECPLAY_PLATFORM_WINDOWS
    // Stored as void* to avoid pulling <windows.h> into this header.
    // HANDLE is typedef void* on Windows so the cast in the .cpp is safe.
    void* m_File    = nullptr; ///< INVALID_HANDLE_VALUE equivalent = nullptr sentinel
    void* m_Mapping = nullptr;

    bool GrowWin32(uint64_t newReserved);
    void CloseWin32();
#else
    int  m_Fd = -1;

    bool GrowPosix(uint64_t newReserved);
    void ClosePosix();
#endif
};

}  // namespace detail
}  // namespace recplay
