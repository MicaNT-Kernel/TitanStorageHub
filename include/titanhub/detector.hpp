#pragma once

#include "titanhub/types.hpp"
#include <fstream>
#include <vector>
#include <span>
#include <cstring>
#include <cstdint>
#include <string>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace titanhub {

class DiskFormatDetector {
public:
    static FsFormat DetectFormatFromBuffer(std::span<const uint8_t> buffer, std::string* outLabel = nullptr) {
        if (outLabel) outLabel->clear();
        const size_t bytesRead = buffer.size();

        if (bytesRead < 512) {
            return FsFormat::Unknown;
        }

        // Helper to trim trailing spaces and nulls
        auto cleanLabel = [](const char* src, size_t maxLen) -> std::string {
            size_t len = 0;
            while (len < maxLen && src[len] != '\0') ++len;
            while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\0')) --len;
            return std::string(src, len);
        };

        // 1. Check BitLocker signature: "-FVE-FS-" at offset 0x03
        if (std::memcmp(buffer.data() + 3, "-FVE-FS-", 8) == 0) {
            if (outLabel) *outLabel = "BitLocker Volume";
            return FsFormat::BitLocker;
        }

        // 2. Check ReFS Signature: "ReFS" at offset 0x03
        if (std::memcmp(buffer.data() + 3, "ReFS", 4) == 0) {
            if (outLabel) *outLabel = "Resilient Volume";
            return FsFormat::ReFS;
        }

        // 3. Check NTFS Signature: "NTFS    " at offset 0x03
        if (std::memcmp(buffer.data() + 3, "NTFS    ", 8) == 0) {
            if (outLabel) *outLabel = "Local Disk";
            return FsFormat::NTFS;
        }

        // 4. Check exFAT Signature: "EXFAT   " at offset 0x03
        if (std::memcmp(buffer.data() + 3, "EXFAT   ", 8) == 0) {
            if (outLabel) *outLabel = "EXFAT_VOLUME";
            return FsFormat::exFAT;
        }

        // 5. Check XFS Signature: "XFSB" at offset 0x00
        if (std::memcmp(buffer.data(), "XFSB", 4) == 0) {
            if (outLabel) {
                if (bytesRead >= 120) {
                    *outLabel = cleanLabel(reinterpret_cast<const char*>(buffer.data() + 108), 12);
                }
                if (outLabel->empty()) *outLabel = "XFS_VOLUME";
            }
            return FsFormat::XFS;
        }

        // 6. Check APFS Container Superblock: "NXSB" at offset 0x20
        if (bytesRead >= 36 && std::memcmp(buffer.data() + 0x20, "NXSB", 4) == 0) {
            if (outLabel) *outLabel = "APFS_CONTAINER";
            return FsFormat::APFS;
        }

        // 7. Check FAT32 Signature: "FAT32   " at offset 0x52
        if (bytesRead >= 90 && std::memcmp(buffer.data() + 0x52, "FAT32   ", 8) == 0) {
            if (outLabel) {
                *outLabel = cleanLabel(reinterpret_cast<const char*>(buffer.data() + 0x47), 11);
                if (outLabel->empty()) *outLabel = "FAT32_VOLUME";
            }
            return FsFormat::FAT32;
        }

        // 8. Check FAT16 / FAT12 Signature: "FAT16   " or "FAT12   " at offset 0x36
        if (bytesRead >= 62 && (std::memcmp(buffer.data() + 0x36, "FAT16   ", 8) == 0 ||
                                std::memcmp(buffer.data() + 0x36, "FAT12   ", 8) == 0)) {
            if (outLabel) {
                *outLabel = cleanLabel(reinterpret_cast<const char*>(buffer.data() + 0x2B), 11);
                if (outLabel->empty()) *outLabel = "FAT_VOLUME";
            }
            return FsFormat::FAT16;
        }

        // 9. Check F2FS Superblock Magic: 0xF2F52010 (LE) at offset 1024 (0x400)
        if (bytesRead >= 0x404) {
            uint32_t f2fsMagic = 0;
            std::memcpy(&f2fsMagic, buffer.data() + 0x400, sizeof(f2fsMagic));
            if (f2fsMagic == 0xF2F52010U) {
                if (outLabel) *outLabel = "F2FS_VOLUME";
                return FsFormat::F2FS;
            }
        }

        // 10. Check HFS+ Volume Header Magic: "H+" or "HX" at offset 1024 (0x400)
        if (bytesRead >= 0x404 && buffer[0x400] == 'H' &&
            (buffer[0x401] == '+' || buffer[0x401] == 'X')) {
            if (outLabel) *outLabel = "HFSPLUS_VOLUME";
            return FsFormat::HFSPlus;
        }

        // 11. Check Ext2/Ext3/Ext4 Superblock Magic: 0xEF53 at offset 1080 (0x438)
        if (bytesRead >= 0x440) {
            uint16_t extMagic = 0;
            std::memcpy(&extMagic, buffer.data() + 0x438, sizeof(extMagic));
            if (extMagic == 0xEF53U) {
                if (outLabel) {
                    if (bytesRead >= 0x488) {
                        *outLabel = cleanLabel(reinterpret_cast<const char*>(buffer.data() + 0x478), 16);
                    }
                    if (outLabel->empty()) *outLabel = "EXT_VOLUME";
                }

                // Differentiate ext4 vs ext3 vs ext2 via feature flags
                if (bytesRead >= 0x464) {
                    uint32_t incompatFeatures = 0;
                    std::memcpy(&incompatFeatures, buffer.data() + 0x460, sizeof(incompatFeatures));
                    if (incompatFeatures & 0x0040 /* EXT4_FEATURE_INCOMPAT_EXTENTS */) {
                        return FsFormat::Ext4;
                    }
                    uint32_t compatFeatures = 0;
                    std::memcpy(&compatFeatures, buffer.data() + 0x45C, sizeof(compatFeatures));
                    if (compatFeatures & 0x0004 /* EXT3_FEATURE_COMPAT_HAS_JOURNAL */) {
                        return FsFormat::Ext3;
                    }
                }
                return FsFormat::Ext2;
            }
        }

        // 12. Check Linux Swap Signature at offset 4086 (0xFF6)
        if (bytesRead >= 4096) {
            if (std::memcmp(buffer.data() + 4086, "SWAPSPACE2", 10) == 0 ||
                std::memcmp(buffer.data() + 4086, "SWAP-SPACE", 10) == 0) {
                if (outLabel) *outLabel = "SWAP";
                return FsFormat::LinuxSwap;
            }
        }

        // 13. Check ISO 9660 & UDF Volume Descriptors at sector 16 (0x8000)
        if (bytesRead >= 0x8028) {
            if (std::memcmp(buffer.data() + 0x8001, "CD001", 5) == 0) {
                if (outLabel) {
                    if (bytesRead >= 0x8048) {
                        *outLabel = cleanLabel(reinterpret_cast<const char*>(buffer.data() + 0x8028), 32);
                    }
                    if (outLabel->empty()) *outLabel = "ISO_IMAGE";
                }
                return FsFormat::ISO9660;
            }
            if (std::memcmp(buffer.data() + 0x8001, "BEA01", 5) == 0 ||
                std::memcmp(buffer.data() + 0x8001, "NSR02", 5) == 0 ||
                std::memcmp(buffer.data() + 0x8001, "NSR03", 5) == 0) {
                if (outLabel) *outLabel = "UDF_VOLUME";
                return FsFormat::UDF;
            }
        }
        if (bytesRead >= 0x9006) {
            if (std::memcmp(buffer.data() + 0x9001, "NSR02", 5) == 0 ||
                std::memcmp(buffer.data() + 0x9001, "NSR03", 5) == 0) {
                if (outLabel) *outLabel = "UDF_VOLUME";
                return FsFormat::UDF;
            }
        }

        // 14. Check Btrfs Signature: "_BHRfS_M" at offset 0x10040 (64 KiB + 64 bytes)
        if (bytesRead >= 0x10048) {
            if (std::memcmp(buffer.data() + 0x10040, "_BHRfS_M", 8) == 0) {
                if (outLabel) {
                    char labelBuf[257]{0};
                    std::memcpy(labelBuf, buffer.data() + 0x1012F, 256);
                    *outLabel = cleanLabel(labelBuf, 256);
                    if (outLabel->empty()) *outLabel = "Btrfs";
                }
                return FsFormat::Btrfs;
            }
        }

        // 15. Check ZFS VDEV Label / Uberblock Magic
        for (size_t off = 0; off + 8 <= bytesRead; off += 512) {
            uint64_t val = *reinterpret_cast<const uint64_t*>(buffer.data() + off);
            if (val == 0x00bab10cULL || val == 0x0cb1ba0000000000ULL ||
                std::memcmp(buffer.data() + off, "zfs_pool", 8) == 0 ||
                std::memcmp(buffer.data() + off, "spa_version", 11) == 0) {
                if (outLabel) *outLabel = "ZFS Pool";
                return FsFormat::ZFS;
            }
        }

        return FsFormat::Unknown;
    }

    static FsFormat DetectFormat(const std::string& path, std::string* outLabel = nullptr, uint64_t* outTotalBytes = nullptr) {
        if (outLabel) outLabel->clear();
        if (outTotalBytes) *outTotalBytes = 0;

        std::vector<uint8_t> buffer(131072, 0); // 128 KiB buffer
        size_t bytesRead = 0;

#ifdef _WIN32
        HANDLE hFile = CreateFileA(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
            nullptr
        );
        if (hFile == INVALID_HANDLE_VALUE) {
            return FsFormat::Unknown;
        }

        DWORD readBytes = 0;
        if (ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &readBytes, nullptr)) {
            bytesRead = readBytes;
        }

        if (outTotalBytes) {
            LARGE_INTEGER liSize;
            if (GetFileSizeEx(hFile, &liSize)) {
                *outTotalBytes = static_cast<uint64_t>(liSize.QuadPart);
            }
        }
        CloseHandle(hFile);
#else
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return FsFormat::Unknown;

        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        bytesRead = file.gcount();

        if (outTotalBytes) {
            file.seekg(0, std::ios::end);
            *outTotalBytes = static_cast<uint64_t>(file.tellg());
        }
#endif

        if (bytesRead < 512) {
            return FsFormat::Unknown;
        }

        buffer.resize(bytesRead);
        return DetectFormatFromBuffer(buffer, outLabel);
    }
};

} // namespace titanhub
