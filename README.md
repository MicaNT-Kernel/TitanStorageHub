# Titan Storage Hub: Unified Drive Control Center for Windows

[![TitanStorageHub CI](https://github.com/MicaNT-Kernel/TitanStorageHub/actions/workflows/ci.yml/badge.svg)](https://github.com/MicaNT-Kernel/TitanStorageHub/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-orange.svg)]()
[![Compliance Audited](https://img.shields.io/badge/Clean--Room-Sentinel%20Audited-brightgreen.svg)](CLEAN_ROOM_PROVENANCE.md)

![Titan Storage Hub](assets/titanstoragehub_hero.jpg)

**Titan Storage Hub** is the unified desktop control center for the **Titan Storage Suite** (`TitanZFS`, `TitanReFS`, `TitanNTFS`, and `TitanBtrfs`). Designed from first principles in modern ISO C++23 and pure Win32 dark-mode GUI architecture, Titan Storage Hub delivers centralized volume discovery, real-time bit-rot scrubber telemetry, subvolume snapshot browsing, filesystem formatting wizards, and background service management without kernel drivers or BSOD risks.

---

## Key Features

- **Unified Multi-Engine Control**: Direct Named Pipe IPC integration (`\\.\pipe\titanzfs_ipc`, `\\.\pipe\titanrefs_ipc`, `\\.\pipe\titanntfs_ipc`, and `\\.\pipe\titanbtrfs_ipc`) communicating with all Titan filesystem services simultaneously.
- **Automatic Filesystem Signature Detection (`DiskFormatDetector`)**: Instantly detects and categorizes disks and volume images using clean-room magic signatures:
  - **Btrfs**: `_BHRfS_M` at `0x10040` (64 KiB superblock offset)
  - **ReFS**: `"ReFS"` at byte offset `0x03`
  - **NTFS**: `"NTFS    "` at byte offset `0x03`
  - **ZFS**: Uberblock magic (`0x00bab10c`) & `zfs_pool` headers
- **Real-Time Bit-Rot Scrubber & Health Center**: Visual progress meters, live sector counters, and error recovery telemetry tracking automated background scrub operations across all mounted pools and volumes.
- **Subvolume & Snapshot Browser**: Inspect subvolume trees, snapshot hierarchies (`@`, `@home`, `@snapshots`), parent-child generations, and clone targets for Btrfs and ZFS datasets.
- **Partition & Format Wizard**: Quick-format disk images or physical drives to ZFS, ReFS v3.12, NTFS, or Btrfs with customized sector sizes and volume labels.
- **Windows Background Service Monitor**: Live status, start/stop controls, and pipe health diagnostics for `TitanZFS`, `TitanReFS`, `TitanNTFS`, and `TitanBtrfs` Windows NT services.
- **Modern Win32 Dark-Mode Architecture**: Left Navigation Sidebar layout (`980 x 640`), smooth Mica styling, GDI+ high-DPI hero graphics, and zero external GUI framework bloat.
- **System Tray Agent**: Background minimization, system notification area integration, and quick drive access menu.
- **Turnkey 1-Click Installer (`titanstoragehub_setup.exe`)**: Single-file installer bundling the desktop manager, hero artwork, and signed WinFsp runtime MSI using clean-room LZ4 PE overlay compression.
- **Clean-Room Compliance**: 100% written under 17 U.S.C. § 1201(f) & *Google v. Oracle* interoperability doctrines, audited by our automated Sentinel on every commit.

---

## Control Center Views

| View | Description |
| :--- | :--- |
| **Drives & Volumes** | Central dashboard listing all detected disks, partitions, mount drive letters, filesystem formats, and capacity usage. |
| **Scrubber & Health** | Real-time monitoring of automated scrubbing routines, scanned blocks, detected errors, and self-healed sectors. |
| **Subvolumes & Snapshots** | Hierarchical view of active subvolumes, CoW clones, and read-only backup snapshots. |
| **Format Wizard** | Turnkey volume preparation tool with filesystem format selection, sector alignment, and volume labeling. |
| **Services & System** | Windows NT Service Control Manager (SCM) probe for Titan background daemons and Named Pipe health. |

---

## Building from Source

### Prerequisites
- CMake 3.24+
- Modern C++23 Compiler (Clang 18+, MSVC 2022 v17.8+, or GCC 14+)
- Ninja build tool (recommended)

### Windows Build (Clang / Ninja)
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

### Run Test Battery
```bash
ctest --test-dir build --output-on-failure
# Or directly:
./build/titanstoragehub_tests.exe
```

### Run Clean-Room Sentinel Audit
```bash
node scripts/clean_room_sentinel.js --local
```

### Create Distribution ZIP
```bash
cpack --config build/CPackConfig.cmake -B build/dist
```

---

## Legal & Clean-Room Architecture
Titan Storage Hub is an independent clean-room implementation developed for functional interoperability under 17 U.S.C. § 1201(f) and SCOTUS fair use doctrine (*Google v. Oracle*). See [CLEAN_ROOM_PROVENANCE.md](CLEAN_ROOM_PROVENANCE.md) for complete compliance documentation.

---

## License
Licensed under the Apache License, Version 2.0.  
Copyright © 2026 Daniel Elliott <ssfdre38@gmail.com>.
