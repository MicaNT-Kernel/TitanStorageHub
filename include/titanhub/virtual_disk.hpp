#pragma once

#include "titanhub/types.hpp"
#include "titanhub/detector.hpp"
#include "titanhub/vhd_types.hpp"
#include "titanhub/vm_disk_types.hpp"

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace titanhub::vhd {

enum class VirtualDiskFormat : uint32_t {
    Unknown     = 0,
    VhdFixed    = 1,
    VhdDynamic  = 2,
    VhdxDynamic = 3,
    VhdxFixed   = 4,
    VdiDynamic  = 5,
    VdiFixed    = 6,
    VmdkSparse  = 7,
    VmdkFlat    = 8,
    Qcow2       = 9,
    RawImage    = 10
};

inline const char* VirtualDiskFormatToString(VirtualDiskFormat fmt) noexcept {
    switch (fmt) {
        case VirtualDiskFormat::VhdFixed:    return "VHD (Fixed)";
        case VirtualDiskFormat::VhdDynamic:  return "VHD (Dynamic Sparse)";
        case VirtualDiskFormat::VhdxDynamic: return "VHDX (Dynamic Sparse)";
        case VirtualDiskFormat::VhdxFixed:   return "VHDX (Fixed)";
        case VirtualDiskFormat::VdiDynamic:  return "VirtualBox VDI (Dynamic Sparse)";
        case VirtualDiskFormat::VdiFixed:    return "VirtualBox VDI (Fixed)";
        case VirtualDiskFormat::VmdkSparse:  return "VMware VMDK (Sparse)";
        case VirtualDiskFormat::VmdkFlat:    return "VMware VMDK (Flat)";
        case VirtualDiskFormat::Qcow2:       return "QEMU QCOW2 (Dynamic CoW)";
        case VirtualDiskFormat::RawImage:    return "Raw Disk Image";
        default:                             return "Unknown Virtual Disk";
    }
}

struct VirtualPartition {
    uint32_t    index{0};
    uint64_t    startSector{0};
    uint64_t    sectorCount{0};
    uint64_t    startOffsetBytes{0};
    uint64_t    lengthBytes{0};
    uint8_t     mbrType{0};
    bool        isGpt{false};
    FsFormat    detectedFs{FsFormat::Unknown};
    std::string fsLabel;
};

// ============================================================================
// IVirtualDisk Interface
// ============================================================================
class IVirtualDisk {
public:
    virtual ~IVirtualDisk() = default;

    virtual VirtualDiskFormat GetFormat() const noexcept = 0;
    virtual uint64_t GetVirtualSize() const noexcept = 0;
    virtual uint32_t GetSectorSize() const noexcept = 0;
    virtual const std::filesystem::path& GetPath() const noexcept = 0;

    virtual void Close() noexcept = 0;
    virtual bool ReadBytes(uint64_t byteOffset, size_t length, void* outBuffer) = 0;

    bool ReadSectors(uint64_t startSector, uint32_t count, void* outBuffer) {
        const uint64_t offset = startSector * GetSectorSize();
        const size_t length = static_cast<size_t>(count) * GetSectorSize();
        return ReadBytes(offset, length, outBuffer);
    }

    std::vector<VirtualPartition> DiscoverPartitions() {
        std::vector<VirtualPartition> partitions;
        const uint32_t sectorSize = GetSectorSize();
        std::vector<uint8_t> sector0(sectorSize > 512 ? sectorSize : 512, 0);

        if (!ReadBytes(0, sector0.size(), sector0.data())) {
            return partitions;
        }

        // Validate MBR Signature: 0x55, 0xAA at 510
        if (sector0[510] != 0x55 || sector0[511] != 0xAA) {
            return partitions;
        }

        // Check if Protective MBR for GPT
        bool isGptDisk = false;
        for (int i = 0; i < 4; ++i) {
            const size_t entryOffset = 446 + (i * 16);
            const uint8_t sysId = sector0[entryOffset + 4];
            if (sysId == 0xEE) {
                isGptDisk = true;
                break;
            }
        }

        if (isGptDisk) {
            // Read GPT Header at LBA 1
            std::vector<uint8_t> gptHeader(sectorSize, 0);
            if (ReadBytes(sectorSize, sectorSize, gptHeader.data()) &&
                std::memcmp(gptHeader.data(), "EFI PART", 8) == 0) {
                
                uint64_t partitionEntryLba = *reinterpret_cast<const uint64_t*>(gptHeader.data() + 72);
                uint32_t numPartitionEntries = *reinterpret_cast<const uint32_t*>(gptHeader.data() + 80);
                uint32_t sizeofPartitionEntry = *reinterpret_cast<const uint32_t*>(gptHeader.data() + 84);

                if (sizeofPartitionEntry >= 128 && numPartitionEntries > 0 && numPartitionEntries <= 256) {
                    const size_t tableSize = numPartitionEntries * sizeofPartitionEntry;
                    std::vector<uint8_t> entryTable(tableSize, 0);
                    if (ReadBytes(partitionEntryLba * sectorSize, tableSize, entryTable.data())) {
                        uint32_t partIdx = 1;
                        for (uint32_t p = 0; p < numPartitionEntries; ++p) {
                            const uint8_t* pEntry = entryTable.data() + (p * sizeofPartitionEntry);
                            // Empty Partition GUID is all zeros
                            bool allZero = true;
                            for (int b = 0; b < 16; ++b) {
                                if (pEntry[b] != 0) { allZero = false; break; }
                            }
                            if (allZero) continue;

                            uint64_t firstLba = *reinterpret_cast<const uint64_t*>(pEntry + 32);
                            uint64_t lastLba = *reinterpret_cast<const uint64_t*>(pEntry + 40);
                            if (lastLba < firstLba) continue;

                            VirtualPartition vp{};
                            vp.index = partIdx++;
                            vp.startSector = firstLba;
                            vp.sectorCount = (lastLba - firstLba + 1);
                            vp.startOffsetBytes = firstLba * sectorSize;
                            vp.lengthBytes = vp.sectorCount * sectorSize;
                            vp.isGpt = true;

                            // Inspect filesystem on this partition
                            ProbePartitionFilesystem(vp);
                            partitions.push_back(vp);
                        }
                    }
                }
            }
        }

        // If not GPT or GPT had no partitions, parse MBR table
        if (partitions.empty()) {
            uint32_t partIdx = 1;
            for (int i = 0; i < 4; ++i) {
                const size_t entryOffset = 446 + (i * 16);
                const uint8_t sysId = sector0[entryOffset + 4];
                if (sysId == 0 || sysId == 0xEE) continue;

                uint32_t startLba = *reinterpret_cast<const uint32_t*>(sector0.data() + entryOffset + 8);
                uint32_t sectorCount = *reinterpret_cast<const uint32_t*>(sector0.data() + entryOffset + 12);
                if (sectorCount == 0) continue;

                VirtualPartition vp{};
                vp.index = partIdx++;
                vp.startSector = startLba;
                vp.sectorCount = sectorCount;
                vp.startOffsetBytes = static_cast<uint64_t>(startLba) * sectorSize;
                vp.lengthBytes = static_cast<uint64_t>(sectorCount) * sectorSize;
                vp.mbrType = sysId;
                vp.isGpt = false;

                ProbePartitionFilesystem(vp);
                partitions.push_back(vp);
            }
        }

        return partitions;
    }

private:
    void ProbePartitionFilesystem(VirtualPartition& part) {
        // Read up to first 128KB of partition to detect filesystem format & label
        constexpr size_t PROBE_SIZE = 131072;
        std::vector<uint8_t> probeBuf(PROBE_SIZE, 0);
        size_t toRead = std::min<size_t>(PROBE_SIZE, static_cast<size_t>(part.lengthBytes));

        if (ReadBytes(part.startOffsetBytes, toRead, probeBuf.data())) {
            std::string label;
            part.detectedFs = DiskFormatDetector::DetectFormatFromBuffer(
                std::span<const uint8_t>(probeBuf.data(), toRead),
                &label
            );
            part.fsLabel = label;
        }
    }
};

// ============================================================================
// VHD Implementation (Fixed & Dynamic)
// ============================================================================
class VhdDisk final : public IVirtualDisk {
public:
    static std::unique_ptr<VhdDisk> Open(const std::filesystem::path& path, std::string* outError = nullptr) {
        auto disk = std::unique_ptr<VhdDisk>(new VhdDisk(path));
        if (!disk->Initialize(outError)) {
            return nullptr;
        }
        return disk;
    }

    VirtualDiskFormat GetFormat() const noexcept override { return m_Format; }
    uint64_t GetVirtualSize() const noexcept override { return m_VirtualSize; }
    uint32_t GetSectorSize() const noexcept override { return 512; }
    const std::filesystem::path& GetPath() const noexcept override { return m_Path; }

    void Close() noexcept override {
        if (m_File.is_open()) {
            m_File.close();
        }
    }

    bool ReadBytes(uint64_t byteOffset, size_t length, void* outBuffer) override {
        if (!m_File.is_open() || outBuffer == nullptr) return false;
        if (byteOffset >= m_VirtualSize) return false;

        uint8_t* dst = static_cast<uint8_t*>(outBuffer);
        size_t remaining = length;
        uint64_t currentOffset = byteOffset;

        // Clip to virtual size
        if (currentOffset + remaining > m_VirtualSize) {
            remaining = static_cast<size_t>(m_VirtualSize - currentOffset);
            std::memset(dst + remaining, 0, length - remaining);
        }

        if (m_Format == VirtualDiskFormat::VhdFixed) {
            m_File.seekg(static_cast<std::streamoff>(currentOffset), std::ios::beg);
            m_File.read(reinterpret_cast<char*>(dst), remaining);
            return m_File.good() || m_File.gcount() == static_cast<std::streamsize>(remaining);
        }

        // Dynamic VHD Block Traversal
        while (remaining > 0) {
            const uint32_t blockIdx = static_cast<uint32_t>(currentOffset / m_BlockSize);
            const uint32_t inBlockOffset = static_cast<uint32_t>(currentOffset % m_BlockSize);
            const size_t chunk = std::min<size_t>(remaining, m_BlockSize - inBlockOffset);

            if (blockIdx >= m_Bat.size() || m_Bat[blockIdx] == 0xFFFFFFFF) {
                // Block unallocated / sparse: return zeros
                std::memset(dst, 0, chunk);
            } else {
                // Block allocated: bat entry is sector index in file
                // Skip the sector bitmap preceding the payload block
                const uint64_t blockFileOffset = (static_cast<uint64_t>(m_Bat[blockIdx]) * 512) + m_BitmapSizeSectors * 512 + inBlockOffset;
                m_File.seekg(static_cast<std::streamoff>(blockFileOffset), std::ios::beg);
                m_File.read(reinterpret_cast<char*>(dst), chunk);
                if (!m_File.good() && m_File.gcount() != static_cast<std::streamsize>(chunk)) {
                    std::memset(dst, 0, chunk);
                }
            }

            dst += chunk;
            currentOffset += chunk;
            remaining -= chunk;
        }

        return true;
    }

private:
    explicit VhdDisk(std::filesystem::path path) : m_Path(std::move(path)) {}

    bool Initialize(std::string* outError) {
        m_File.open(m_Path, std::ios::binary);
        if (!m_File.is_open()) {
            if (outError) *outError = "Failed to open file: " + m_Path.string();
            return false;
        }

        m_File.seekg(0, std::ios::end);
        const uint64_t fileSize = static_cast<uint64_t>(m_File.tellg());
        if (fileSize < 512) {
            if (outError) *outError = "File too small to be a valid VHD (< 512 bytes)";
            return false;
        }

        // 1. Try reading footer from end of file
        VhdFooter footer{};
        m_File.seekg(static_cast<std::streamoff>(fileSize - 512), std::ios::beg);
        m_File.read(reinterpret_cast<char*>(&footer), 512);

        if (std::memcmp(footer.cookie, VHD_COOKIE_FOOTER, 8) != 0) {
            // Also check offset 0 (dynamic VHD duplicate header)
            m_File.seekg(0, std::ios::beg);
            m_File.read(reinterpret_cast<char*>(&footer), 512);
            if (std::memcmp(footer.cookie, VHD_COOKIE_FOOTER, 8) != 0) {
                if (outError) *outError = "Invalid VHD cookie signature ('conectix' expected)";
                return false;
            }
        }

        const uint32_t diskType = SwapBe32(footer.diskType);
        m_VirtualSize = SwapBe64(footer.currentSize);

        if (diskType == static_cast<uint32_t>(VhdDiskType::Fixed)) {
            m_Format = VirtualDiskFormat::VhdFixed;
            return true;
        }

        if (diskType == static_cast<uint32_t>(VhdDiskType::Dynamic)) {
            m_Format = VirtualDiskFormat::VhdDynamic;
            const uint64_t dynOffset = SwapBe64(footer.dataOffset);
            if (dynOffset == 0xFFFFFFFFFFFFFFFFULL || dynOffset > fileSize - 1024) {
                if (outError) *outError = "Invalid dynamic VHD header offset";
                return false;
            }

            VhdDynamicHeader dyn{};
            m_File.seekg(static_cast<std::streamoff>(dynOffset), std::ios::beg);
            m_File.read(reinterpret_cast<char*>(&dyn), sizeof(dyn));

            if (std::memcmp(dyn.cookie, VHD_COOKIE_DYNAMIC, 8) != 0) {
                if (outError) *outError = "Invalid VHD dynamic cookie signature ('cxsparse' expected)";
                return false;
            }

            m_BlockSize = SwapBe32(dyn.blockSize);
            if (m_BlockSize == 0 || (m_BlockSize & (m_BlockSize - 1)) != 0) {
                m_BlockSize = 2097152; // Default 2MB
            }

            // In dynamic VHD, each block has a sector bitmap preceding its data
            // Bitmap size = (BlockSize / 512 / 8) bytes, rounded up to 512-byte sectors
            const uint32_t bitmapBytes = (m_BlockSize / 512) / 8;
            m_BitmapSizeSectors = (bitmapBytes + 511) / 512;

            const uint64_t batOffset = SwapBe64(dyn.tableOffset);
            const uint32_t maxEntries = SwapBe32(dyn.maxTableEntries);
            if (batOffset > fileSize || maxEntries == 0 || maxEntries > 1048576) {
                if (outError) *outError = "Corrupted VHD BAT table configuration";
                return false;
            }

            m_Bat.resize(maxEntries);
            m_File.seekg(static_cast<std::streamoff>(batOffset), std::ios::beg);
            m_File.read(reinterpret_cast<char*>(m_Bat.data()), maxEntries * sizeof(uint32_t));

            // Convert BAT entries from Big-Endian to Host
            for (auto& entry : m_Bat) {
                entry = SwapBe32(entry);
            }

            return true;
        }

        if (outError) *outError = "Unsupported VHD disk type: " + std::to_string(diskType);
        return false;
    }

    std::filesystem::path m_Path;
    std::ifstream         m_File;
    VirtualDiskFormat     m_Format{VirtualDiskFormat::Unknown};
    uint64_t              m_VirtualSize{0};
    uint32_t              m_BlockSize{2097152};
    uint32_t              m_BitmapSizeSectors{1};
    std::vector<uint32_t> m_Bat;
};

// ============================================================================
// VHDX Implementation (Modern Hyper-V / VirtualBox VHDX)
// ============================================================================
class VhdxDisk final : public IVirtualDisk {
public:
    static std::unique_ptr<VhdxDisk> Open(const std::filesystem::path& path, std::string* outError = nullptr) {
        auto disk = std::unique_ptr<VhdxDisk>(new VhdxDisk(path));
        if (!disk->Initialize(outError)) {
            return nullptr;
        }
        return disk;
    }

    VirtualDiskFormat GetFormat() const noexcept override { return m_Format; }
    uint64_t GetVirtualSize() const noexcept override { return m_VirtualSize; }
    uint32_t GetSectorSize() const noexcept override { return m_LogicalSectorSize; }
    const std::filesystem::path& GetPath() const noexcept override { return m_Path; }

    void Close() noexcept override {
        if (m_File.is_open()) {
            m_File.close();
        }
    }

    bool ReadBytes(uint64_t byteOffset, size_t length, void* outBuffer) override {
        if (!m_File.is_open() || outBuffer == nullptr) return false;
        if (byteOffset >= m_VirtualSize) return false;

        uint8_t* dst = static_cast<uint8_t*>(outBuffer);
        size_t remaining = length;
        uint64_t currentOffset = byteOffset;

        if (currentOffset + remaining > m_VirtualSize) {
            remaining = static_cast<size_t>(m_VirtualSize - currentOffset);
            std::memset(dst + remaining, 0, length - remaining);
        }

        while (remaining > 0) {
            const uint64_t blockIdx = currentOffset / m_BlockSize;
            const uint32_t inBlockOffset = static_cast<uint32_t>(currentOffset % m_BlockSize);
            const size_t chunk = std::min<size_t>(remaining, m_BlockSize - inBlockOffset);

            // In VHDX, sector bitmap blocks are interspersed in the BAT every ChunkRatio entries:
            // ChunkRatio = (1 << 23) * LogicalSectorSize / BlockSize
            const uint64_t batEntryIndex = m_ChunkRatio > 0 
                ? (blockIdx + (blockIdx / m_ChunkRatio)) 
                : blockIdx;

            if (batEntryIndex >= m_Bat.size()) {
                std::memset(dst, 0, chunk);
            } else {
                const uint64_t batEntry = m_Bat[batEntryIndex];
                const uint32_t state = static_cast<uint32_t>(batEntry & 0x7);
                const uint64_t fileOffsetMB = batEntry >> 20;

                if (state == VhdxBlockFullyPresent || state == VhdxBlockPartiallyPresent) {
                    const uint64_t blockFileOffset = (fileOffsetMB * 1048576ULL) + inBlockOffset;
                    m_File.seekg(static_cast<std::streamoff>(blockFileOffset), std::ios::beg);
                    m_File.read(reinterpret_cast<char*>(dst), chunk);
                    if (!m_File.good() && m_File.gcount() != static_cast<std::streamsize>(chunk)) {
                        std::memset(dst, 0, chunk);
                    }
                } else {
                    // BlockZero or NotPresent: sparse zeros
                    std::memset(dst, 0, chunk);
                }
            }

            dst += chunk;
            currentOffset += chunk;
            remaining -= chunk;
        }

        return true;
    }

private:
    explicit VhdxDisk(std::filesystem::path path) : m_Path(std::move(path)) {}

    bool Initialize(std::string* outError) {
        m_File.open(m_Path, std::ios::binary);
        if (!m_File.is_open()) {
            if (outError) *outError = "Failed to open file: " + m_Path.string();
            return false;
        }

        // 1. Verify VHDX File Identifier at offset 0
        VhdxFileIdentifier ident{};
        m_File.seekg(0, std::ios::beg);
        m_File.read(reinterpret_cast<char*>(&ident), sizeof(ident));
        if (std::memcmp(ident.signature, VHDX_FILE_IDENTIFIER, 8) != 0) {
            if (outError) *outError = "Invalid VHDX signature ('vhdxfile' expected)";
            return false;
        }

        // 2. Read Region Table Header at 192KB (offset 0x30000)
        std::vector<uint8_t> regionBuf(65536, 0);
        m_File.seekg(0x30000, std::ios::beg);
        m_File.read(reinterpret_cast<char*>(regionBuf.data()), regionBuf.size());

        if (std::memcmp(regionBuf.data(), VHDX_REGION_SIGNATURE, 4) != 0) {
            // Check secondary region table at 256KB (offset 0x40000)
            m_File.seekg(0x40000, std::ios::beg);
            m_File.read(reinterpret_cast<char*>(regionBuf.data()), regionBuf.size());
            if (std::memcmp(regionBuf.data(), VHDX_REGION_SIGNATURE, 4) != 0) {
                if (outError) *outError = "Invalid VHDX region table signature ('regi' expected)";
                return false;
            }
        }

        const auto* regHeader = reinterpret_cast<const VhdxRegionTableHeader*>(regionBuf.data());
        const uint32_t entryCount = regHeader->entryCount;
        uint64_t batOffset = 0;
        uint32_t batLength = 0;
        uint64_t metaOffset = 0;
        uint32_t metaLength = 0;

        for (uint32_t i = 0; i < entryCount && i < 2047; ++i) {
            const auto& entry = regHeader->entries[i];
            if (std::memcmp(entry.guid, VHDX_BAT_GUID, 16) == 0) {
                batOffset = entry.fileOffset;
                batLength = entry.length;
            } else if (std::memcmp(entry.guid, VHDX_METADATA_GUID, 16) == 0) {
                metaOffset = entry.fileOffset;
                metaLength = entry.length;
            }
        }

        if (batOffset == 0 || metaOffset == 0) {
            if (outError) *outError = "Missing BAT or Metadata region in VHDX";
            return false;
        }

        // 3. Parse Metadata Region to get Disk Size and Block Size
        std::vector<uint8_t> metaBuf(metaLength > 0 ? metaLength : 65536, 0);
        m_File.seekg(static_cast<std::streamoff>(metaOffset), std::ios::beg);
        m_File.read(reinterpret_cast<char*>(metaBuf.data()), metaBuf.size());

        const auto* metaHeader = reinterpret_cast<const VhdxMetadataTableHeader*>(metaBuf.data());
        if (std::memcmp(metaHeader->signature, VHDX_METADATA_SIGNATURE, 8) != 0) {
            if (outError) *outError = "Invalid VHDX metadata signature ('metadata' expected)";
            return false;
        }

        const auto* entries = reinterpret_cast<const VhdxMetadataEntry*>(metaBuf.data() + sizeof(VhdxMetadataTableHeader));
        for (uint16_t m = 0; m < metaHeader->entryCount; ++m) {
            const auto& item = entries[m];
            if (item.offset + item.length <= metaBuf.size()) {
                const uint8_t* pData = metaBuf.data() + item.offset;
                if (std::memcmp(item.itemId, VHDX_META_DISK_SIZE_GUID, 16) == 0 && item.length >= 8) {
                    m_VirtualSize = *reinterpret_cast<const uint64_t*>(pData);
                } else if (std::memcmp(item.itemId, VHDX_META_SECTOR_SIZE_GUID, 16) == 0 && item.length >= 4) {
                    m_LogicalSectorSize = *reinterpret_cast<const uint32_t*>(pData);
                } else if (std::memcmp(item.itemId, VHDX_META_FILE_PARAMS_GUID, 16) == 0 && item.length >= 8) {
                    m_BlockSize = *reinterpret_cast<const uint32_t*>(pData);
                }
            }
        }

        if (m_VirtualSize == 0 || m_BlockSize == 0) {
            if (outError) *outError = "Failed to extract VHDX disk geometry from metadata";
            return false;
        }

        // Calculate Chunk Ratio for VHDX: (1 << 23) * LogicalSectorSize / BlockSize
        m_ChunkRatio = (static_cast<uint64_t>(1) << 23) * m_LogicalSectorSize / m_BlockSize;

        // 4. Load BAT (Block Allocation Table)
        const size_t totalBatEntries = batLength / sizeof(uint64_t);
        m_Bat.resize(totalBatEntries);
        m_File.seekg(static_cast<std::streamoff>(batOffset), std::ios::beg);
        m_File.read(reinterpret_cast<char*>(m_Bat.data()), batLength);

        m_Format = VirtualDiskFormat::VhdxDynamic;
        return true;
    }

    std::filesystem::path m_Path;
    std::ifstream         m_File;
    VirtualDiskFormat     m_Format{VirtualDiskFormat::Unknown};
    uint64_t              m_VirtualSize{0};
    uint32_t              m_BlockSize{33554432}; // Typical VHDX block size is 32MB
    uint32_t              m_LogicalSectorSize{512};
    uint64_t              m_ChunkRatio{0};
    std::vector<uint64_t> m_Bat;
};

// ============================================================================
// VDI Implementation (Oracle VirtualBox VDI)
// ============================================================================
class VdiDisk final : public IVirtualDisk {
public:
    static std::unique_ptr<VdiDisk> Open(const std::filesystem::path& path, std::string* outError = nullptr) {
        auto disk = std::unique_ptr<VdiDisk>(new VdiDisk(path));
        if (!disk->Initialize(outError)) return nullptr;
        return disk;
    }

    VirtualDiskFormat GetFormat() const noexcept override { return m_Format; }
    uint64_t GetVirtualSize() const noexcept override { return m_VirtualSize; }
    uint32_t GetSectorSize() const noexcept override { return m_SectorSize; }
    const std::filesystem::path& GetPath() const noexcept override { return m_Path; }

    void Close() noexcept override {
        if (m_File.is_open()) m_File.close();
    }

    bool ReadBytes(uint64_t byteOffset, size_t length, void* outBuffer) override {
        if (!m_File.is_open() || outBuffer == nullptr) return false;
        if (byteOffset >= m_VirtualSize) return false;

        uint8_t* dst = static_cast<uint8_t*>(outBuffer);
        size_t remaining = length;
        uint64_t currentOffset = byteOffset;

        if (currentOffset + remaining > m_VirtualSize) {
            remaining = static_cast<size_t>(m_VirtualSize - currentOffset);
            std::memset(dst + remaining, 0, length - remaining);
        }

        while (remaining > 0) {
            const uint32_t blockIdx = static_cast<uint32_t>(currentOffset / m_BlockSize);
            const uint32_t inBlockOffset = static_cast<uint32_t>(currentOffset % m_BlockSize);
            const size_t chunk = std::min<size_t>(remaining, m_BlockSize - inBlockOffset);

            if (blockIdx >= m_Bat.size() || m_Bat[blockIdx] == 0xFFFFFFFF || m_Bat[blockIdx] == 0xFFFFFFFE) {
                std::memset(dst, 0, chunk);
            } else {
                const uint64_t blockFileOffset = static_cast<uint64_t>(m_OffsetData) + 
                    (static_cast<uint64_t>(m_Bat[blockIdx]) * (m_BlockSize + m_BlockExtra)) + inBlockOffset;
                m_File.seekg(static_cast<std::streamoff>(blockFileOffset), std::ios::beg);
                m_File.read(reinterpret_cast<char*>(dst), chunk);
                if (!m_File.good() && m_File.gcount() != static_cast<std::streamsize>(chunk)) {
                    std::memset(dst, 0, chunk);
                }
            }

            dst += chunk;
            currentOffset += chunk;
            remaining -= chunk;
        }

        return true;
    }

private:
    explicit VdiDisk(std::filesystem::path path) : m_Path(std::move(path)) {}

    bool Initialize(std::string* outError) {
        m_File.open(m_Path, std::ios::binary);
        if (!m_File.is_open()) {
            if (outError) *outError = "Failed to open VDI file: " + m_Path.string();
            return false;
        }

        VdiHeader hdr{};
        m_File.seekg(0, std::ios::beg);
        m_File.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        if (hdr.signature != VDI_IMAGE_SIGNATURE) {
            if (outError) *outError = "Invalid VDI image signature (0xBEDA107F expected)";
            return false;
        }

        m_VirtualSize = hdr.diskSize;
        m_BlockSize = hdr.blockSize > 0 ? hdr.blockSize : 1048576;
        m_BlockExtra = hdr.blockExtraData;
        m_OffsetData = hdr.offsetData;
        m_SectorSize = hdr.sectorSize > 0 ? hdr.sectorSize : 512;

        if (hdr.imageType == static_cast<uint32_t>(VdiImageType::NormalFixed)) {
            m_Format = VirtualDiskFormat::VdiFixed;
        } else {
            m_Format = VirtualDiskFormat::VdiDynamic;
        }

        const uint32_t totalBlocks = hdr.totalBlocks;
        if (totalBlocks > 0 && hdr.offsetBlocks > 0) {
            m_Bat.resize(totalBlocks);
            m_File.seekg(static_cast<std::streamoff>(hdr.offsetBlocks), std::ios::beg);
            m_File.read(reinterpret_cast<char*>(m_Bat.data()), totalBlocks * sizeof(uint32_t));
        }

        return true;
    }

    std::filesystem::path m_Path;
    std::ifstream         m_File;
    VirtualDiskFormat     m_Format{VirtualDiskFormat::VdiDynamic};
    uint64_t              m_VirtualSize{0};
    uint32_t              m_BlockSize{1048576};
    uint32_t              m_BlockExtra{0};
    uint32_t              m_OffsetData{0};
    uint32_t              m_SectorSize{512};
    std::vector<uint32_t> m_Bat;
};

// ============================================================================
// VMDK Implementation (VMware VMDK Sparse)
// ============================================================================
class VmdkDisk final : public IVirtualDisk {
public:
    static std::unique_ptr<VmdkDisk> Open(const std::filesystem::path& path, std::string* outError = nullptr) {
        auto disk = std::unique_ptr<VmdkDisk>(new VmdkDisk(path));
        if (!disk->Initialize(outError)) return nullptr;
        return disk;
    }

    VirtualDiskFormat GetFormat() const noexcept override { return m_Format; }
    uint64_t GetVirtualSize() const noexcept override { return m_VirtualSize; }
    uint32_t GetSectorSize() const noexcept override { return 512; }
    const std::filesystem::path& GetPath() const noexcept override { return m_Path; }

    void Close() noexcept override {
        if (m_File.is_open()) m_File.close();
    }

    bool ReadBytes(uint64_t byteOffset, size_t length, void* outBuffer) override {
        if (!m_File.is_open() || outBuffer == nullptr) return false;
        if (byteOffset >= m_VirtualSize) return false;

        uint8_t* dst = static_cast<uint8_t*>(outBuffer);
        size_t remaining = length;
        uint64_t currentOffset = byteOffset;

        if (currentOffset + remaining > m_VirtualSize) {
            remaining = static_cast<size_t>(m_VirtualSize - currentOffset);
            std::memset(dst + remaining, 0, length - remaining);
        }

        const uint64_t grainBytes = m_GrainSizeSectors * 512ULL;
        while (remaining > 0) {
            const uint64_t grainIdx = currentOffset / grainBytes;
            const uint32_t inGrainOffset = static_cast<uint32_t>(currentOffset % grainBytes);
            const size_t chunk = std::min<size_t>(remaining, grainBytes - inGrainOffset);

            const uint64_t gdIdx = grainIdx / m_NumGTEsPerGT;
            const uint64_t gtIdx = grainIdx % m_NumGTEsPerGT;

            uint32_t grainSector = 0;
            if (gdIdx < m_Gd.size() && m_Gd[gdIdx] != 0) {
                const uint64_t gtOffset = static_cast<uint64_t>(m_Gd[gdIdx]) * 512ULL;
                if (m_CachedGtOffset != gtOffset) {
                    m_CachedGt.resize(m_NumGTEsPerGT);
                    m_File.seekg(static_cast<std::streamoff>(gtOffset), std::ios::beg);
                    m_File.read(reinterpret_cast<char*>(m_CachedGt.data()), m_NumGTEsPerGT * sizeof(uint32_t));
                    m_CachedGtOffset = gtOffset;
                }
                if (gtIdx < m_CachedGt.size()) {
                    grainSector = m_CachedGt[gtIdx];
                }
            }

            if (grainSector == 0) {
                std::memset(dst, 0, chunk);
            } else {
                const uint64_t grainFileOffset = (static_cast<uint64_t>(grainSector) * 512ULL) + inGrainOffset;
                m_File.seekg(static_cast<std::streamoff>(grainFileOffset), std::ios::beg);
                m_File.read(reinterpret_cast<char*>(dst), chunk);
                if (!m_File.good() && m_File.gcount() != static_cast<std::streamsize>(chunk)) {
                    std::memset(dst, 0, chunk);
                }
            }

            dst += chunk;
            currentOffset += chunk;
            remaining -= chunk;
        }

        return true;
    }

private:
    explicit VmdkDisk(std::filesystem::path path) : m_Path(std::move(path)) {}

    bool Initialize(std::string* outError) {
        m_File.open(m_Path, std::ios::binary);
        if (!m_File.is_open()) {
            if (outError) *outError = "Failed to open VMDK file: " + m_Path.string();
            return false;
        }

        VmdkSparseHeader hdr{};
        m_File.seekg(0, std::ios::beg);
        m_File.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));

        if (hdr.magicNumber != VMDK_SPARSE_MAGIC) {
            if (outError) *outError = "Invalid VMDK sparse magic (0x564d444b expected)";
            return false;
        }

        m_VirtualSize = hdr.capacity * 512ULL;
        m_GrainSizeSectors = hdr.grainSize > 0 ? hdr.grainSize : 128;
        m_NumGTEsPerGT = hdr.numGTEsPerGT > 0 ? hdr.numGTEsPerGT : 512;
        m_Format = VirtualDiskFormat::VmdkSparse;

        const uint64_t totalGrains = (hdr.capacity + m_GrainSizeSectors - 1) / m_GrainSizeSectors;
        const uint64_t totalGTs = (totalGrains + m_NumGTEsPerGT - 1) / m_NumGTEsPerGT;

        if (hdr.gdOffset > 0 && totalGTs > 0) {
            m_Gd.resize(static_cast<size_t>(totalGTs));
            m_File.seekg(static_cast<std::streamoff>(hdr.gdOffset * 512ULL), std::ios::beg);
            m_File.read(reinterpret_cast<char*>(m_Gd.data()), totalGTs * sizeof(uint32_t));
        }

        return true;
    }

    std::filesystem::path m_Path;
    std::ifstream         m_File;
    VirtualDiskFormat     m_Format{VirtualDiskFormat::VmdkSparse};
    uint64_t              m_VirtualSize{0};
    uint64_t              m_GrainSizeSectors{128};
    uint32_t              m_NumGTEsPerGT{512};
    std::vector<uint32_t> m_Gd;
    std::vector<uint32_t> m_CachedGt;
    uint64_t              m_CachedGtOffset{0xFFFFFFFFFFFFFFFFULL};
};

// ============================================================================
// QCOW2 Implementation (QEMU Copy-On-Write v2/v3)
// ============================================================================
class Qcow2Disk final : public IVirtualDisk {
public:
    static std::unique_ptr<Qcow2Disk> Open(const std::filesystem::path& path, std::string* outError = nullptr) {
        auto disk = std::unique_ptr<Qcow2Disk>(new Qcow2Disk(path));
        if (!disk->Initialize(outError)) return nullptr;
        return disk;
    }

    VirtualDiskFormat GetFormat() const noexcept override { return m_Format; }
    uint64_t GetVirtualSize() const noexcept override { return m_VirtualSize; }
    uint32_t GetSectorSize() const noexcept override { return 512; }
    const std::filesystem::path& GetPath() const noexcept override { return m_Path; }

    void Close() noexcept override {
        if (m_File.is_open()) m_File.close();
    }

    bool ReadBytes(uint64_t byteOffset, size_t length, void* outBuffer) override {
        if (!m_File.is_open() || outBuffer == nullptr) return false;
        if (byteOffset >= m_VirtualSize) return false;

        uint8_t* dst = static_cast<uint8_t*>(outBuffer);
        size_t remaining = length;
        uint64_t currentOffset = byteOffset;

        if (currentOffset + remaining > m_VirtualSize) {
            remaining = static_cast<size_t>(m_VirtualSize - currentOffset);
            std::memset(dst + remaining, 0, length - remaining);
        }

        const uint64_t clusterSize = 1ULL << m_ClusterBits;
        const uint64_t l2Entries = 1ULL << (m_ClusterBits - 3);

        while (remaining > 0) {
            const uint64_t clusterIdx = currentOffset / clusterSize;
            const uint32_t inClusterOffset = static_cast<uint32_t>(currentOffset % clusterSize);
            const size_t chunk = std::min<size_t>(remaining, clusterSize - inClusterOffset);

            const uint64_t l1Idx = clusterIdx / l2Entries;
            const uint64_t l2Idx = clusterIdx % l2Entries;

            uint64_t clusterOffset = 0;
            if (l1Idx < m_L1Table.size() && m_L1Table[l1Idx] != 0) {
                const uint64_t l2TableOffset = m_L1Table[l1Idx] & ~511ULL;
                if (m_CachedL2Offset != l2TableOffset) {
                    m_CachedL2.resize(static_cast<size_t>(l2Entries));
                    m_File.seekg(static_cast<std::streamoff>(l2TableOffset), std::ios::beg);
                    m_File.read(reinterpret_cast<char*>(m_CachedL2.data()), l2Entries * sizeof(uint64_t));
                    for (auto& entry : m_CachedL2) {
                        entry = SwapBe64(entry);
                    }
                    m_CachedL2Offset = l2TableOffset;
                }
                if (l2Idx < m_CachedL2.size()) {
                    const uint64_t l2Entry = m_CachedL2[l2Idx];
                    // Check standard cluster (not compressed, not zero cluster)
                    if ((l2Entry & 1) == 0 && (l2Entry & (1ULL << 62)) == 0) {
                        clusterOffset = l2Entry & 0x00FFFFFFFFFFFF00ULL;
                    }
                }
            }

            if (clusterOffset == 0) {
                std::memset(dst, 0, chunk);
            } else {
                const uint64_t fileOffset = clusterOffset + inClusterOffset;
                m_File.seekg(static_cast<std::streamoff>(fileOffset), std::ios::beg);
                m_File.read(reinterpret_cast<char*>(dst), chunk);
                if (!m_File.good() && m_File.gcount() != static_cast<std::streamsize>(chunk)) {
                    std::memset(dst, 0, chunk);
                }
            }

            dst += chunk;
            currentOffset += chunk;
            remaining -= chunk;
        }

        return true;
    }

private:
    explicit Qcow2Disk(std::filesystem::path path) : m_Path(std::move(path)) {}

    bool Initialize(std::string* outError) {
        m_File.open(m_Path, std::ios::binary);
        if (!m_File.is_open()) {
            if (outError) *outError = "Failed to open QCOW2 file: " + m_Path.string();
            return false;
        }

        Qcow2Header hdr{};
        m_File.seekg(0, std::ios::beg);
        m_File.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));

        if (SwapBe32(hdr.magic) != QCOW2_MAGIC) {
            if (outError) *outError = "Invalid QCOW2 magic signature (0x514649fb expected)";
            return false;
        }

        m_ClusterBits = SwapBe32(hdr.clusterBits);
        if (m_ClusterBits < 9 || m_ClusterBits > 21) {
            m_ClusterBits = 16; // Default 64KB
        }

        m_VirtualSize = SwapBe64(hdr.size);
        const uint32_t l1Size = SwapBe32(hdr.l1Size);
        const uint64_t l1Offset = SwapBe64(hdr.l1TableOffset);

        if (l1Size > 0 && l1Offset > 0) {
            m_L1Table.resize(l1Size);
            m_File.seekg(static_cast<std::streamoff>(l1Offset), std::ios::beg);
            m_File.read(reinterpret_cast<char*>(m_L1Table.data()), l1Size * sizeof(uint64_t));
            for (auto& entry : m_L1Table) {
                entry = SwapBe64(entry);
            }
        }

        m_Format = VirtualDiskFormat::Qcow2;
        return true;
    }

    std::filesystem::path m_Path;
    std::ifstream         m_File;
    VirtualDiskFormat     m_Format{VirtualDiskFormat::Qcow2};
    uint64_t              m_VirtualSize{0};
    uint32_t              m_ClusterBits{16};
    std::vector<uint64_t> m_L1Table;
    std::vector<uint64_t> m_CachedL2;
    uint64_t              m_CachedL2Offset{0xFFFFFFFFFFFFFFFFULL};
};

// ============================================================================
// Factory: OpenVirtualDisk
// ============================================================================
inline std::unique_ptr<IVirtualDisk> OpenVirtualDisk(const std::filesystem::path& path, std::string* outError = nullptr) {
    if (!std::filesystem::exists(path)) {
        if (outError) *outError = "File not found: " + path.string();
        return nullptr;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        if (outError) *outError = "Cannot open file: " + path.string();
        return nullptr;
    }

    // 1. Check QCOW2 (Big-Endian "QFI\xfb" = 0x514649fb)
    uint32_t magic32 = 0;
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(&magic32), 4);
    if (SwapBe32(magic32) == QCOW2_MAGIC) {
        f.close();
        return Qcow2Disk::Open(path, outError);
    }

    // 2. Check VMDK Sparse ("KDMV" = 0x564d444b)
    if (magic32 == VMDK_SPARSE_MAGIC) {
        f.close();
        return VmdkDisk::Open(path, outError);
    }

    // 3. Check VDI (0xBEDA107F at offset 0x40 / 64)
    uint32_t vdiSig = 0;
    f.seekg(0x40, std::ios::beg);
    f.read(reinterpret_cast<char*>(&vdiSig), 4);
    if (vdiSig == VDI_IMAGE_SIGNATURE) {
        f.close();
        return VdiDisk::Open(path, outError);
    }

    // 4. Check VHDX file signature ("vhdxfile" at 0)
    char sig[8]{};
    f.seekg(0, std::ios::beg);
    f.read(sig, 8);
    if (std::memcmp(sig, VHDX_FILE_IDENTIFIER, 8) == 0) {
        f.close();
        return VhdxDisk::Open(path, outError);
    }

    // 5. Check VHD (footer at end of file or offset 0)
    f.seekg(0, std::ios::end);
    const uint64_t sz = static_cast<uint64_t>(f.tellg());
    if (sz >= 512) {
        f.seekg(static_cast<std::streamoff>(sz - 512), std::ios::beg);
        f.read(sig, 8);
        if (std::memcmp(sig, VHD_COOKIE_FOOTER, 8) == 0) {
            f.close();
            return VhdDisk::Open(path, outError);
        }

        f.seekg(0, std::ios::beg);
        f.read(sig, 8);
        if (std::memcmp(sig, VHD_COOKIE_FOOTER, 8) == 0) {
            f.close();
            return VhdDisk::Open(path, outError);
        }
    }

    if (outError) *outError = "Unsupported virtual disk format or unrecognized signature.";
    return nullptr;
}

} // namespace titanhub::vhd
