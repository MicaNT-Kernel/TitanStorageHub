#pragma once

#include <cstdint>
#include <array>
#include <string_view>

namespace titanhub::vhd {

// ============================================================================
// VHD (Virtual Hard Disk v1) Structures
// Defined in Microsoft Virtual Hard Disk Image Format Specification v1.0
// All multi-byte fields are Big-Endian on disk.
// ============================================================================

#pragma pack(push, 1)

constexpr char VHD_COOKIE_FOOTER[8] = {'c', 'o', 'n', 'e', 'c', 't', 'i', 'x'};
constexpr char VHD_COOKIE_DYNAMIC[8] = {'c', 'x', 's', 'p', 'a', 'r', 's', 'e'};

enum class VhdDiskType : uint32_t {
    None           = 0,
    Reserved1      = 1,
    Fixed          = 2,
    Dynamic        = 3,
    Differencing   = 4,
    Reserved5      = 5,
    Reserved6      = 6
};

struct VhdFooter {
    char        cookie[8];              // "conectix"
    uint32_t    features;              // Bit field (0x00000002)
    uint32_t    fileFormatVersion;     // 0x00010000
    uint64_t    dataOffset;            // Absolute byte offset to next structure (or 0xFFFFFFFFFFFFFFFF for fixed)
    uint32_t    timestamp;             // Seconds since Jan 1, 2000 00:00:00 UTC
    char        creatorApp[4];         // "vpc ", "win ", etc.
    uint32_t    creatorVersion;        // Version of creator
    char        creatorHostOs[4];      // "Wi2k", "Mac ", etc.
    uint64_t    originalSize;          // Size at creation (bytes)
    uint64_t    currentSize;           // Current virtual size (bytes)
    uint16_t    geometryCylinders;     // CHS cylinders
    uint8_t     geometryHeads;         // CHS heads
    uint8_t     geometrySectors;       // CHS sectors per track
    uint32_t    diskType;              // VhdDiskType (2=Fixed, 3=Dynamic)
    uint32_t    checksum;              // One's complement sum
    uint8_t     uniqueId[16];          // UUID
    uint8_t     savedState;            // 1 if in saved state
    uint8_t     reserved[427];         // Pad to 512 bytes
};
static_assert(sizeof(VhdFooter) == 512, "VhdFooter must be 512 bytes");

struct VhdDynamicHeader {
    char        cookie[8];              // "cxsparse"
    uint64_t    dataOffset;            // 0xFFFFFFFFFFFFFFFF
    uint64_t    tableOffset;           // Absolute byte offset to Block Allocation Table (BAT)
    uint32_t    headerVersion;         // 0x00010000
    uint32_t    maxTableEntries;       // Total blocks in virtual disk
    uint32_t    blockSize;             // Block size in bytes (typically 2MB = 0x00200000)
    uint32_t    checksum;              // Dynamic header checksum
    uint8_t     parentUniqueId[16];    // For differencing disks
    uint32_t    parentTimestamp;       // For differencing disks
    uint32_t    reserved1;
    uint8_t     parentUnicodeName[512];// For differencing disks
    uint8_t     parentLocators[192];   // 8 locators x 24 bytes
    uint8_t     reserved2[256];        // Pad to 1024 bytes
};
static_assert(sizeof(VhdDynamicHeader) == 1024, "VhdDynamicHeader must be 1024 bytes");

// ============================================================================
// VHDX (Virtual Hard Disk v2) Structures
// Defined in Microsoft Virtual Hard Disk v2 (VHDX) Format Specification
// All multi-byte fields are Little-Endian on disk.
// ============================================================================

constexpr char VHDX_FILE_IDENTIFIER[8] = {'v', 'h', 'd', 'x', 'f', 'i', 'l', 'e'};
constexpr char VHDX_HEADER_SIGNATURE[4] = {'h', 'e', 'a', 'd'};
constexpr char VHDX_REGION_SIGNATURE[4] = {'r', 'e', 'g', 'i'};
constexpr char VHDX_METADATA_SIGNATURE[8] = {'m', 'e', 't', 'a', 'd', 'a', 't', 'a'};

struct VhdxFileIdentifier {
    char        signature[8];          // "vhdxfile"
    uint16_t    creator[256];          // UTF-16LE creator name
};
static_assert(sizeof(VhdxFileIdentifier) == 520, "VhdxFileIdentifier must be 520 bytes");

struct VhdxHeader {
    char        signature[4];          // "head"
    uint32_t    checksum;              // CRC32C over 4KB with checksum field zeroed
    uint64_t    sequenceNumber;        // Monotonically increasing sequence
    uint8_t     fileWriteGuid[16];
    uint8_t     dataWriteGuid[16];
    uint8_t     logGuid[16];
    uint16_t    logVersion;            // Must be 0
    uint16_t    version;               // Must be 1
    uint32_t    logLength;             // Length of log in bytes
    uint64_t    logOffset;             // Offset of log in bytes
    uint8_t     reserved[4016];        // Pad to 4096 bytes
};
static_assert(sizeof(VhdxHeader) == 4096, "VhdxHeader must be 4096 bytes");

struct VhdxRegionEntry {
    uint8_t     guid[16];              // BAT GUID or Metadata GUID
    uint64_t    fileOffset;            // Absolute byte offset in file (1MB aligned)
    uint32_t    length;                // Length of region in bytes
    uint32_t    required : 1;          // Must understand to open
    uint32_t    reserved : 31;
};
static_assert(sizeof(VhdxRegionEntry) == 32, "VhdxRegionEntry must be 32 bytes");

struct VhdxRegionTableHeader {
    char            signature[4];      // "regi"
    uint32_t        checksum;          // CRC32C
    uint32_t        entryCount;        // Number of region entries (<= 2047)
    uint32_t        reserved;
    VhdxRegionEntry entries[1];        // Array of entries
};

// Known VHDX Region GUIDs
// BAT GUID: 2DC27766-F623-4200-9D64-115E9BFD4A08
inline constexpr uint8_t VHDX_BAT_GUID[16] = {
    0x66, 0x77, 0xc2, 0x2d, 0x23, 0xf6, 0x00, 0x42,
    0x9d, 0x64, 0x11, 0x5e, 0x9b, 0xfd, 0x4a, 0x08
};

// Metadata GUID: 8B7D40A4-0796-4B83-AC90-3E6B47C5A3E2
inline constexpr uint8_t VHDX_METADATA_GUID[16] = {
    0xa4, 0x40, 0x7d, 0x8b, 0x96, 0x07, 0x83, 0x4b,
    0xac, 0x90, 0x3e, 0x6b, 0x47, 0xc5, 0xa3, 0xe2
};

// Metadata Item GUIDs
// Virtual Disk Size: 2FA42514-B2F6-4764-80A7-E1E5E4ECFAAC (uint64_t)
inline constexpr uint8_t VHDX_META_DISK_SIZE_GUID[16] = {
    0x14, 0x25, 0xa4, 0x2f, 0xf6, 0xb2, 0x64, 0x47,
    0x80, 0xa7, 0xe1, 0xe5, 0xe4, 0xec, 0xfa, 0xac
};

// Logical Sector Size: 8141219F-4625-47A5-9D49-55B67675EE61 (uint32_t, 512 or 4096)
inline constexpr uint8_t VHDX_META_SECTOR_SIZE_GUID[16] = {
    0x9f, 0x21, 0x41, 0x81, 0x25, 0x46, 0xa5, 0x47,
    0x9d, 0x49, 0x55, 0xb6, 0x76, 0x75, 0xee, 0x61
};

// File Parameters: CAA16737-FA36-4D43-B3B6-30DE6C0FB44B (BlockSize: uint32, Bits: uint32)
inline constexpr uint8_t VHDX_META_FILE_PARAMS_GUID[16] = {
    0x37, 0x67, 0xa1, 0xca, 0x36, 0xfa, 0x43, 0x4d,
    0xb3, 0xb6, 0x30, 0xde, 0x6c, 0x0f, 0xb4, 0x4b
};

struct VhdxMetadataTableHeader {
    char        signature[8];          // "metadata"
    uint16_t    reserved;
    uint16_t    entryCount;
    uint32_t    reserved2[5];
};

struct VhdxMetadataEntry {
    uint8_t     itemId[16];            // GUID of metadata item
    uint32_t    offset;                // Offset relative to start of metadata region
    uint32_t    length;                // Length in bytes
    uint32_t    isUser : 1;
    uint32_t    isVirtualDisk : 1;
    uint32_t    isRequired : 1;
    uint32_t    reserved : 29;
    uint32_t    reserved2;
};

// VHDX BAT Entry Payload States
enum VhdxPayloadBlockState : uint32_t {
    VhdxBlockNotPresent       = 0,
    VhdxBlockUndefined        = 1,
    VhdxBlockZero             = 2,
    VhdxBlockUnmapped         = 3,
    VhdxBlockFullyPresent     = 6,
    VhdxBlockPartiallyPresent = 7
};

#pragma pack(pop)

// Endian conversion utilities
inline uint16_t SwapBe16(uint16_t val) noexcept {
    return static_cast<uint16_t>((val << 8) | (val >> 8));
}

inline uint32_t SwapBe32(uint32_t val) noexcept {
    return ((val >> 24) & 0x000000FF) |
           ((val >> 8)  & 0x0000FF00) |
           ((val << 8)  & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
}

inline uint64_t SwapBe64(uint64_t val) noexcept {
    return ((val >> 56) & 0x00000000000000FFULL) |
           ((val >> 40) & 0x000000000000FF00ULL) |
           ((val >> 24) & 0x0000000000FF0000ULL) |
           ((val >> 8)  & 0x00000000FF000000ULL) |
           ((val << 8)  & 0x000000FF00000000ULL) |
           ((val << 24) & 0x0000FF0000000000ULL) |
           ((val << 40) & 0x00FF000000000000ULL) |
           ((val << 56) & 0xFF00000000000000ULL);
}

} // namespace titanhub::vhd
