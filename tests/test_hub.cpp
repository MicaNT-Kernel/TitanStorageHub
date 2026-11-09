#include "titanhub/types.hpp"
#include "titanhub/detector.hpp"
#include "titanhub/compression.hpp"
#include "titanhub/ipc_manager.hpp"
#include "titanhub/virtual_disk.hpp"

#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <random>

namespace fs = std::filesystem;

static int g_failures = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_failures++; \
        } else { \
            std::cout << "[+] PASSED: " << msg << "\n"; \
        } \
    } while (0)

void TestFormatStrings() {
    std::cout << "\n=== TestFormatStrings ===\n";
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::ZFS)) == "ZFS", "ZFS name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::ReFS)) == "ReFS v3.12", "ReFS name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::NTFS)) == "NTFS", "NTFS name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::Btrfs)) == "Btrfs", "Btrfs name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::FAT32)) == "FAT32", "FAT32 name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::exFAT)) == "exFAT", "exFAT name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::Ext4)) == "ext4", "ext4 name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::Ext3)) == "ext3", "ext3 name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::Ext2)) == "ext2", "ext2 name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::XFS)) == "XFS", "XFS name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::F2FS)) == "F2FS", "F2FS name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::APFS)) == "APFS", "APFS name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::HFSPlus)) == "HFS+", "HFS+ name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::ISO9660)) == "ISO 9660", "ISO9660 name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::UDF)) == "UDF", "UDF name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::BitLocker)) == "BitLocker", "BitLocker name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::LinuxSwap)) == "Linux Swap", "Linux Swap name");
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::Unknown)) == "Unknown", "Unknown name");

    TEST_ASSERT(std::wstring(titanhub::FsFormatToWString(titanhub::FsFormat::ZFS)) == L"ZFS", "ZFS wname");
    TEST_ASSERT(std::wstring(titanhub::FsFormatToWString(titanhub::FsFormat::Btrfs)) == L"Btrfs", "Btrfs wname");
    TEST_ASSERT(std::wstring(titanhub::FsFormatToWString(titanhub::FsFormat::Ext4)) == L"ext4", "ext4 wname");
}

void TestParseFsFormatString() {
    std::cout << "\n=== TestParseFsFormatString ===\n";
    TEST_ASSERT(titanhub::ParseFsFormatString("NTFS") == titanhub::FsFormat::NTFS, "Parse NTFS");
    TEST_ASSERT(titanhub::ParseFsFormatString("ntfs") == titanhub::FsFormat::NTFS, "Parse ntfs lowercase");
    TEST_ASSERT(titanhub::ParseFsFormatString("ReFS") == titanhub::FsFormat::ReFS, "Parse ReFS");
    TEST_ASSERT(titanhub::ParseFsFormatString("FAT32") == titanhub::FsFormat::FAT32, "Parse FAT32");
    TEST_ASSERT(titanhub::ParseFsFormatString("exFAT") == titanhub::FsFormat::exFAT, "Parse exFAT");
    TEST_ASSERT(titanhub::ParseFsFormatString("BTRFS") == titanhub::FsFormat::Btrfs, "Parse BTRFS");
    TEST_ASSERT(titanhub::ParseFsFormatString("ZFS") == titanhub::FsFormat::ZFS, "Parse ZFS");
    TEST_ASSERT(titanhub::ParseFsFormatString("EXT4") == titanhub::FsFormat::Ext4, "Parse EXT4");
    TEST_ASSERT(titanhub::ParseFsFormatString("ext4") == titanhub::FsFormat::Ext4, "Parse ext4");
    TEST_ASSERT(titanhub::ParseFsFormatString("XFS") == titanhub::FsFormat::XFS, "Parse XFS");
    TEST_ASSERT(titanhub::ParseFsFormatString("CDFS") == titanhub::FsFormat::ISO9660, "Parse CDFS");
    TEST_ASSERT(titanhub::ParseFsFormatString("UDF") == titanhub::FsFormat::UDF, "Parse UDF");
    TEST_ASSERT(titanhub::ParseFsFormatString("RandomNonexistentFS") == titanhub::FsFormat::Unknown, "Parse Unknown");
}

void TestDiskFormatDetector() {
    std::cout << "\n=== TestDiskFormatDetector ===\n";
    fs::path tempDir = fs::temp_directory_path() / "titanhub_tests";
    fs::create_directories(tempDir);

    // 1. Test Btrfs
    {
        fs::path p = tempDir / "test_btrfs.img";
        std::vector<uint8_t> buf(131072, 0);
        std::memcpy(buf.data() + 0x10040, "_BHRfS_M", 8);
        const char* label = "TITAN_ROOT";
        std::memcpy(buf.data() + 0x1012F, label, std::strlen(label));

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        uint64_t outSize = 0;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel, &outSize);
        TEST_ASSERT(fmt == titanhub::FsFormat::Btrfs, "Btrfs format detection");
        TEST_ASSERT(outLabel == "TITAN_ROOT", "Btrfs label extraction");
        TEST_ASSERT(outSize == 131072, "Btrfs file size detection");
        fs::remove(p);
    }

    // 2. Test ReFS
    {
        fs::path p = tempDir / "test_refs.img";
        std::vector<uint8_t> buf(4096, 0);
        std::memcpy(buf.data() + 3, "ReFS", 4);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::ReFS, "ReFS format detection");
        TEST_ASSERT(outLabel == "Resilient Volume", "ReFS label");
        fs::remove(p);
    }

    // 3. Test NTFS
    {
        fs::path p = tempDir / "test_ntfs.img";
        std::vector<uint8_t> buf(4096, 0);
        std::memcpy(buf.data() + 3, "NTFS    ", 8);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::NTFS, "NTFS format detection");
        TEST_ASSERT(outLabel == "Local Disk", "NTFS label");
        fs::remove(p);
    }

    // 4. Test exFAT
    {
        fs::path p = tempDir / "test_exfat.img";
        std::vector<uint8_t> buf(4096, 0);
        std::memcpy(buf.data() + 3, "EXFAT   ", 8);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::exFAT, "exFAT format detection");
        TEST_ASSERT(outLabel == "EXFAT_VOLUME", "exFAT label");
        fs::remove(p);
    }

    // 5. Test FAT32
    {
        fs::path p = tempDir / "test_fat32.img";
        std::vector<uint8_t> buf(4096, 0);
        std::memcpy(buf.data() + 0x52, "FAT32   ", 8);
        std::memcpy(buf.data() + 0x47, "MY_USB_DISK", 11);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::FAT32, "FAT32 format detection");
        TEST_ASSERT(outLabel == "MY_USB_DISK", "FAT32 label");
        fs::remove(p);
    }

    // 6. Test Ext4
    {
        fs::path p = tempDir / "test_ext4.img";
        std::vector<uint8_t> buf(4096, 0);
        uint16_t magic = 0xEF53;
        std::memcpy(buf.data() + 0x438, &magic, 2);
        uint32_t incompat = 0x0040; // EXT4_FEATURE_INCOMPAT_EXTENTS
        std::memcpy(buf.data() + 0x460, &incompat, 4);
        std::memcpy(buf.data() + 0x478, "Ubuntu_Root", 11);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::Ext4, "Ext4 format detection");
        TEST_ASSERT(outLabel == "Ubuntu_Root", "Ext4 label");
        fs::remove(p);
    }

    // 7. Test XFS
    {
        fs::path p = tempDir / "test_xfs.img";
        std::vector<uint8_t> buf(4096, 0);
        std::memcpy(buf.data(), "XFSB", 4);
        std::memcpy(buf.data() + 108, "DataStore", 9);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::XFS, "XFS format detection");
        TEST_ASSERT(outLabel == "DataStore", "XFS label");
        fs::remove(p);
    }

    // 8. Test F2FS
    {
        fs::path p = tempDir / "test_f2fs.img";
        std::vector<uint8_t> buf(4096, 0);
        uint32_t magic = 0xF2F52010U;
        std::memcpy(buf.data() + 0x400, &magic, 4);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::F2FS, "F2FS format detection");
        fs::remove(p);
    }

    // 9. Test APFS
    {
        fs::path p = tempDir / "test_apfs.img";
        std::vector<uint8_t> buf(4096, 0);
        std::memcpy(buf.data() + 0x20, "NXSB", 4);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::APFS, "APFS format detection");
        fs::remove(p);
    }

    // 10. Test ISO 9660
    {
        fs::path p = tempDir / "test_iso.iso";
        std::vector<uint8_t> buf(65536, 0);
        std::memcpy(buf.data() + 0x8001, "CD001", 5);
        std::memcpy(buf.data() + 0x8028, "LINUX_INSTALLER", 15);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::ISO9660, "ISO 9660 format detection");
        TEST_ASSERT(outLabel == "LINUX_INSTALLER", "ISO 9660 label");
        fs::remove(p);
    }

    // 11. Test BitLocker
    {
        fs::path p = tempDir / "test_bitlocker.img";
        std::vector<uint8_t> buf(4096, 0);
        std::memcpy(buf.data() + 3, "-FVE-FS-", 8);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::BitLocker, "BitLocker format detection");
        fs::remove(p);
    }

    // 12. Test ZFS
    {
        fs::path p = tempDir / "test_zfs.img";
        std::vector<uint8_t> buf(4096, 0);
        std::memcpy(buf.data() + 512, "zfs_pool", 8);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel);
        TEST_ASSERT(fmt == titanhub::FsFormat::ZFS, "ZFS format detection");
        TEST_ASSERT(outLabel == "ZFS Pool", "ZFS label");
        fs::remove(p);
    }

    // 13. Test Unknown & Missing File
    {
        fs::path p = tempDir / "test_unknown.img";
        std::vector<uint8_t> buf(4096, 0xAA);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string());
        TEST_ASSERT(fmt == titanhub::FsFormat::Unknown, "Unknown format detection");
        fs::remove(p);

        auto fmtMissing = titanhub::DiskFormatDetector::DetectFormat((tempDir / "missing.img").string());
        TEST_ASSERT(fmtMissing == titanhub::FsFormat::Unknown, "Missing file returns Unknown");
    }

    fs::remove_all(tempDir);
}

void TestCompressionRLE() {
    std::cout << "\n=== TestCompressionRLE ===\n";
    // 1. All zero buffer
    {
        std::vector<uint8_t> zeros(65536, 0);
        std::vector<uint8_t> comp;
        size_t cSize = titanhub::CompressionEngine::CompressRLE(zeros, comp);
        TEST_ASSERT(cSize == 9, "RLE all-zero size is 9 bytes");
        TEST_ASSERT(comp[0] == 0xFF, "RLE all-zero flag header");

        std::vector<uint8_t> decomp(65536, 0xCC);
        bool ok = titanhub::CompressionEngine::DecompressRLE(comp, decomp);
        TEST_ASSERT(ok, "RLE decompress success");
        TEST_ASSERT(titanhub::CompressionEngine::IsAllZero(decomp), "RLE decompressed is all zero");
    }

    // 2. Pattern data
    {
        std::vector<uint8_t> src;
        for (int i = 0; i < 50; ++i) src.push_back('A');
        for (int i = 0; i < 80; ++i) src.push_back('B');
        for (int i = 0; i < 10; ++i) src.push_back('C');

        std::vector<uint8_t> comp;
        size_t cSize = titanhub::CompressionEngine::CompressRLE(src, comp);
        TEST_ASSERT(cSize == 6, "RLE pattern compressed to 6 bytes");

        std::vector<uint8_t> decomp(src.size(), 0);
        bool ok = titanhub::CompressionEngine::DecompressRLE(comp, decomp);
        TEST_ASSERT(ok, "RLE pattern decompressed ok");
        TEST_ASSERT(decomp == src, "RLE pattern content match");
    }

    // 3. Empty buffer
    {
        std::vector<uint8_t> src;
        std::vector<uint8_t> comp;
        size_t cSize = titanhub::CompressionEngine::CompressRLE(src, comp);
        TEST_ASSERT(cSize == 0, "RLE empty input produces 0 output");
    }
}

void TestCompressionLZ4() {
    std::cout << "\n=== TestCompressionLZ4 ===\n";
    // 1. Highly compressible repetitive text
    {
        std::string phrase = "TitanStorageHub Unified Control Center for ZFS, ReFS, NTFS, and Btrfs on Windows. ";
        std::vector<uint8_t> src;
        for (int i = 0; i < 100; ++i) {
            src.insert(src.end(), phrase.begin(), phrase.end());
        }

        std::vector<uint8_t> comp;
        size_t cSize = titanhub::CompressionEngine::CompressLZ4(src, comp);
        TEST_ASSERT(cSize > 0 && cSize < src.size(), "LZ4 compressed size smaller than source");

        std::vector<uint8_t> decomp(src.size(), 0);
        bool ok = titanhub::CompressionEngine::DecompressLZ4(comp, decomp);
        TEST_ASSERT(ok, "LZ4 decompression succeeded");
        TEST_ASSERT(decomp == src, "LZ4 decompressed data equals original");
    }

    // 2. Pseudo-random data
    {
        std::vector<uint8_t> src(16384);
        std::mt19937 rng(42);
        for (auto& b : src) b = static_cast<uint8_t>(rng() & 0xFF);

        std::vector<uint8_t> comp;
        size_t cSize = titanhub::CompressionEngine::CompressLZ4(src, comp);
        TEST_ASSERT(cSize > 0, "LZ4 compressed random data");

        std::vector<uint8_t> decomp(src.size(), 0);
        bool ok = titanhub::CompressionEngine::DecompressLZ4(comp, decomp);
        TEST_ASSERT(ok, "LZ4 random decompress succeeded");
        TEST_ASSERT(decomp == src, "LZ4 random data matches");
    }
}

void TestTypesAndTelemetry() {
    std::cout << "\n=== TestTypesAndTelemetry ===\n";

    titanhub::VolumeInfo vol;
    vol.format = titanhub::FsFormat::Btrfs;
    vol.label = "DATA_POOL";
    vol.driveType = "Fixed SSD/HDD";
    vol.totalBytes = 100ULL * 1024 * 1024 * 1024;
    vol.usedBytes = 40ULL * 1024 * 1024 * 1024;
    vol.mountedDriveLetter = "T:";
    vol.activeSubvolume = "@home";
    vol.isMounted = true;
    vol.isPhysical = true;

    TEST_ASSERT(vol.format == titanhub::FsFormat::Btrfs, "VolumeInfo format");
    TEST_ASSERT(vol.driveType == "Fixed SSD/HDD", "VolumeInfo driveType");
    TEST_ASSERT(vol.mountedDriveLetter == "T:", "VolumeInfo drive letter");
    TEST_ASSERT(vol.activeSubvolume == "@home", "VolumeInfo subvolume");
    TEST_ASSERT(vol.isMounted && vol.isPhysical, "VolumeInfo flags");
}

void TestVirtualDiskVhdFixed() {
    std::cout << "\n=== TestVirtualDiskVhdFixed ===\n";
    fs::path tempDir = fs::temp_directory_path() / "titanhub_tests";
    fs::create_directories(tempDir);
    fs::path vhdPath = tempDir / "test_fixed.vhd";

    constexpr uint64_t VIRTUAL_SIZE = 1048576; // 1 MB
    std::vector<uint8_t> diskData(VIRTUAL_SIZE + 512, 0);

    // 1. Create MBR partition table at sector 0
    diskData[510] = 0x55;
    diskData[511] = 0xAA;
    // Partition 1: Active, starts at sector 128 (offset 65536 = 64KB), length = 1024 sectors
    diskData[446 + 0] = 0x80; // Active
    diskData[446 + 4] = 0x83; // Linux native partition
    uint32_t startLba = 128;
    uint32_t sectorCount = 1024;
    std::memcpy(&diskData[446 + 8], &startLba, 4);
    std::memcpy(&diskData[446 + 12], &sectorCount, 4);

    // 2. Put Btrfs Superblock inside Partition 1 at offset 0x10040 (65536 + 65600 = 131136)
    const uint64_t btrfsOffset = (128 * 512) + 0x10040;
    std::memcpy(&diskData[btrfsOffset], "_BHRfS_M", 8);
    const char* label = "VHD_BTRFS";
    std::memcpy(&diskData[(128 * 512) + 0x1012F], label, std::strlen(label));

    // 3. Write VhdFooter at end of file (offset VIRTUAL_SIZE)
    auto* footer = reinterpret_cast<titanhub::vhd::VhdFooter*>(&diskData[VIRTUAL_SIZE]);
    std::memcpy(footer->cookie, titanhub::vhd::VHD_COOKIE_FOOTER, 8);
    footer->features = titanhub::vhd::SwapBe32(0x00000002);
    footer->fileFormatVersion = titanhub::vhd::SwapBe32(0x00010000);
    footer->dataOffset = 0xFFFFFFFFFFFFFFFFULL; // Fixed disk
    footer->currentSize = titanhub::vhd::SwapBe64(VIRTUAL_SIZE);
    footer->originalSize = titanhub::vhd::SwapBe64(VIRTUAL_SIZE);
    footer->diskType = titanhub::vhd::SwapBe32(static_cast<uint32_t>(titanhub::vhd::VhdDiskType::Fixed));

    std::ofstream out(vhdPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(diskData.data()), diskData.size());
    out.close();

    // 4. Open and Verify via IVirtualDisk
    std::string err;
    auto vdisk = titanhub::vhd::OpenVirtualDisk(vhdPath, &err);
    TEST_ASSERT(vdisk != nullptr, "OpenVirtualDisk fixed VHD succeeded");
    if (vdisk) {
        TEST_ASSERT(vdisk->GetFormat() == titanhub::vhd::VirtualDiskFormat::VhdFixed, "Format is VhdFixed");
        TEST_ASSERT(vdisk->GetVirtualSize() == VIRTUAL_SIZE, "Virtual size is 1MB");
        TEST_ASSERT(vdisk->GetSectorSize() == 512, "Sector size is 512");

        // Verify partition discovery & filesystem probe
        auto partitions = vdisk->DiscoverPartitions();
        TEST_ASSERT(partitions.size() == 1, "1 partition discovered in VHD");
        if (!partitions.empty()) {
            TEST_ASSERT(partitions[0].startSector == 128, "Partition starts at sector 128");
            TEST_ASSERT(partitions[0].detectedFs == titanhub::FsFormat::Btrfs, "Autodetected Btrfs inside VHD");
            TEST_ASSERT(partitions[0].fsLabel == "VHD_BTRFS", "Extracted Btrfs label inside VHD");
        }
    }

    vdisk.reset();
    fs::remove(vhdPath);
}

void TestVirtualDiskVhdDynamic() {
    std::cout << "\n=== TestVirtualDiskVhdDynamic ===\n";
    fs::path tempDir = fs::temp_directory_path() / "titanhub_tests";
    fs::create_directories(tempDir);
    fs::path vhdPath = tempDir / "test_dynamic.vhd";

    constexpr uint64_t VIRTUAL_SIZE = 4194304; // 4 MB (2 blocks of 2MB)
    constexpr uint32_t BLOCK_SIZE = 2097152;  // 2 MB

    // Layout:
    // [0..511]: Footer
    // [512..1535]: Dynamic Header (1024 bytes)
    // [1536..2047]: BAT Table (512 bytes = 128 uint32 entries, we use 2)
    // [2048..2559]: Block 0 Sector Bitmap (512 bytes)
    // [2560..2560 + BLOCK_SIZE - 1]: Block 0 Payload Data
    // [end]: Duplicate Footer
    const uint64_t block0SectorOffset = 4; // sector 4 = offset 2048
    const uint64_t totalFileSize = 2560 + BLOCK_SIZE + 512;
    std::vector<uint8_t> fileBuf(totalFileSize, 0);

    // 1. Footer at 0
    auto* footer = reinterpret_cast<titanhub::vhd::VhdFooter*>(&fileBuf[0]);
    std::memcpy(footer->cookie, titanhub::vhd::VHD_COOKIE_FOOTER, 8);
    footer->features = titanhub::vhd::SwapBe32(0x00000002);
    footer->fileFormatVersion = titanhub::vhd::SwapBe32(0x00010000);
    footer->dataOffset = titanhub::vhd::SwapBe64(512); // Points to Dynamic Header
    footer->currentSize = titanhub::vhd::SwapBe64(VIRTUAL_SIZE);
    footer->originalSize = titanhub::vhd::SwapBe64(VIRTUAL_SIZE);
    footer->diskType = titanhub::vhd::SwapBe32(static_cast<uint32_t>(titanhub::vhd::VhdDiskType::Dynamic));

    // 2. Dynamic Header at 512
    auto* dyn = reinterpret_cast<titanhub::vhd::VhdDynamicHeader*>(&fileBuf[512]);
    std::memcpy(dyn->cookie, titanhub::vhd::VHD_COOKIE_DYNAMIC, 8);
    dyn->dataOffset = 0xFFFFFFFFFFFFFFFFULL;
    dyn->tableOffset = titanhub::vhd::SwapBe64(1536); // BAT offset
    dyn->headerVersion = titanhub::vhd::SwapBe32(0x00010000);
    dyn->maxTableEntries = titanhub::vhd::SwapBe32(2);
    dyn->blockSize = titanhub::vhd::SwapBe32(BLOCK_SIZE);

    // 3. BAT Table at 1536
    uint32_t* bat = reinterpret_cast<uint32_t*>(&fileBuf[1536]);
    bat[0] = titanhub::vhd::SwapBe32(static_cast<uint32_t>(block0SectorOffset)); // Block 0 allocated at sector 4
    bat[1] = 0xFFFFFFFF; // Block 1 unallocated / sparse

    // 4. Block 0 Payload at 2560: Write a signature string
    const char* testMsg = "TITAN_DYNAMIC_VHD_BLOCK0_DATA";
    std::memcpy(&fileBuf[2560], testMsg, std::strlen(testMsg));

    // 5. Duplicate footer at end
    std::memcpy(&fileBuf[2560 + BLOCK_SIZE], footer, 512);

    std::ofstream out(vhdPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(fileBuf.data()), fileBuf.size());
    out.close();

    // Verify
    std::string err;
    auto vdisk = titanhub::vhd::OpenVirtualDisk(vhdPath, &err);
    TEST_ASSERT(vdisk != nullptr, "OpenVirtualDisk dynamic VHD succeeded");
    if (vdisk) {
        TEST_ASSERT(vdisk->GetFormat() == titanhub::vhd::VirtualDiskFormat::VhdDynamic, "Format is VhdDynamic");
        TEST_ASSERT(vdisk->GetVirtualSize() == VIRTUAL_SIZE, "Virtual size is 4MB");

        // Read from Block 0 (allocated)
        char readBuf[64]{};
        bool readOk = vdisk->ReadBytes(0, 30, readBuf);
        TEST_ASSERT(readOk, "Read allocated block succeeded");
        TEST_ASSERT(std::string(readBuf) == testMsg, "Block 0 data matches expected pattern");

        // Read from Block 1 (unallocated / sparse)
        std::vector<uint8_t> sparseBuf(1024, 0xAA);
        readOk = vdisk->ReadBytes(BLOCK_SIZE, sparseBuf.size(), sparseBuf.data());
        TEST_ASSERT(readOk, "Read unallocated sparse block succeeded");
        bool allZero = true;
        for (uint8_t b : sparseBuf) {
            if (b != 0) { allZero = false; break; }
        }
        TEST_ASSERT(allZero, "Sparse unallocated block returns pure zeros");
    }

    vdisk.reset();
    fs::remove(vhdPath);
}

void TestVirtualDiskVhdx() {
    std::cout << "\n=== TestVirtualDiskVhdx ===\n";
    fs::path tempDir = fs::temp_directory_path() / "titanhub_tests";
    fs::create_directories(tempDir);
    fs::path vhdxPath = tempDir / "test_modern.vhdx";

    // VHDX Minimal layout:
    // Offset 0: VhdxFileIdentifier ("vhdxfile")
    // Offset 0x30000 (192KB): Region Table Header ("regi")
    // Region 0: BAT at 0x100000 (1MB)
    // Region 1: Metadata at 0x200000 (2MB)
    // BAT Entry 0: points to payload block at 0x300000 (3MB)
    constexpr uint64_t FILE_SIZE = 0x400000; // 4MB
    constexpr uint64_t VIRTUAL_SIZE = 16777216; // 16MB virtual disk
    constexpr uint32_t BLOCK_SIZE = 1048576; // 1MB block size
    constexpr uint32_t SECTOR_SIZE = 512;

    std::vector<uint8_t> vhdxData(FILE_SIZE, 0);

    // 1. Signature at offset 0
    std::memcpy(&vhdxData[0], titanhub::vhd::VHDX_FILE_IDENTIFIER, 8);

    // 2. Region Table Header at 0x30000
    auto* regHeader = reinterpret_cast<titanhub::vhd::VhdxRegionTableHeader*>(&vhdxData[0x30000]);
    std::memcpy(regHeader->signature, titanhub::vhd::VHDX_REGION_SIGNATURE, 4);
    regHeader->entryCount = 2;

    // Entry 0: BAT
    std::memcpy(regHeader->entries[0].guid, titanhub::vhd::VHDX_BAT_GUID, 16);
    regHeader->entries[0].fileOffset = 0x100000; // 1MB
    regHeader->entries[0].length = 65536;

    // Entry 1: Metadata
    std::memcpy(regHeader->entries[1].guid, titanhub::vhd::VHDX_METADATA_GUID, 16);
    regHeader->entries[1].fileOffset = 0x200000; // 2MB
    regHeader->entries[1].length = 65536;

    // 3. Metadata Region at 0x200000
    auto* metaHeader = reinterpret_cast<titanhub::vhd::VhdxMetadataTableHeader*>(&vhdxData[0x200000]);
    std::memcpy(metaHeader->signature, titanhub::vhd::VHDX_METADATA_SIGNATURE, 8);
    metaHeader->entryCount = 3;

    auto* metaEntries = reinterpret_cast<titanhub::vhd::VhdxMetadataEntry*>(&vhdxData[0x200000 + sizeof(titanhub::vhd::VhdxMetadataTableHeader)]);
    
    // Metadata Item 0: Virtual Disk Size (uint64_t)
    std::memcpy(metaEntries[0].itemId, titanhub::vhd::VHDX_META_DISK_SIZE_GUID, 16);
    metaEntries[0].offset = 4096;
    metaEntries[0].length = 8;
    *reinterpret_cast<uint64_t*>(&vhdxData[0x200000 + 4096]) = VIRTUAL_SIZE;

    // Metadata Item 1: Sector Size (uint32_t)
    std::memcpy(metaEntries[1].itemId, titanhub::vhd::VHDX_META_SECTOR_SIZE_GUID, 16);
    metaEntries[1].offset = 4112;
    metaEntries[1].length = 4;
    *reinterpret_cast<uint32_t*>(&vhdxData[0x200000 + 4112]) = SECTOR_SIZE;

    // Metadata Item 2: File Params (BlockSize uint32_t)
    std::memcpy(metaEntries[2].itemId, titanhub::vhd::VHDX_META_FILE_PARAMS_GUID, 16);
    metaEntries[2].offset = 4128;
    metaEntries[2].length = 8;
    *reinterpret_cast<uint32_t*>(&vhdxData[0x200000 + 4128]) = BLOCK_SIZE;

    // 4. BAT Table at 0x100000
    // Entry 0: Payload block present, FileOffsetMB = 3 (0x300000), State = 6 (VhdxBlockFullyPresent)
    uint64_t* bat = reinterpret_cast<uint64_t*>(&vhdxData[0x100000]);
    bat[0] = (3ULL << 20) | titanhub::vhd::VhdxBlockFullyPresent; // Block 0 is at 3MB
    bat[1] = titanhub::vhd::VhdxBlockZero;                        // Block 1 is sparse zero

    // 5. Write test payload in block 0 at 0x300000
    const char* vhdxPattern = "VHDX_PAYLOAD_BLOCK0_VERIFIED";
    std::memcpy(&vhdxData[0x300000], vhdxPattern, std::strlen(vhdxPattern));

    std::ofstream out(vhdxPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(vhdxData.data()), vhdxData.size());
    out.close();

    // Verify
    std::string err;
    auto vdisk = titanhub::vhd::OpenVirtualDisk(vhdxPath, &err);
    TEST_ASSERT(vdisk != nullptr, "OpenVirtualDisk VHDX succeeded");
    if (vdisk) {
        TEST_ASSERT(vdisk->GetFormat() == titanhub::vhd::VirtualDiskFormat::VhdxDynamic, "Format is VhdxDynamic");
        TEST_ASSERT(vdisk->GetVirtualSize() == VIRTUAL_SIZE, "Virtual size is 16MB");
        TEST_ASSERT(vdisk->GetSectorSize() == SECTOR_SIZE, "Sector size is 512");

        // Read from block 0
        char readBuf[64]{};
        bool readOk = vdisk->ReadBytes(0, std::strlen(vhdxPattern), readBuf);
        TEST_ASSERT(readOk, "Read VHDX block 0 succeeded");
        TEST_ASSERT(std::string(readBuf) == vhdxPattern, "VHDX block 0 data matches expected pattern");

        // Read from block 1 (sparse zero)
        std::vector<uint8_t> sparseBuf(512, 0xFF);
        readOk = vdisk->ReadBytes(BLOCK_SIZE, sparseBuf.size(), sparseBuf.data());
        TEST_ASSERT(readOk, "Read VHDX sparse block succeeded");
        bool allZero = true;
        for (uint8_t b : sparseBuf) {
            if (b != 0) { allZero = false; break; }
        }
        TEST_ASSERT(allZero, "VHDX sparse block returned pure zeros");
    }

    vdisk.reset();
    fs::remove(vhdxPath);
}

int main() {
    std::cout << "=================================================\n";
    std::cout << "       Titan Storage Hub Unit Test Battery       \n";
    std::cout << "=================================================\n";

    TestFormatStrings();
    TestParseFsFormatString();
    TestDiskFormatDetector();
    TestCompressionRLE();
    TestCompressionLZ4();
    TestTypesAndTelemetry();
    TestVirtualDiskVhdFixed();
    TestVirtualDiskVhdDynamic();
    TestVirtualDiskVhdx();

    std::cout << "\n-------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << " ALL UNIT TESTS COMPLETED SUCCESSFULLY (0 ERRORS)\n";
        std::cout << "-------------------------------------------------\n";
        return 0;
    } else {
        std::cerr << " TEST SUITE FAILED WITH " << g_failures << " ERROR(S)\n";
        std::cout << "-------------------------------------------------\n";
        return 1;
    }
}
