#pragma once

#include "titanhub/types.hpp"
#include "titanhub/detector.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <memory>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>
#include <winioctl.h>
#endif

namespace titanhub {

class MultiEngineIpcManager {
public:
    static bool SendPipeCommand(const std::wstring& pipeName, uint64_t magic, uint32_t cmd,
                                const std::string& request, std::string& response, uint32_t timeoutMs = 2000) {
#ifdef _WIN32
        if (!WaitNamedPipeW(pipeName.c_str(), timeoutMs)) {
            return false;
        }

        HANDLE hPipe = CreateFileW(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
        if (hPipe == INVALID_HANDLE_VALUE) return false;

#pragma pack(push, 1)
        struct ReqHeader {
            uint64_t magic;
            uint32_t cmd;
            uint32_t payloadLen;
        };
        struct RespHdr {
            uint64_t magic;
            uint32_t status;
            uint32_t payloadLen;
        };
#pragma pack(pop)

        ReqHeader reqHdr{magic, cmd, static_cast<uint32_t>(request.size())};
        DWORD written = 0;
        if (!WriteFile(hPipe, &reqHdr, sizeof(reqHdr), &written, nullptr) || written != sizeof(reqHdr)) {
            CloseHandle(hPipe);
            return false;
        }

        if (!request.empty()) {
            WriteFile(hPipe, request.data(), static_cast<DWORD>(request.size()), &written, nullptr);
        }

        RespHdr respHdr{};
        DWORD readBytes = 0;
        if (!ReadFile(hPipe, &respHdr, sizeof(respHdr), &readBytes, nullptr) || readBytes != sizeof(respHdr)) {
            CloseHandle(hPipe);
            return false;
        }

        response.clear();
        if (respHdr.payloadLen > 0) {
            response.resize(respHdr.payloadLen);
            DWORD total = 0;
            while (total < respHdr.payloadLen) {
                DWORD chunk = 0;
                if (!ReadFile(hPipe, response.data() + total, respHdr.payloadLen - total, &chunk, nullptr) || chunk == 0) {
                    break;
                }
                total += chunk;
            }
        }

        CloseHandle(hPipe);
        return true;
#else
        (void)pipeName; (void)magic; (void)cmd; (void)request; (void)response; (void)timeoutMs;
        return false;
#endif
    }

    static void PollServices(std::vector<ServiceTelemetry>& outServices) {
        outServices.clear();

        struct SvcDef {
            const char* name;
            const char* id;
            const wchar_t* pipe;
            uint64_t magic;
        } svcs[] = {
            {"TitanZFS",     "TitanZFS",     L"\\\\.\\pipe\\titanzfs_ipc",   0x5A465349504331ULL},
            {"TitanReFS",    "TitanReFS",    L"\\\\.\\pipe\\titanrefs_ipc",  0x5245465349504331ULL},
            {"TitanNTFS",    "TitanNTFS",    L"\\\\.\\pipe\\titanntfs_ipc",  0x4E54465349504331ULL},
            {"TitanBtrfs",   "TitanBtrfs",   L"\\\\.\\pipe\\titanbtrfs_ipc", 0x4254524653495031ULL},
            {"TitanExt",     "TitanExt",     L"\\\\.\\pipe\\titanext_ipc",   0x4558543449504331ULL}
        };

#ifdef _WIN32
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
#endif

        for (const auto& s : svcs) {
            ServiceTelemetry st;
            st.name = s.name;
            st.serviceId = s.id;
            std::wstring wPipe(s.pipe);
            st.pipeName = std::string(wPipe.begin(), wPipe.end());

#ifdef _WIN32
            if (scm) {
                std::wstring wId(s.id, s.id + std::strlen(s.id));
                SC_HANDLE svc = OpenServiceW(scm, wId.c_str(), SERVICE_QUERY_STATUS);
                if (svc) {
                    st.isInstalled = true;
                    SERVICE_STATUS_PROCESS ssp{};
                    DWORD bytesNeeded = 0;
                    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded)) {
                        st.isRunning = (ssp.dwCurrentState == SERVICE_RUNNING);
                    }
                    CloseServiceHandle(svc);
                }
            }

            // Probe pipe for live responsiveness
            std::string resp;
            if (SendPipeCommand(s.pipe, s.magic, 6 /* IpcCmdServiceStatus */, "", resp, 100)) {
                st.isRunning = true;
                if (resp.find("\"mountCount\":") != std::string::npos) {
                    auto pos = resp.find("\"mountCount\":");
                    st.mountCount = std::atoi(resp.c_str() + pos + 13);
                }
            }
#endif
            outServices.push_back(st);
        }

#ifdef _WIN32
        if (scm) CloseServiceHandle(scm);
#endif
    }

    static void PollMountedVolumes(std::vector<VolumeInfo>& outMounts) {
        outMounts.clear();

        struct SvcDef {
            FsFormat fmt;
            const wchar_t* pipe;
            uint64_t magic;
        } svcs[] = {
            {FsFormat::ZFS,   L"\\\\.\\pipe\\titanzfs_ipc",   0x5A465349504331ULL},
            {FsFormat::ReFS,  L"\\\\.\\pipe\\titanrefs_ipc",  0x5245465349504331ULL},
            {FsFormat::NTFS,  L"\\\\.\\pipe\\titanntfs_ipc",  0x4E54465349504331ULL},
            {FsFormat::Btrfs, L"\\\\.\\pipe\\titanbtrfs_ipc", 0x4254524653495031ULL},
            {FsFormat::Ext4,  L"\\\\.\\pipe\\titanext_ipc",   0x4558543449504331ULL}
        };

        for (const auto& s : svcs) {
            std::string resp;
            if (SendPipeCommand(s.pipe, s.magic, 7 /* IpcCmdListMounts */, "", resp, 250)) {
                size_t pos = 0;
                while ((pos = resp.find("{\"drive\":\"", pos)) != std::string::npos) {
                    pos += 10;
                    size_t driveEnd = resp.find('"', pos);
                    std::string drive = resp.substr(pos, driveEnd - pos);

                    std::string dev;
                    size_t devPos = resp.find("\"device\":\"", driveEnd);
                    if (devPos != std::string::npos) {
                        devPos += 10;
                        size_t devEnd = resp.find('"', devPos);
                        dev = resp.substr(devPos, devEnd - devPos);
                    }

                    std::string subvol;
                    size_t subPos = resp.find("\"subvol\":\"", driveEnd);
                    if (subPos != std::string::npos) {
                        subPos += 10;
                        size_t subEnd = resp.find('"', subPos);
                        subvol = resp.substr(subPos, subEnd - subPos);
                    }

                    uint64_t totalBytes = 0;
                    size_t totPos = resp.find("\"totalBytes\":", driveEnd);
                    if (totPos != std::string::npos) {
                        totPos += 13;
                        totalBytes = std::strtoull(resp.c_str() + totPos, nullptr, 10);
                    }

                    VolumeInfo vol;
                    vol.format = s.fmt;
                    vol.path = dev;
                    vol.mountedDriveLetter = drive;
                    vol.activeSubvolume = subvol;
                    vol.fileSystemName = FsFormatToString(s.fmt);
                    vol.driveType = "Titan Virtual (" + std::string(FsFormatToString(s.fmt)) + ")";
                    vol.totalBytes = totalBytes;
                    vol.isMounted = true;
                    vol.isPhysical = (dev.find("\\\\.\\") == 0);
                    outMounts.push_back(vol);

                    pos = driveEnd;
                }
            }
        }
    }

    static void EnumerateAllSystemDrives(std::vector<VolumeInfo>& outVolumes) {
        outVolumes.clear();

        // 1. Collect all Titan-mounted volumes via IPC
        std::vector<VolumeInfo> titanMounts;
        PollMountedVolumes(titanMounts);

#ifdef _WIN32
        // 2. Query all Windows logical drive strings (A:\, C:\, D:\, etc.)
        wchar_t driveStrings[512]{0};
        DWORD len = GetLogicalDriveStringsW(511, driveStrings);
        if (len > 0) {
            const wchar_t* pDrive = driveStrings;
            while (*pDrive) {
                std::wstring rootPath = pDrive; // e.g. L"C:\\"
                std::string driveLetter;
                if (rootPath.size() >= 2) {
                    driveLetter = std::string(rootPath.begin(), rootPath.begin() + 2);
                }

                // Check if this drive letter is managed by a Titan service
                bool isTitan = false;
                for (const auto& tm : titanMounts) {
                    if (tm.mountedDriveLetter == driveLetter) {
                        VolumeInfo augmented = tm;
                        augmented.driveType = "Titan Virtual (" + std::string(FsFormatToString(tm.format)) + ")";
                        outVolumes.push_back(augmented);
                        isTitan = true;
                        break;
                    }
                }

                if (!isTitan) {
                    UINT driveType = GetDriveTypeW(rootPath.c_str());
                    if (driveType != DRIVE_NO_ROOT_DIR) {
                        wchar_t volName[MAX_PATH + 1]{0};
                        wchar_t fsName[MAX_PATH + 1]{0};
                        DWORD serialNum = 0, maxComponentLen = 0, flags = 0;

                        GetVolumeInformationW(
                            rootPath.c_str(),
                            volName, MAX_PATH,
                            &serialNum, &maxComponentLen, &flags,
                            fsName, MAX_PATH
                        );

                        ULARGE_INTEGER freeBytesAvail{}, totalBytes{}, totalFreeBytes{};
                        GetDiskFreeSpaceExW(rootPath.c_str(), &freeBytesAvail, &totalBytes, &totalFreeBytes);

                        VolumeInfo vol;
                        vol.mountedDriveLetter = driveLetter;
                        vol.path = std::string(rootPath.begin(), rootPath.end());
                        vol.isMounted = true;
                        vol.isPhysical = (driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE);

                        std::wstring wVol(volName);
                        vol.label = std::string(wVol.begin(), wVol.end());
                        if (vol.label.empty()) {
                            vol.label = (driveType == DRIVE_FIXED) ? "Local Disk" : "Removable Drive";
                        }

                        std::wstring wFs(fsName);
                        vol.fileSystemName = std::string(wFs.begin(), wFs.end());
                        vol.format = ParseFsFormatString(vol.fileSystemName);

                        vol.totalBytes = totalBytes.QuadPart;
                        vol.usedBytes = (totalBytes.QuadPart >= totalFreeBytes.QuadPart)
                                            ? (totalBytes.QuadPart - totalFreeBytes.QuadPart) : 0;

                        switch (driveType) {
                            case DRIVE_FIXED:     vol.driveType = "Fixed SSD/HDD"; break;
                            case DRIVE_REMOVABLE: vol.driveType = "Removable USB / SD"; break;
                            case DRIVE_CDROM:     vol.driveType = "Optical / ISO"; break;
                            case DRIVE_REMOTE:    vol.driveType = "Network Share"; break;
                            case DRIVE_RAMDISK:   vol.driveType = "RAM Disk"; break;
                            default:              vol.driveType = "System Volume"; break;
                        }

                        outVolumes.push_back(vol);
                    }
                }

                pDrive += rootPath.length() + 1;
            }
        }

        // Add any remaining Titan mounts not covered in logical strings
        for (const auto& tm : titanMounts) {
            bool found = false;
            for (const auto& v : outVolumes) {
                if (v.mountedDriveLetter == tm.mountedDriveLetter) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                VolumeInfo v = tm;
                v.driveType = "Titan Virtual (" + std::string(FsFormatToString(tm.format)) + ")";
                outVolumes.push_back(v);
            }
        }

        // 3. Probe physical drives \\.\PhysicalDrive0 .. 7 for unmounted raw filesystems
        for (int i = 0; i < 8; ++i) {
            std::string physPath = "\\\\.\\PhysicalDrive" + std::to_string(i);
            std::wstring wPhysPath(physPath.begin(), physPath.end());

            HANDLE hDisk = CreateFileW(
                wPhysPath.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr
            );

            if (hDisk != INVALID_HANDLE_VALUE) {
                std::vector<uint8_t> buffer(131072, 0);
                DWORD readBytes = 0;
                if (ReadFile(hDisk, buffer.data(), static_cast<DWORD>(buffer.size()), &readBytes, nullptr) && readBytes >= 512) {
                    std::string label;
                    buffer.resize(readBytes);
                    FsFormat detected = DiskFormatDetector::DetectFormatFromBuffer(buffer, &label);
                    if (detected != FsFormat::Unknown) {
                        bool exists = false;
                        for (const auto& v : outVolumes) {
                            if (v.path == physPath) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            VolumeInfo pVol;
                            pVol.path = physPath;
                            pVol.format = detected;
                            pVol.label = label.empty() ? ("Drive " + std::to_string(i)) : label;
                            pVol.fileSystemName = FsFormatToString(detected);
                            pVol.driveType = "Physical Disk (Raw/Unmounted)";
                            pVol.isPhysical = true;
                            pVol.isMounted = false;

                            DISK_GEOMETRY_EX geom{};
                            DWORD bytesReturned = 0;
                            if (DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geom, sizeof(geom), &bytesReturned, nullptr)) {
                                pVol.totalBytes = static_cast<uint64_t>(geom.DiskSize.QuadPart);
                            }
                            outVolumes.push_back(pVol);
                        }
                    }
                }
                CloseHandle(hDisk);
            }
        }
#else
        for (const auto& tm : titanMounts) {
            outVolumes.push_back(tm);
        }
#endif
    }

    static bool MountVolume(FsFormat fmt, const std::string& dev, const std::string& drive,
                            const std::string& extra, std::string& err) {
        const wchar_t* pipe = nullptr;
        uint64_t magic = 0;
        switch (fmt) {
            case FsFormat::ZFS:   pipe = L"\\\\.\\pipe\\titanzfs_ipc";   magic = 0x5A465349504331ULL; break;
            case FsFormat::ReFS:  pipe = L"\\\\.\\pipe\\titanrefs_ipc";  magic = 0x5245465349504331ULL; break;
            case FsFormat::NTFS:  pipe = L"\\\\.\\pipe\\titanntfs_ipc";  magic = 0x4E54465349504331ULL; break;
            case FsFormat::Btrfs: pipe = L"\\\\.\\pipe\\titanbtrfs_ipc"; magic = 0x4254524653495031ULL; break;
            case FsFormat::Ext4:  pipe = L"\\\\.\\pipe\\titanext_ipc";   magic = 0x4558543449504331ULL; break;
            default: err = "Unsupported filesystem format."; return false;
        }

        std::string req = dev + " " + drive;
        if (!extra.empty()) req += " " + extra;

        std::string resp;
        if (!SendPipeCommand(pipe, magic, 2 /* IpcCmdVolumeMount */, req, resp, 5000)) {
            err = "Failed to communicate with service background daemon. Is the service running?";
            return false;
        }

        if (resp.find("\"status\":\"mounted\"") != std::string::npos) {
            return true;
        }

        err = resp;
        return false;
    }

    static bool UnmountVolume(FsFormat fmt, const std::string& drive, std::string& err) {
        const wchar_t* pipe = nullptr;
        uint64_t magic = 0;
        switch (fmt) {
            case FsFormat::ZFS:   pipe = L"\\\\.\\pipe\\titanzfs_ipc";   magic = 0x5A465349504331ULL; break;
            case FsFormat::ReFS:  pipe = L"\\\\.\\pipe\\titanrefs_ipc";  magic = 0x5245465349504331ULL; break;
            case FsFormat::NTFS:  pipe = L"\\\\.\\pipe\\titanntfs_ipc";  magic = 0x4E54465349504331ULL; break;
            case FsFormat::Btrfs: pipe = L"\\\\.\\pipe\\titanbtrfs_ipc"; magic = 0x4254524653495031ULL; break;
            case FsFormat::Ext4:  pipe = L"\\\\.\\pipe\\titanext_ipc";   magic = 0x4558543449504331ULL; break;
            default: err = "Unsupported filesystem format."; return false;
        }

        std::string resp;
        if (!SendPipeCommand(pipe, magic, 3 /* IpcCmdVolumeUnmount */, drive, resp, 5000)) {
            err = "Failed to communicate with service daemon.";
            return false;
        }

        return (resp.find("\"status\":\"unmounted\"") != std::string::npos);
    }
};

} // namespace titanhub
