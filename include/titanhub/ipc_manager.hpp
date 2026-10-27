#pragma once

#include "titanhub/types.hpp"
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
            {"TitanBtrfs",   "TitanBtrfs",   L"\\\\.\\pipe\\titanbtrfs_ipc", 0x4254524653495031ULL}
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
            {FsFormat::Btrfs, L"\\\\.\\pipe\\titanbtrfs_ipc", 0x4254524653495031ULL}
        };

        for (const auto& s : svcs) {
            std::string resp;
            if (SendPipeCommand(s.pipe, s.magic, 7 /* IpcCmdListMounts */, "", resp, 250)) {
                // Parse simple json: {"mounts":[{"drive":"B:","device":"...","subvol":"@","totalBytes":123456}]}
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
                    vol.totalBytes = totalBytes;
                    vol.isMounted = true;
                    vol.isPhysical = (dev.find("\\\\.\\") == 0);
                    outMounts.push_back(vol);

                    pos = driveEnd;
                }
            }
        }
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
