#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <chrono>

namespace titanhub {

enum class FsFormat : uint32_t {
    Unknown   = 0,
    ZFS       = 1,
    ReFS      = 2,
    NTFS      = 3,
    Btrfs     = 4,
    FAT32     = 5,
    exFAT     = 6,
    Ext4      = 7,
    Ext3      = 8,
    Ext2      = 9,
    XFS       = 10,
    F2FS      = 11,
    APFS      = 12,
    HFSPlus   = 13,
    ISO9660   = 14,
    UDF       = 15,
    FAT16     = 16,
    BitLocker = 17,
    LinuxSwap = 18
};

inline const char* FsFormatToString(FsFormat fmt) noexcept {
    switch (fmt) {
        case FsFormat::ZFS:       return "ZFS";
        case FsFormat::ReFS:      return "ReFS v3.12";
        case FsFormat::NTFS:      return "NTFS";
        case FsFormat::Btrfs:     return "Btrfs";
        case FsFormat::FAT32:     return "FAT32";
        case FsFormat::exFAT:     return "exFAT";
        case FsFormat::Ext4:      return "ext4";
        case FsFormat::Ext3:      return "ext3";
        case FsFormat::Ext2:      return "ext2";
        case FsFormat::XFS:       return "XFS";
        case FsFormat::F2FS:      return "F2FS";
        case FsFormat::APFS:      return "APFS";
        case FsFormat::HFSPlus:   return "HFS+";
        case FsFormat::ISO9660:   return "ISO 9660";
        case FsFormat::UDF:       return "UDF";
        case FsFormat::FAT16:     return "FAT16";
        case FsFormat::BitLocker: return "BitLocker";
        case FsFormat::LinuxSwap: return "Linux Swap";
        default:                  return "Unknown";
    }
}

inline const wchar_t* FsFormatToWString(FsFormat fmt) noexcept {
    switch (fmt) {
        case FsFormat::ZFS:       return L"ZFS";
        case FsFormat::ReFS:      return L"ReFS v3.12";
        case FsFormat::NTFS:      return L"NTFS";
        case FsFormat::Btrfs:     return L"Btrfs";
        case FsFormat::FAT32:     return L"FAT32";
        case FsFormat::exFAT:     return L"exFAT";
        case FsFormat::Ext4:      return L"ext4";
        case FsFormat::Ext3:      return L"ext3";
        case FsFormat::Ext2:      return L"ext2";
        case FsFormat::XFS:       return L"XFS";
        case FsFormat::F2FS:      return L"F2FS";
        case FsFormat::APFS:      return L"APFS";
        case FsFormat::HFSPlus:   return L"HFS+";
        case FsFormat::ISO9660:   return L"ISO 9660";
        case FsFormat::UDF:       return L"UDF";
        case FsFormat::FAT16:     return L"FAT16";
        case FsFormat::BitLocker: return L"BitLocker";
        case FsFormat::LinuxSwap: return L"Linux Swap";
        default:                  return L"Unknown";
    }
}

inline FsFormat ParseFsFormatString(std::string_view name) noexcept {
    auto iequals = [](std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            char ca = (a[i] >= 'a' && a[i] <= 'z') ? static_cast<char>(a[i] - 32) : a[i];
            char cb = (b[i] >= 'a' && b[i] <= 'z') ? static_cast<char>(b[i] - 32) : b[i];
            if (ca != cb) return false;
        }
        return true;
    };

    if (iequals(name, "NTFS")) return FsFormat::NTFS;
    if (iequals(name, "ReFS")) return FsFormat::ReFS;
    if (iequals(name, "FAT32")) return FsFormat::FAT32;
    if (iequals(name, "exFAT")) return FsFormat::exFAT;
    if (iequals(name, "BTRFS")) return FsFormat::Btrfs;
    if (iequals(name, "ZFS")) return FsFormat::ZFS;
    if (iequals(name, "FAT") || iequals(name, "FAT16") || iequals(name, "FAT12")) return FsFormat::FAT16;
    if (iequals(name, "CDFS") || iequals(name, "ISO9660") || iequals(name, "ISO-9660")) return FsFormat::ISO9660;
    if (iequals(name, "UDF")) return FsFormat::UDF;
    if (iequals(name, "ext4") || iequals(name, "EXT4")) return FsFormat::Ext4;
    if (iequals(name, "ext3") || iequals(name, "EXT3")) return FsFormat::Ext3;
    if (iequals(name, "ext2") || iequals(name, "EXT2")) return FsFormat::Ext2;
    if (iequals(name, "XFS")) return FsFormat::XFS;
    if (iequals(name, "F2FS")) return FsFormat::F2FS;
    if (iequals(name, "APFS")) return FsFormat::APFS;
    if (iequals(name, "HFS") || iequals(name, "HFS+")) return FsFormat::HFSPlus;
    return FsFormat::Unknown;
}

struct VolumeInfo {
    std::string path;
    FsFormat    format{FsFormat::Unknown};
    std::string label;
    std::string fileSystemName;     // OS reported string or signature e.g. "NTFS", "FAT32", "exFAT", "BTRFS", "ext4"
    std::string driveType;          // "Fixed SSD/HDD", "Removable USB / SD", "Optical / ISO", "Network Share", "Titan Virtual"
    uint64_t    totalBytes{0};
    uint64_t    usedBytes{0};
    uint32_t    sectorSize{4096};
    std::string mountedDriveLetter; // e.g. "C:" or "B:" or empty
    std::string activeSubvolume;    // e.g. "@home" or empty
    bool        isPhysical{false};
    bool        isMounted{false};
};

struct ScrubTelemetry {
    FsFormat    format{FsFormat::Unknown};
    std::string driveLetter;
    std::string devicePath;
    bool        isRunning{false};
    uint32_t    progressPercent{0};
    uint64_t    blocksScanned{0};
    uint64_t    errorsDetected{0};
    uint64_t    repairedCount{0};
    std::string lastRunTimestamp;
};

struct SubvolSnapshotEntry {
    FsFormat    format{FsFormat::Unknown};
    uint64_t    id{0};
    uint64_t    parentId{0};
    std::string name;
    bool        isSnapshot{false};
    uint64_t    generation{1};
};

struct ServiceTelemetry {
    std::string name;
    std::string serviceId; // "TitanZFS", "TitanReFS", "TitanNTFS", "TitanBtrfs"
    std::string pipeName;
    bool        isInstalled{false};
    bool        isRunning{false};
    uint32_t    mountCount{0};
};

} // namespace titanhub
