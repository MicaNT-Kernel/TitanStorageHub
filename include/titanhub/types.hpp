#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace titanhub {

enum class FsFormat : uint32_t {
    Unknown = 0,
    ZFS     = 1,
    ReFS    = 2,
    NTFS    = 3,
    Btrfs   = 4
};

inline const char* FsFormatToString(FsFormat fmt) noexcept {
    switch (fmt) {
        case FsFormat::ZFS:   return "ZFS";
        case FsFormat::ReFS:  return "ReFS v3.12";
        case FsFormat::NTFS:  return "NTFS";
        case FsFormat::Btrfs: return "Btrfs";
        default:              return "Unknown";
    }
}

inline const wchar_t* FsFormatToWString(FsFormat fmt) noexcept {
    switch (fmt) {
        case FsFormat::ZFS:   return L"ZFS";
        case FsFormat::ReFS:  return L"ReFS v3.12";
        case FsFormat::NTFS:  return L"NTFS";
        case FsFormat::Btrfs: return L"Btrfs";
        default:              return L"Unknown";
    }
}

struct VolumeInfo {
    std::string path;
    FsFormat    format{FsFormat::Unknown};
    std::string label;
    uint64_t    totalBytes{0};
    uint64_t    usedBytes{0};
    uint32_t    sectorSize{4096};
    std::string mountedDriveLetter; // e.g. "B:" or empty
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
