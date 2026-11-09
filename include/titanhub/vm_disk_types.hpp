#pragma once

#include <cstdint>
#include <array>
#include <string_view>
#include "titanhub/vhd_types.hpp"

namespace titanhub::vhd {

#pragma pack(push, 1)

// ============================================================================
// VDI (Oracle VirtualBox Virtual Disk Image) Specifications
// ============================================================================

constexpr char VDI_TEXT_SIGNATURE[40] = "<<< Oracle VM VirtualBox Disk Image >>>";
constexpr uint32_t VDI_IMAGE_SIGNATURE = 0xBEDA107F;
constexpr uint32_t VDI_VERSION_1_1      = 0x00010001;

enum class VdiImageType : uint32_t {
    NormalDynamic = 1,
    NormalFixed   = 2,
    Undo          = 3,
    Diff          = 4
};

struct VdiHeader {
    char        text[64];              // "<<< Oracle VM VirtualBox Disk Image >>>\n"
    uint32_t    signature;             // 0xBEDA107F
    uint32_t    version;               // 0x00010001
    uint32_t    headerSize;            // 0x00000190 (400 bytes)
    uint32_t    imageType;             // 1=Dynamic, 2=Fixed
    uint32_t    imageFlags;            // 0
    char        description[256];      // Optional description string
    uint32_t    offsetBlocks;          // Byte offset of BAT (block allocation table), e.g. 512
    uint32_t    offsetData;            // Byte offset where data blocks start (1MB aligned)
    uint32_t    cylinders;             // Geometry
    uint32_t    heads;
    uint32_t    sectors;
    uint32_t    sectorSize;            // Typically 512
    uint32_t    unused;
    uint64_t    diskSize;              // Virtual disk size in bytes
    uint32_t    blockSize;             // Block size in bytes (typically 1MB = 1048576)
    uint32_t    blockExtraData;        // 0 for regular VDI
    uint32_t    totalBlocks;           // Number of blocks in virtual disk
    uint32_t    allocatedBlocks;       // Allocated blocks in file
    uint8_t     uuidCreate[16];
    uint8_t     uuidModify[16];
    uint8_t     uuidLinkage[16];
    uint8_t     uuidParent[16];
    uint8_t     unused2[56];           // 56 bytes padding to 512 bytes
};
static_assert(sizeof(VdiHeader) == 512, "VdiHeader must be 512 bytes");

// ============================================================================
// VMDK (VMware Virtual Machine Disk) Specifications
// ============================================================================

constexpr uint32_t VMDK_SPARSE_MAGIC = 0x564d444b; // 'K', 'D', 'M', 'V' in LE

struct VmdkSparseHeader {
    uint32_t    magicNumber;           // VMDK_SPARSE_MAGIC (0x564d444b)
    uint32_t    version;               // 1, 2, or 3
    uint32_t    flags;                 // Bit 0: Valid NL detection, Bit 1: Redundant GD
    uint64_t    capacity;              // Capacity in 512-byte sectors
    uint64_t    grainSize;             // Grain size in sectors (typically 128 = 64KB)
    uint64_t    descriptorOffset;      // Sector offset of embedded text descriptor
    uint64_t    descriptorSize;        // Size of descriptor in sectors
    uint32_t    numGTEsPerGT;          // Number of Grain Table Entries per Grain Table (typically 512)
    uint64_t    rgdOffset;             // Sector offset of redundant Grain Directory
    uint64_t    gdOffset;              // Sector offset of primary Grain Directory
    uint64_t    overHead;              // Overhead in sectors
    uint8_t     uncleanShutdown;       // 1 if unclean shutdown
    char        singleEndLineChar;     // '\n'
    char        nonEndLineChar;        // ' '
    char        doubleEndLineChar1;    // '\r'
    char        doubleEndLineChar2;    // '\n'
    uint16_t    compressAlgorithm;     // 0 = None, 1 = Deflate
    uint8_t     pad[433];              // Pad to 512 bytes
};
static_assert(sizeof(VmdkSparseHeader) == 512, "VmdkSparseHeader must be 512 bytes");

// ============================================================================
// QCOW2 (QEMU Copy-On-Write v2/v3) Specifications
// Big-Endian on disk.
// ============================================================================

constexpr uint32_t QCOW2_MAGIC = 0x514649fb; // "QFI\xfb"

struct Qcow2Header {
    uint32_t    magic;                 // QCOW2_MAGIC (0x514649fb)
    uint32_t    version;               // 2 or 3
    uint64_t    backingFileOffset;     // Offset of backing file string
    uint32_t    backingFileSize;       // Length of backing file string
    uint32_t    clusterBits;           // Cluster size = 1 << clusterBits (e.g. 16 for 64KB)
    uint64_t    size;                  // Virtual disk size in bytes
    uint32_t    cryptMethod;           // 0 = None, 1 = AES
    uint32_t    l1Size;                // Number of entries in L1 table
    uint64_t    l1TableOffset;         // Offset of L1 table in bytes
    uint64_t    refcountTableOffset;   // Offset of refcount table
    uint32_t    refcountTableClusters; // Number of clusters for refcount table
    uint32_t    nbSnapshots;           // Number of snapshots
    uint64_t    snapshotsOffset;       // Snapshot table offset
};
static_assert(sizeof(Qcow2Header) == 72, "Qcow2Header must be 72 bytes");

#pragma pack(pop)

} // namespace titanhub::vhd
