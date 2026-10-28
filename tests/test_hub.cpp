#include "titanhub/types.hpp"
#include "titanhub/detector.hpp"
#include "titanhub/compression.hpp"
#include "titanhub/ipc_manager.hpp"

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
