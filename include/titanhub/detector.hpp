#pragma once

#include "titanhub/types.hpp"
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace titanhub {

class DiskFormatDetector {
public:
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

        // 1. Check Btrfs Signature: "_BHRfS_M" at offset 0x10040 (64 KiB + 64 bytes)
        if (bytesRead >= 0x10048) {
            if (std::memcmp(buffer.data() + 0x10040, "_BHRfS_M", 8) == 0) {
                if (outLabel) {
                    char labelBuf[257]{0};
                    std::memcpy(labelBuf, buffer.data() + 0x1012F, 256);
                    *outLabel = labelBuf;
                    if (outLabel->empty()) *outLabel = "TITAN_BTRFS";
                }
                return FsFormat::Btrfs;
            }
        }

        // 2. Check ReFS Signature: "ReFS" at offset 0x03
        if (std::memcmp(buffer.data() + 3, "ReFS", 4) == 0) {
            if (outLabel) *outLabel = "TITAN_REFS";
            return FsFormat::ReFS;
        }

        // 3. Check NTFS Signature: "NTFS    " at offset 0x03
        if (std::memcmp(buffer.data() + 3, "NTFS    ", 8) == 0) {
            if (outLabel) *outLabel = "TITAN_NTFS";
            return FsFormat::NTFS;
        }

        // 4. Check ZFS VDEV Label / Uberblock Magic
        // ZFS labels are located at the start and end of vdevs (0x00 - 0x80000)
        // Uberblock magic is 0x00bab10c (little endian) or 0x0cb1ba00 (big endian)
        for (size_t off = 0; off + 8 <= bytesRead; off += 512) {
            uint64_t val = *reinterpret_cast<const uint64_t*>(buffer.data() + off);
            if (val == 0x00bab10cULL || val == 0x0cb1ba0000000000ULL ||
                std::memcmp(buffer.data() + off, "zfs_pool", 8) == 0 ||
                std::memcmp(buffer.data() + off, "spa_version", 11) == 0) {
                if (outLabel) *outLabel = "TITAN_ZFS";
                return FsFormat::ZFS;
            }
        }

        return FsFormat::Unknown;
    }
};

} // namespace titanhub
