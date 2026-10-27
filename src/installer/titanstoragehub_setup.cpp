#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <algorithm>
#include "titanhub/compression.hpp"

namespace {

#pragma pack(push, 1)
struct PayloadFooter {
    uint64_t tocOffset{0};
    uint64_t payloadStart{0};
    char     magic[16]{0};
};
#pragma pack(pop)
static_assert(sizeof(PayloadFooter) == 32, "PayloadFooter must be 32 bytes");

struct EmbeddedFile {
    std::string name;
    uint64_t offset{0};
    uint64_t compSize{0};
    uint64_t uncompSize{0};
    uint32_t isCompressed{0};
};

bool ReadEmbeddedPayload(std::vector<EmbeddedFile>& outEntries) {
    wchar_t thisExe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, thisExe, MAX_PATH) == 0) return false;

    std::ifstream file(thisExe, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    if (fileSize < sizeof(PayloadFooter)) return false;

    file.seekg(fileSize - sizeof(PayloadFooter), std::ios::beg);
    PayloadFooter footer{};
    file.read(reinterpret_cast<char*>(&footer), sizeof(footer));

    if (std::memcmp(footer.magic, "TITANHUB_BUNDLE", 16) != 0) return false;
    if (footer.tocOffset >= fileSize || footer.payloadStart >= fileSize) return false;

    file.seekg(footer.tocOffset, std::ios::beg);
    uint32_t entryCount = 0;
    file.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));
    if (entryCount == 0 || entryCount > 256) return false;

    outEntries.clear();
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint32_t nameLen = 0;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        if (nameLen > 1024) return false;
        std::string name(nameLen, '\0');
        file.read(name.data(), nameLen);

        EmbeddedFile ent;
        ent.name = name;
        file.read(reinterpret_cast<char*>(&ent.offset), sizeof(ent.offset));
        file.read(reinterpret_cast<char*>(&ent.compSize), sizeof(ent.compSize));
        file.read(reinterpret_cast<char*>(&ent.uncompSize), sizeof(ent.uncompSize));
        file.read(reinterpret_cast<char*>(&ent.isCompressed), sizeof(ent.isCompressed));
        outEntries.push_back(std::move(ent));
    }
    return !outEntries.empty();
}

bool ExtractEmbeddedFile(const EmbeddedFile& ent, const std::wstring& outPath) {
    wchar_t thisExe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, thisExe, MAX_PATH) == 0) return false;

    std::ifstream file(thisExe, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(ent.offset, std::ios::beg);
    std::vector<uint8_t> buffer(ent.compSize);
    file.read(reinterpret_cast<char*>(buffer.data()), ent.compSize);

    std::vector<uint8_t> decompressed;
    if (ent.isCompressed) {
        decompressed.resize(ent.uncompSize);
        if (!titanhub::CompressionEngine::DecompressLZ4(buffer, decompressed)) {
            return false;
        }
    } else {
        decompressed = std::move(buffer);
    }

    std::error_code ec;
    std::filesystem::path p(outPath);
    std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
    return true;
}

enum {
    ID_EDIT_DEST = 1001,
    ID_BTN_BROWSE,
    ID_CHK_PATH,
    ID_CHK_WINFSP,
    ID_CHK_STARTMENU,
    ID_CHK_DESKTOP,
    ID_PROGRESS,
    ID_STATIC_STATUS,
    ID_BTN_INSTALL,
    ID_BTN_CANCEL
};

HINSTANCE g_hInstance{nullptr};
HWND g_hMainWnd{nullptr};
HWND g_hEditDest{nullptr};
HWND g_hBtnBrowse{nullptr};
HWND g_hChkPath{nullptr};
HWND g_hChkWinFsp{nullptr};
HWND g_hChkStartMenu{nullptr};
HWND g_hChkDesktop{nullptr};
HWND g_hProgress{nullptr};
HWND g_hStaticStatus{nullptr};
HWND g_hBtnInstall{nullptr};
HWND g_hBtnCancel{nullptr};

bool IsWinFspInstalled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

HFONT g_hFontTitle{nullptr};
HFONT g_hFontSub{nullptr};
HFONT g_hFontNormal{nullptr};
HFONT g_hFontBold{nullptr};
HBRUSH g_hDarkBrush{nullptr};

ULONG_PTR g_gdiplusToken{0};
std::wstring g_HeroImagePath;
bool g_InstallFinished{false};

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring ws(path);
    size_t pos = ws.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? ws.substr(0, pos) : L".";
}

std::wstring FindHeroImage() {
    std::vector<EmbeddedFile> entries;
    if (ReadEmbeddedPayload(entries)) {
        for (const auto& e : entries) {
            if (e.name.find("hero.jpg") != std::string::npos) {
                std::error_code ec;
                std::wstring tempDir = std::filesystem::temp_directory_path().wstring() + L"\\TitanStorageHub";
                std::filesystem::create_directories(tempDir, ec);
                std::wstring tempHero = tempDir + L"\\titanstoragehub_hero.jpg";
                if (!std::filesystem::exists(tempHero, ec)) {
                    ExtractEmbeddedFile(e, tempHero);
                }
                if (std::filesystem::exists(tempHero, ec)) {
                    return tempHero;
                }
            }
        }
    }

    std::wstring dir = GetModuleDir();
    std::vector<std::wstring> candidates = {
        dir + L"\\assets\\titanstoragehub_hero.jpg",
        dir + L"\\..\\assets\\titanstoragehub_hero.jpg",
        dir + L"\\titanstoragehub_hero.jpg",
        L"assets\\titanstoragehub_hero.jpg"
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) return c;
    }
    return L"";
}

void DrawAspectFill(Gdiplus::Graphics& graphics, Gdiplus::Image* pImg, float targetX, float targetY, float targetW, float targetH) {
    if (!pImg) return;
    float imgW = static_cast<float>(pImg->GetWidth());
    float imgH = static_cast<float>(pImg->GetHeight());
    if (imgW <= 0.0f || imgH <= 0.0f) return;

    float scale = (std::max)(targetW / imgW, targetH / imgH);
    float drawW = imgW * scale;
    float drawH = imgH * scale;
    float drawX = targetX + (targetW - drawW) / 2.0f;
    float drawY = targetY + (targetH - drawH) / 2.0f;

    Gdiplus::Region oldClip;
    graphics.GetClip(&oldClip);
    graphics.SetClip(Gdiplus::RectF(targetX, targetY, targetW, targetH));
    graphics.DrawImage(pImg, drawX, drawY, drawW, drawH);
    graphics.SetClip(&oldClip);
}

bool CreateShortcut(const std::wstring& targetPath, const std::wstring& shortcutPath, const std::wstring& description) {
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (SUCCEEDED(hr)) {
        psl->SetPath(targetPath.c_str());
        psl->SetDescription(description.c_str());
        psl->SetIconLocation(targetPath.c_str(), 0);
        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(shortcutPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    return SUCCEEDED(hr);
}

bool AddToSystemPath(const std::wstring& dir) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        wchar_t currentPath[32768]{};
        DWORD bufSize = sizeof(currentPath);
        DWORD type = REG_EXPAND_SZ;
        RegQueryValueExW(hKey, L"Path", nullptr, &type, reinterpret_cast<LPBYTE>(currentPath), &bufSize);
        std::wstring ws(currentPath);
        if (ws.find(dir) == std::wstring::npos) {
            if (!ws.empty() && ws.back() != L';') ws += L';';
            ws += dir;
            RegSetValueExW(hKey, L"Path", 0, type, reinterpret_cast<const BYTE*>(ws.c_str()), static_cast<DWORD>((ws.size() + 1) * sizeof(wchar_t)));
            DWORD_PTR dwResult;
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 2000, &dwResult);
        }
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool RegisterUninstallEntry(const std::wstring& installDir) {
    HKEY hKey;
    const wchar_t subkey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TitanStorageHub";
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        auto setStr = [&](const wchar_t* valName, const std::wstring& val) {
            RegSetValueExW(hKey, valName, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()), static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
        };
        auto setDword = [&](const wchar_t* valName, DWORD val) {
            RegSetValueExW(hKey, valName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(DWORD));
        };

        setStr(L"DisplayName", L"Titan Storage Hub — Unified Drive Control Center");
        setStr(L"DisplayVersion", L"1.0.0");
        setStr(L"Publisher", L"MicaNT-Kernel / Daniel Elliott");
        setStr(L"InstallLocation", installDir);
        setStr(L"UninstallString", L"\"" + installDir + L"\\titanstoragehub_setup.exe\" /uninstall");
        setStr(L"QuietUninstallString", L"\"" + installDir + L"\\titanstoragehub_setup.exe\" /uninstall /silent");
        setStr(L"DisplayIcon", installDir + L"\\titanstoragehub.exe,0");
        setDword(L"EstimatedSize", 14000);
        setDword(L"NoModify", 1);
        setDword(L"NoRepair", 1);

        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool PerformInstall(const std::wstring& dstDir, bool addPath, bool installWinFsp, bool startMenu, bool desktop, HWND hProg, HWND hStat) {
    auto setStatus = [&](const std::wstring& text, int pos) {
        if (hStat) SetWindowTextW(hStat, text.c_str());
        if (hProg) SendMessageW(hProg, PBM_SETPOS, pos, 0);
    };

    setStatus(L"Preparing destination directory...", 10);
    std::error_code ec;
    std::filesystem::create_directories(dstDir, ec);

    std::wstring srcDir = GetModuleDir();

    // 1. Install WinFsp
    if (installWinFsp && !IsWinFspInstalled()) {
        setStatus(L"Installing WinFsp Filesystem Driver (WHQL)...", 15);
        std::wstring tempMsi = std::filesystem::temp_directory_path().wstring() + L"\\TitanStorageHub\\winfsp.msi";
        bool foundMsi = false;

        std::vector<EmbeddedFile> entries;
        if (ReadEmbeddedPayload(entries)) {
            for (const auto& ef : entries) {
                if (ef.name.find("winfsp") != std::string::npos && ef.name.ends_with(".msi")) {
                    ExtractEmbeddedFile(ef, tempMsi);
                    foundMsi = std::filesystem::exists(tempMsi, ec);
                    break;
                }
            }
        }

        if (foundMsi) {
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"msiexec.exe /i \"" + tempMsi + L"\" /qn /norestart";
            std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
            cmdBuf.push_back(0);
            if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 60000);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
            std::filesystem::remove(tempMsi, ec);
        }
    }

    // 2. Extract Embedded Payload
    std::vector<EmbeddedFile> embeddedFiles;
    if (ReadEmbeddedPayload(embeddedFiles) && !embeddedFiles.empty()) {
        int idx = 0;
        for (const auto& ef : embeddedFiles) {
            int pct = 25 + static_cast<int>((idx * 45) / embeddedFiles.size());
            std::wstring wName(ef.name.begin(), ef.name.end());
            setStatus(L"Extracting " + wName + L"...", pct);
            ExtractEmbeddedFile(ef, dstDir + L"\\" + wName);
            idx++;
        }
    } else {
        setStatus(L"Copying Titan Storage Hub...", 35);
        std::wstring appSrc = srcDir + L"\\titanstoragehub.exe";
        std::filesystem::copy_file(appSrc, dstDir + L"\\titanstoragehub.exe", std::filesystem::copy_options::overwrite_existing, ec);

        std::wstring heroSrc = FindHeroImage();
        if (!heroSrc.empty()) {
            std::filesystem::create_directories(dstDir + L"\\assets", ec);
            std::filesystem::copy_file(heroSrc, dstDir + L"\\assets\\titanstoragehub_hero.jpg", std::filesystem::copy_options::overwrite_existing, ec);
        }
    }

    // Copy setup tool itself for uninstaller
    wchar_t thisExe[MAX_PATH];
    GetModuleFileNameW(nullptr, thisExe, MAX_PATH);
    std::filesystem::copy_file(thisExe, dstDir + L"\\titanstoragehub_setup.exe", std::filesystem::copy_options::overwrite_existing, ec);

    if (addPath) {
        setStatus(L"Configuring System PATH...", 75);
        AddToSystemPath(dstDir);
    }

    std::wstring appPath = dstDir + L"\\titanstoragehub.exe";
    if (startMenu) {
        setStatus(L"Creating Start Menu shortcut...", 85);
        PWSTR pszPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &pszPath))) {
            std::wstring folder = std::wstring(pszPath) + L"\\Titan Storage Hub";
            CoTaskMemFree(pszPath);
            std::filesystem::create_directories(folder, ec);
            CreateShortcut(appPath, folder + L"\\Titan Storage Hub.lnk", L"Titan Storage Hub — Unified Drive Control Center");
        }
    }

    if (desktop) {
        setStatus(L"Creating Desktop shortcut...", 92);
        PWSTR pszPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, nullptr, &pszPath))) {
            std::wstring folder = std::wstring(pszPath);
            CoTaskMemFree(pszPath);
            CreateShortcut(appPath, folder + L"\\Titan Storage Hub.lnk", L"Titan Storage Hub");
        }
    }

    RegisterUninstallEntry(dstDir);
    setStatus(L"Installation complete! Titan Storage Hub is ready to launch.", 100);
    return true;
}

bool PerformUninstall(bool silent) {
    if (!silent) {
        int ret = MessageBoxW(nullptr,
            L"Are you sure you want to uninstall Titan Storage Hub?",
            L"Titan Storage Hub Uninstall",
            MB_YESNO | MB_ICONQUESTION);
        if (ret != IDYES) return false;
    }

    std::wstring installDir = GetModuleDir();
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TitanStorageHub");

    std::error_code ec;
    PWSTR pszPrograms = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &pszPrograms))) {
        std::filesystem::remove_all(std::wstring(pszPrograms) + L"\\Titan Storage Hub", ec);
        CoTaskMemFree(pszPrograms);
    }

    PWSTR pszDesktop = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, nullptr, &pszDesktop))) {
        std::filesystem::remove(std::wstring(pszDesktop) + L"\\Titan Storage Hub.lnk", ec);
        CoTaskMemFree(pszDesktop);
    }

    std::filesystem::remove(installDir + L"\\titanstoragehub.exe", ec);
    std::filesystem::remove_all(installDir + L"\\assets", ec);
    std::filesystem::remove_all(installDir + L"\\redist", ec);

    wchar_t thisExe[MAX_PATH];
    GetModuleFileNameW(nullptr, thisExe, MAX_PATH);
    MoveFileExW(thisExe, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

    if (!silent) {
        MessageBoxW(nullptr, L"Titan Storage Hub has been successfully uninstalled.", L"Uninstall Complete", MB_OK | MB_ICONINFORMATION);
    }
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));

            g_hFontTitle = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontSub = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontNormal = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBold = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hDarkBrush = CreateSolidBrush(RGB(22, 22, 24));

            g_HeroImagePath = FindHeroImage();

            HWND hTitle = CreateWindowExW(0, L"STATIC", L"Titan Storage Hub Setup", WS_CHILD | WS_VISIBLE, 290, 22, 500, 28, hwnd, nullptr, g_hInstance, nullptr);
            SendMessageW(hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontTitle), TRUE);

            HWND hSub = CreateWindowExW(0, L"STATIC", L"Unified Storage & Drive Control Center", WS_CHILD | WS_VISIBLE, 290, 52, 500, 20, hwnd, nullptr, g_hInstance, nullptr);
            SendMessageW(hSub, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontSub), TRUE);

            HWND hDesc = CreateWindowExW(0, L"STATIC", L"Manage ZFS, ReFS, NTFS, and Btrfs drives in one unified control center.", WS_CHILD | WS_VISIBLE, 290, 75, 500, 20, hwnd, nullptr, g_hInstance, nullptr);
            SendMessageW(hDesc, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            HWND hLblDest = CreateWindowExW(0, L"STATIC", L"Installation Destination:", WS_CHILD | WS_VISIBLE, 290, 115, 200, 18, hwnd, nullptr, g_hInstance, nullptr);
            SendMessageW(hLblDest, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            g_hEditDest = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"C:\\Program Files\\TitanStorageHub", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 138, 385, 28, hwnd, reinterpret_cast<HMENU>(ID_EDIT_DEST), g_hInstance, nullptr);
            SendMessageW(g_hEditDest, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            g_hBtnBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 685, 137, 95, 30, hwnd, reinterpret_cast<HMENU>(ID_BTN_BROWSE), g_hInstance, nullptr);
            SendMessageW(g_hBtnBrowse, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            bool winfspPresent = IsWinFspInstalled();
            std::wstring winfspLabel = winfspPresent ? 
                L"WinFsp Filesystem Driver (Detected & Active)" : 
                L"Install WinFsp Filesystem Driver (WHQL runtime bundle)";

            g_hChkPath = CreateWindowExW(0, L"BUTTON", L"Add Titan Storage Hub to System PATH", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 290, 185, 490, 22, hwnd, reinterpret_cast<HMENU>(ID_CHK_PATH), g_hInstance, nullptr);
            SendMessageW(g_hChkPath, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hChkPath, BM_SETCHECK, BST_CHECKED, 0);

            DWORD winfspStyle = WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | (winfspPresent ? WS_DISABLED : WS_TABSTOP);
            g_hChkWinFsp = CreateWindowExW(0, L"BUTTON", winfspLabel.c_str(), winfspStyle, 290, 215, 490, 22, hwnd, reinterpret_cast<HMENU>(ID_CHK_WINFSP), g_hInstance, nullptr);
            SendMessageW(g_hChkWinFsp, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hChkWinFsp, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkStartMenu = CreateWindowExW(0, L"BUTTON", L"Create Start Menu shortcut", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 290, 245, 490, 22, hwnd, reinterpret_cast<HMENU>(ID_CHK_STARTMENU), g_hInstance, nullptr);
            SendMessageW(g_hChkStartMenu, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hChkStartMenu, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkDesktop = CreateWindowExW(0, L"BUTTON", L"Create Desktop shortcut", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 290, 275, 490, 22, hwnd, reinterpret_cast<HMENU>(ID_CHK_DESKTOP), g_hInstance, nullptr);
            SendMessageW(g_hChkDesktop, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hChkDesktop, BM_SETCHECK, BST_CHECKED, 0);

            g_hProgress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 290, 365, 490, 16, hwnd, reinterpret_cast<HMENU>(ID_PROGRESS), g_hInstance, nullptr);
            SendMessageW(g_hProgress, PBM_SETRANGE32, 0, 100);
            SendMessageW(g_hProgress, PBM_SETPOS, 0, 0);

            g_hStaticStatus = CreateWindowExW(0, L"STATIC", L"Ready to install Titan Storage Hub on Windows.", WS_CHILD | WS_VISIBLE, 290, 395, 490, 36, hwnd, reinterpret_cast<HMENU>(ID_STATIC_STATUS), g_hInstance, nullptr);
            SendMessageW(g_hStaticStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            g_hBtnInstall = CreateWindowExW(0, L"BUTTON", L"Install Titan Storage Hub", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 465, 465, 195, 38, hwnd, reinterpret_cast<HMENU>(ID_BTN_INSTALL), g_hInstance, nullptr);
            SendMessageW(g_hBtnInstall, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            g_hBtnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 675, 465, 105, 38, hwnd, reinterpret_cast<HMENU>(ID_BTN_CANCEL), g_hInstance, nullptr);
            SendMessageW(g_hBtnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int height = clientRect.bottom - clientRect.top;

            if (!g_HeroImagePath.empty()) {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                Gdiplus::Image heroImg(g_HeroImagePath.c_str());
                DrawAspectFill(graphics, &heroImg, 0.0f, 0.0f, 260.0f, static_cast<float>(height));

                Gdiplus::SolidBrush vignette(Gdiplus::Color(220, 12, 14, 20));
                graphics.FillRectangle(&vignette, 0, height - 70, 260, 70);

                Gdiplus::FontFamily ff(L"Segoe UI");
                Gdiplus::Font fTitle(&ff, 10.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::Font fSub(&ff, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush tBrush(Gdiplus::Color(255, 240, 245, 255));
                Gdiplus::SolidBrush sBrush(Gdiplus::Color(220, 0, 190, 255));

                graphics.DrawString(L"Titan Storage Hub v1.0.0", -1, &fTitle, Gdiplus::PointF(16.0f, static_cast<Gdiplus::REAL>(height - 52)), &tBrush);
                graphics.DrawString(L"Unified Storage Suite", -1, &fSub, Gdiplus::PointF(16.0f, static_cast<Gdiplus::REAL>(height - 30)), &sBrush);
            } else {
                HBRUSH hSideBrush = CreateSolidBrush(RGB(15, 20, 30));
                RECT rcSide{0, 0, 260, height};
                FillRect(hdc, &rcSide, hSideBrush);
                DeleteObject(hSideBrush);
            }

            HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 180, 255));
            HPEN hOldPen = reinterpret_cast<HPEN>(SelectObject(hdc, hPen));
            MoveToEx(hdc, 260, 0, nullptr);
            LineTo(hdc, 260, height);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORDLG: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(22, 22, 24));
            SetTextColor(hdc, RGB(225, 225, 225));
            return reinterpret_cast<LRESULT>(g_hDarkBrush);
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == ID_BTN_CANCEL) {
                PostQuitMessage(0);
            } else if (wmId == ID_BTN_INSTALL) {
                if (g_InstallFinished) {
                    PostQuitMessage(0);
                    return 0;
                }

                wchar_t destBuf[MAX_PATH]{};
                GetWindowTextW(g_hEditDest, destBuf, MAX_PATH);
                std::wstring dstDir(destBuf);

                bool addPath = (SendMessageW(g_hChkPath, BM_GETCHECK, 0, 0) == BST_CHECKED);
                bool installWinFsp = (SendMessageW(g_hChkWinFsp, BM_GETCHECK, 0, 0) == BST_CHECKED);
                bool startMenu = (SendMessageW(g_hChkStartMenu, BM_GETCHECK, 0, 0) == BST_CHECKED);
                bool desktop = (SendMessageW(g_hChkDesktop, BM_GETCHECK, 0, 0) == BST_CHECKED);

                EnableWindow(g_hBtnInstall, FALSE);
                EnableWindow(g_hBtnBrowse, FALSE);
                EnableWindow(g_hEditDest, FALSE);

                std::thread([=]() {
                    PerformInstall(dstDir, addPath, installWinFsp, startMenu, desktop, g_hProgress, g_hStaticStatus);
                    g_InstallFinished = true;
                    SetWindowTextW(g_hBtnInstall, L"Launch Hub");
                    EnableWindow(g_hBtnInstall, TRUE);
                    ShowWindow(g_hBtnCancel, SW_HIDE);
                }).detach();
            }
            break;
        }

        case WM_DESTROY: {
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontSub) DeleteObject(g_hFontSub);
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hFontBold) DeleteObject(g_hFontBold);
            if (g_hDarkBrush) DeleteObject(g_hDarkBrush);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    g_hInstance = hInstance;

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    std::wstring cmdLine(pCmdLine ? pCmdLine : L"");
    std::transform(cmdLine.begin(), cmdLine.end(), cmdLine.begin(), ::towlower);

    if (cmdLine.find(L"/uninstall") != std::wstring::npos) {
        bool silent = (cmdLine.find(L"/silent") != std::wstring::npos || cmdLine.find(L"/quiet") != std::wstring::npos);
        bool res = PerformUninstall(silent);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return res ? 0 : 1;
    }

    g_HeroImagePath = FindHeroImage();

    INITCOMMONCONTROLSEX icex{
        .dwSize = sizeof(icex),
        .dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES
    };
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"TitanStorageHub_Installer_Class";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    g_hMainWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Titan Storage Hub Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 830, 560,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hMainWnd) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(msg.wParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    return wWinMain(hInstance, nullptr, GetCommandLineW(), nCmdShow);
}
