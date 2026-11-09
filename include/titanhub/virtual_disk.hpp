#pragma once

#include "titanhub/types.hpp"
#include "titanhub/detector.hpp"
#include "titanhub/vhd_types.hpp"

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
    RawImage    = 5
};

inline const char* VirtualDiskFormatToString(VirtualDiskFormat fmt) noexcept {
    switch (fmt) {
        case VirtualDiskFormat::VhdFixed:    return "VHD (Fixed)";
        case VirtualDiskFormat::VhdDynamic:  return "VHD (Dynamic Sparse)";
        case VirtualDiskFormat::VhdxDynamic: return "VHDX (Dynamic Sparse)";
        case VirtualDiskFormat::VhdxFixed:   return "VHDX (Fixed)";
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

    // Check VHDX file signature ("vhdxfile" at 0)
    char sig[8]{};
    f.read(sig, 8);
    if (std::memcmp(sig, VHDX_FILE_IDENTIFIER, 8) == 0) {
        f.close();
        return VhdxDisk::Open(path, outError);
    }

    // Check VHD (footer at end of file or offset 0)
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
