/// @file detail/file_map.cpp
/// @brief MappedFile implementation — Windows (CreateFileMapping) and POSIX (mmap).

// local includes
#include "file_map.hpp"

// system includes
#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

// ---------------------------------------------------------------------------
// Platform-specific OS headers
// ---------------------------------------------------------------------------
#if RECPLAY_PLATFORM_WINDOWS
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
    // Helpers to cast between void* (stored in header) and HANDLE (Windows typedef).
    static inline HANDLE ToHandle(void* v)   { return reinterpret_cast<HANDLE>(v); }
    static inline void*  FromHandle(HANDLE h){ return reinterpret_cast<void*>(h); }
    // Sentinel matching INVALID_HANDLE_VALUE without including windows.h in the header.
    static void* const INVALID_FILE = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
#else
#   include <cerrno>
#   include <fcntl.h>
#   include <sys/mman.h>
#   include <sys/stat.h>
#   include <unistd.h>
#endif

namespace recplay {
namespace detail {

namespace {
    /// Round `bytes` up to the next multiple of FILE_MAP_GROW_BYTES.
    uint64_t RoundUpToGrow(uint64_t bytes) noexcept {
        return ((bytes + FILE_MAP_GROW_BYTES - 1u) / FILE_MAP_GROW_BYTES) * FILE_MAP_GROW_BYTES;
    }
}  // namespace

// ---------------------------------------------------------------------------
// Destructor / move
// ---------------------------------------------------------------------------

MappedFile::~MappedFile() { Close(); }

MappedFile::MappedFile(MappedFile&& o) noexcept { *this = std::move(o); }

MappedFile& MappedFile::operator=(MappedFile&& o) noexcept {
    if (this == &o) return *this;
    Close();

    m_Mode     = o.m_Mode;
    m_Base     = o.m_Base;
    m_Reserved = o.m_Reserved;
    m_Cursor   = o.m_Cursor;
    m_FileSize = o.m_FileSize;

#if RECPLAY_PLATFORM_WINDOWS
    m_File    = o.m_File;
    m_Mapping = o.m_Mapping;
    o.m_File  = INVALID_FILE;
    o.m_Mapping = nullptr;
#else
    m_Fd = o.m_Fd;
    o.m_Fd = -1;
#endif

    o.m_Base     = nullptr;
    o.m_Reserved = 0;
    o.m_Cursor   = 0;
    o.m_FileSize = 0;
    return *this;
}

// ---------------------------------------------------------------------------
// IsOpen
// ---------------------------------------------------------------------------

bool MappedFile::IsOpen() const noexcept {
#if RECPLAY_PLATFORM_WINDOWS
    return m_File != nullptr && m_File != INVALID_FILE;
#else
    return m_Fd >= 0;
#endif
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const uint8_t* MappedFile::Data()         const noexcept { return m_Base; }
uint64_t       MappedFile::FileSize()     const noexcept { return m_FileSize; }
uint8_t*       MappedFile::WritePtr()           noexcept { return m_Base + m_Cursor; }
uint64_t       MappedFile::BytesWritten() const noexcept { return m_Cursor; }

uint64_t MappedFile::Available() const noexcept {
    return (m_Reserved > m_Cursor) ? (m_Reserved - m_Cursor) : 0u;
}

void MappedFile::Advance(uint64_t bytes) noexcept {
    m_Cursor += bytes;
}

bool MappedFile::EnsureAvailable(uint64_t needed) {
    if (Available() >= needed) return true;
    const uint64_t newReserved = RoundUpToGrow(m_Cursor + needed);
#if RECPLAY_PLATFORM_WINDOWS
    return GrowWin32(newReserved);
#else
    return GrowPosix(newReserved);
#endif
}

// ===========================================================================
// Windows implementation
// ===========================================================================
#if RECPLAY_PLATFORM_WINDOWS

bool MappedFile::GrowWin32(uint64_t newReserved) {
    // 1. Flush and unmap the current view.
    if (m_Base) {
        FlushViewOfFile(m_Base, 0);
        UnmapViewOfFile(m_Base);
        m_Base = nullptr;
    }
    // 2. Close the old mapping object.
    if (m_Mapping) {
        CloseHandle(ToHandle(m_Mapping));
        m_Mapping = nullptr;
    }
    // 3. Extend the file on disk.
    LARGE_INTEGER li{};
    li.QuadPart = static_cast<LONGLONG>(newReserved);
    if (!SetFilePointerEx(ToHandle(m_File), li, nullptr, FILE_BEGIN)) return false;
    if (!SetEndOfFile(ToHandle(m_File)))                               return false;

    // 4. Create a new file-mapping object and map it.
    HANDLE hMap = CreateFileMappingA(ToHandle(m_File), nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (!hMap) return false;

    void* base = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, 0);
    if (!base) { CloseHandle(hMap); return false; }

    m_Mapping  = FromHandle(hMap);
    m_Base     = static_cast<uint8_t*>(base);
    m_Reserved = newReserved;
    return true;
}

void MappedFile::CloseWin32() {
    if (m_Base) {
        FlushViewOfFile(m_Base, 0);
        UnmapViewOfFile(m_Base);
        m_Base = nullptr;
    }
    if (m_Mapping) {
        CloseHandle(ToHandle(m_Mapping));
        m_Mapping = nullptr;
    }
    if (m_File != nullptr && m_File != INVALID_FILE) {
        if (m_Mode == Mode::Write && m_Cursor > 0) {
            // Truncate the file to exactly the bytes written.
            LARGE_INTEGER li{};
            li.QuadPart = static_cast<LONGLONG>(m_Cursor);
            SetFilePointerEx(ToHandle(m_File), li, nullptr, FILE_BEGIN);
            SetEndOfFile(ToHandle(m_File));
        }
        CloseHandle(ToHandle(m_File));
        m_File = INVALID_FILE;
    }
    m_Reserved = 0;
    m_Cursor   = 0;
    m_FileSize = 0;
}

void MappedFile::Close() { CloseWin32(); }

MappedFile MappedFile::OpenRead(const std::string& path) {
    MappedFile f;
    f.m_Mode = Mode::Read;

    HANDLE hFile = CreateFileA(path.c_str(),
        GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return f;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0) {
        CloseHandle(hFile);
        return f;
    }

    HANDLE hMap = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) { CloseHandle(hFile); return f; }

    void* base = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!base) { CloseHandle(hMap); CloseHandle(hFile); return f; }

    f.m_File     = FromHandle(hFile);
    f.m_Mapping  = FromHandle(hMap);
    f.m_Base     = static_cast<uint8_t*>(base);
    f.m_FileSize = static_cast<uint64_t>(size.QuadPart);
    f.m_Reserved = f.m_FileSize;
    return f;
}

MappedFile MappedFile::OpenWrite(const std::string& path, uint64_t reserveBytes) {
    MappedFile f;
    f.m_Mode = Mode::Write;

    const uint64_t initial = RoundUpToGrow(std::max(reserveBytes, FILE_MAP_GROW_BYTES));

    HANDLE hFile = CreateFileA(path.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return f;

    f.m_File = FromHandle(hFile);

    if (!f.GrowWin32(initial)) {
        f.CloseWin32();
        return MappedFile{};
    }
    return f;
}

// ===========================================================================
// POSIX implementation
// ===========================================================================
#else

bool MappedFile::GrowPosix(uint64_t newReserved) {
    // 1. Unmap the current region.
    if (m_Base) {
        ::munmap(m_Base, static_cast<size_t>(m_Reserved));
        m_Base = nullptr;
    }
    // 2. Extend the file (ftruncate fills new bytes with zeros).
    if (::ftruncate(m_Fd, static_cast<off_t>(newReserved)) < 0) return false;

    // 3. Map the full new region.
    void* base = ::mmap(nullptr, static_cast<size_t>(newReserved),
                        PROT_READ | PROT_WRITE, MAP_SHARED, m_Fd, 0);
    if (base == MAP_FAILED) return false;

    m_Base     = static_cast<uint8_t*>(base);
    m_Reserved = newReserved;
    return true;
}

void MappedFile::ClosePosix() {
    if (m_Base) {
        ::msync(m_Base, static_cast<size_t>(m_Reserved), MS_SYNC);
        ::munmap(m_Base, static_cast<size_t>(m_Reserved));
        m_Base = nullptr;
    }
    if (m_Fd >= 0) {
        if (m_Mode == Mode::Write && m_Cursor > 0) {
            // Truncate to the exact number of bytes written.
            ::ftruncate(m_Fd, static_cast<off_t>(m_Cursor));
        }
        ::close(m_Fd);
        m_Fd = -1;
    }
    m_Reserved = 0;
    m_Cursor   = 0;
    m_FileSize = 0;
}

void MappedFile::Close() { ClosePosix(); }

MappedFile MappedFile::OpenRead(const std::string& path) {
    MappedFile f;
    f.m_Mode = Mode::Read;

    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return f;

    struct stat st{};
    if (::fstat(fd, &st) < 0 || st.st_size <= 0) { ::close(fd); return f; }

    const auto size = static_cast<uint64_t>(st.st_size);
    // MAP_POPULATE (Linux) faults in pages upfront for faster sequential reads.
    const int mmapFlags = MAP_SHARED
#if RECPLAY_PLATFORM_LINUX
                        | MAP_POPULATE
#endif
                        ;
    void* base = ::mmap(nullptr, static_cast<size_t>(size),
                        PROT_READ, mmapFlags, fd, 0);
    if (base == MAP_FAILED) { ::close(fd); return f; }

    // Hint sequential access pattern to the kernel prefetcher.
    ::madvise(base, static_cast<size_t>(size), MADV_SEQUENTIAL);

    f.m_Fd       = fd;
    f.m_Base     = static_cast<uint8_t*>(base);
    f.m_FileSize = size;
    f.m_Reserved = size;
    return f;
}

MappedFile MappedFile::OpenWrite(const std::string& path, uint64_t reserveBytes) {
    MappedFile f;
    f.m_Mode = Mode::Write;

    const uint64_t initial = RoundUpToGrow(std::max(reserveBytes, FILE_MAP_GROW_BYTES));

    // O_CLOEXEC: don't inherit fd across fork/exec
    const int fd = ::open(path.c_str(),
                          O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) return f;

    f.m_Fd = fd;
    if (!f.GrowPosix(initial)) {
        f.ClosePosix();
        return MappedFile{};
    }
    return f;
}

#endif  // RECPLAY_PLATFORM_WINDOWS

}  // namespace detail
}  // namespace recplay
