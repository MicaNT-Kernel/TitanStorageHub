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
    TEST_ASSERT(std::string(titanhub::FsFormatToString(titanhub::FsFormat::Unknown)) == "Unknown", "Unknown name");

    TEST_ASSERT(std::wstring(titanhub::FsFormatToWString(titanhub::FsFormat::ZFS)) == L"ZFS", "ZFS wname");
    TEST_ASSERT(std::wstring(titanhub::FsFormatToWString(titanhub::FsFormat::Btrfs)) == L"Btrfs", "Btrfs wname");
}

void TestDiskFormatDetector() {
    std::cout << "\n=== TestDiskFormatDetector ===\n";
    fs::path tempDir = fs::temp_directory_path() / "titanhub_tests";
    fs::create_directories(tempDir);

    // 1. Test Btrfs
    {
        fs::path p = tempDir / "test_btrfs.img";
        std::vector<uint8_t> buf(131072, 0);
        // Signature at 0x10040: "_BHRfS_M"
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
        uint64_t outSize = 0;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel, &outSize);
        TEST_ASSERT(fmt == titanhub::FsFormat::ReFS, "ReFS format detection");
        TEST_ASSERT(outLabel == "TITAN_REFS", "ReFS label");
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
        uint64_t outSize = 0;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel, &outSize);
        TEST_ASSERT(fmt == titanhub::FsFormat::NTFS, "NTFS format detection");
        TEST_ASSERT(outLabel == "TITAN_NTFS", "NTFS label");
        fs::remove(p);
    }

    // 4. Test ZFS
    {
        fs::path p = tempDir / "test_zfs.img";
        std::vector<uint8_t> buf(4096, 0);
        std::memcpy(buf.data() + 512, "zfs_pool", 8);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        uint64_t outSize = 0;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel, &outSize);
        TEST_ASSERT(fmt == titanhub::FsFormat::ZFS, "ZFS format detection");
        TEST_ASSERT(outLabel == "TITAN_ZFS", "ZFS label");
        fs::remove(p);
    }

    // 5. Test Unknown
    {
        fs::path p = tempDir / "test_unknown.img";
        std::vector<uint8_t> buf(4096, 0xAA);

        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        f.close();

        std::string outLabel;
        uint64_t outSize = 0;
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string(), &outLabel, &outSize);
        TEST_ASSERT(fmt == titanhub::FsFormat::Unknown, "Unknown format detection");
        fs::remove(p);
    }

    // 6. Test Non-existent file
    {
        fs::path p = tempDir / "non_existent.img";
        auto fmt = titanhub::DiskFormatDetector::DetectFormat(p.string());
        TEST_ASSERT(fmt == titanhub::FsFormat::Unknown, "Missing file returns Unknown");
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

    // 3. Small buffer
    {
        std::string smallText = "Hello, Titan Storage Suite!";
        std::vector<uint8_t> src(smallText.begin(), smallText.end());

        std::vector<uint8_t> comp;
        size_t cSize = titanhub::CompressionEngine::CompressLZ4(src, comp);
        TEST_ASSERT(cSize > 0, "LZ4 small text compressed");

        std::vector<uint8_t> decomp(src.size(), 0);
        bool ok = titanhub::CompressionEngine::DecompressLZ4(comp, decomp);
        TEST_ASSERT(ok, "LZ4 small text decompressed");
        TEST_ASSERT(decomp == src, "LZ4 small text match");
    }
}

void TestTypesAndTelemetry() {
    std::cout << "\n=== TestTypesAndTelemetry ===\n";

    titanhub::VolumeInfo vol;
    vol.format = titanhub::FsFormat::Btrfs;
    vol.label = "DATA_POOL";
    vol.totalBytes = 100ULL * 1024 * 1024 * 1024;
    vol.usedBytes = 40ULL * 1024 * 1024 * 1024;
    vol.mountedDriveLetter = "T:";
    vol.activeSubvolume = "@home";
    vol.isMounted = true;
    vol.isPhysical = true;

    TEST_ASSERT(vol.format == titanhub::FsFormat::Btrfs, "VolumeInfo format");
    TEST_ASSERT(vol.mountedDriveLetter == "T:", "VolumeInfo drive letter");
    TEST_ASSERT(vol.activeSubvolume == "@home", "VolumeInfo subvolume");
    TEST_ASSERT(vol.isMounted && vol.isPhysical, "VolumeInfo flags");

    titanhub::ScrubTelemetry scrub;
    scrub.format = titanhub::FsFormat::ZFS;
    scrub.driveLetter = "Z:";
    scrub.isRunning = true;
    scrub.progressPercent = 65;
    scrub.blocksScanned = 1000000;
    scrub.errorsDetected = 3;
    scrub.repairedCount = 3;

    TEST_ASSERT(scrub.format == titanhub::FsFormat::ZFS, "ScrubTelemetry format");
    TEST_ASSERT(scrub.progressPercent == 65, "ScrubTelemetry progress");
    TEST_ASSERT(scrub.errorsDetected == scrub.repairedCount, "ScrubTelemetry repaired == errors");

    titanhub::SubvolSnapshotEntry subvol;
    subvol.format = titanhub::FsFormat::Btrfs;
    subvol.id = 256;
    subvol.parentId = 5;
    subvol.name = "@snapshots_2026";
    subvol.isSnapshot = true;
    subvol.generation = 42;

    TEST_ASSERT(subvol.isSnapshot, "SubvolSnapshotEntry isSnapshot");
    TEST_ASSERT(subvol.generation == 42, "SubvolSnapshotEntry generation");
}

int main() {
    std::cout << "=================================================\n";
    std::cout << "       Titan Storage Hub Unit Test Battery       \n";
    std::cout << "=================================================\n";

    TestFormatStrings();
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
