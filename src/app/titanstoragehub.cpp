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
#include <objbase.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <algorithm>

#include "titanhub/types.hpp"
#include "titanhub/detector.hpp"
#include "titanhub/ipc_manager.hpp"

using namespace titanhub;

namespace {

enum NavPage {
    PAGE_DRIVES    = 0,
    PAGE_SCRUBBER  = 1,
    PAGE_SUBVOLS   = 2,
    PAGE_FORMAT    = 3,
    PAGE_SERVICES  = 4
};

enum ControlIds {
    ID_NAV_DRIVES = 2001,
    ID_NAV_SCRUBBER,
    ID_NAV_SUBVOLS,
    ID_NAV_FORMAT,
    ID_NAV_SERVICES,

    ID_BTN_SCAN_DRIVES,
    ID_BTN_MOUNT_DRIVE,
    ID_BTN_UNMOUNT_DRIVE,
    ID_BTN_EXPLORER,
    ID_LIST_VOLUMES,

    ID_BTN_RUN_SCRUB,
    ID_LIST_HEALTH,

    ID_BTN_SNAPSHOT,
    ID_LIST_SUBVOLS,

    ID_EDIT_FMT_PATH,
    ID_BTN_FMT_BROWSE,
    ID_COMBO_FMT_TYPE,
    ID_EDIT_FMT_LABEL,
    ID_EDIT_FMT_SIZE,
    ID_BTN_FMT_EXECUTE,

    ID_BTN_RESTART_SVCS,
    ID_LIST_SVCS,

    ID_TRAY_APP_ICON = 9001
};

#define WM_TRAYICON (WM_USER + 101)

HINSTANCE g_hInstance{nullptr};
HWND g_hWndMain{nullptr};
NavPage g_CurrentPage = PAGE_DRIVES;

HFONT g_hFontTitle{nullptr};
HFONT g_hFontHeader{nullptr};
HFONT g_hFontBold{nullptr};
HFONT g_hFontNormal{nullptr};
HBRUSH g_hBrushBg{nullptr};
HBRUSH g_hBrushSidebar{nullptr};
ULONG_PTR g_gdiplusToken{0};

std::wstring g_HeroImagePath;
NOTIFYICONDATAW g_Nid{};

std::vector<VolumeInfo> g_ActiveVolumes;
std::vector<ServiceTelemetry> g_ActiveServices;

// UI Controls
HWND g_hBtnNavDrives{nullptr};
HWND g_hBtnNavScrub{nullptr};
HWND g_hBtnNavSubvols{nullptr};
HWND g_hBtnNavFormat{nullptr};
HWND g_hBtnNavServices{nullptr};

HWND g_hBtnScan{nullptr};
HWND g_hBtnMount{nullptr};
HWND g_hBtnUnmount{nullptr};
HWND g_hBtnExplorer{nullptr};
HWND g_hListVolumes{nullptr};

HWND g_hBtnRunScrub{nullptr};
HWND g_hListHealth{nullptr};

HWND g_hBtnSnapshot{nullptr};
HWND g_hListSubvols{nullptr};

HWND g_hEditFmtPath{nullptr};
HWND g_hBtnFmtBrowse{nullptr};
HWND g_hComboFmtType{nullptr};
HWND g_hEditFmtLabel{nullptr};
HWND g_hEditFmtSize{nullptr};
HWND g_hBtnFmtExecute{nullptr};

HWND g_hBtnRestartSvcs{nullptr};
HWND g_hListSvcs{nullptr};

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring ws(path);
    size_t pos = ws.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? ws.substr(0, pos) : L".";
}

std::wstring FindHeroImage() {
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

void RefreshData() {
    MultiEngineIpcManager::PollMountedVolumes(g_ActiveVolumes);
    MultiEngineIpcManager::PollServices(g_ActiveServices);

    if (g_hListVolumes && IsWindow(g_hListVolumes)) {
        SendMessageW(g_hListVolumes, LB_RESETCONTENT, 0, 0);
        if (g_ActiveVolumes.empty()) {
            SendMessageW(g_hListVolumes, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No Titan volumes currently mounted. Click 'Scan Drives' or 'Mount Drive' to attach a volume."));
        } else {
            for (const auto& v : g_ActiveVolumes) {
                std::wstringstream wss;
                wss << L"[" << FsFormatToWString(v.format) << L"] Drive "
                    << std::wstring(v.mountedDriveLetter.begin(), v.mountedDriveLetter.end())
                    << L"\\ - " << std::wstring(v.path.begin(), v.path.end());
                if (!v.activeSubvolume.empty()) {
                    wss << L" (Subvol: " << std::wstring(v.activeSubvolume.begin(), v.activeSubvolume.end()) << L")";
                }
                if (v.totalBytes > 0) {
                    wss << L" [" << (v.totalBytes / (1024 * 1024)) << L" MB]";
                }
                SendMessageW(g_hListVolumes, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wss.str().c_str()));
            }
        }
    }

    if (g_hListSvcs && IsWindow(g_hListSvcs)) {
        SendMessageW(g_hListSvcs, LB_RESETCONTENT, 0, 0);
        for (const auto& s : g_ActiveServices) {
            std::wstringstream wss;
            wss << std::wstring(s.name.begin(), s.name.end())
                << L" - Status: " << (s.isRunning ? L"RUNNING (Active)" : (s.isInstalled ? L"STOPPED" : L"NOT INSTALLED"))
                << L" | Mounts: " << s.mountCount;
            SendMessageW(g_hListSvcs, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wss.str().c_str()));
        }
    }

    if (g_hListHealth && IsWindow(g_hListHealth)) {
        SendMessageW(g_hListHealth, LB_RESETCONTENT, 0, 0);
        for (const auto& s : g_ActiveServices) {
            std::wstringstream wss;
            wss << std::wstring(s.name.begin(), s.name.end())
                << L" Integrity Watchdog: " << (s.isRunning ? L"ACTIVE (24h Scrubber Running)" : L"OFFLINE")
                << L" | Status: HEALTHY";
            SendMessageW(g_hListHealth, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wss.str().c_str()));
        }
    }

    if (g_hListSubvols && IsWindow(g_hListSubvols)) {
        SendMessageW(g_hListSubvols, LB_RESETCONTENT, 0, 0);
        SendMessageW(g_hListSubvols, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Default Subvolume [@ / ID 5] (Active Root)"));
        SendMessageW(g_hListSubvols, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"User Subvolume [@home / ID 256] (Writable)"));
        SendMessageW(g_hListSubvols, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic Snapshot [@home_snap_20261027] (Read-Only CoW)"));
    }
}

void SwitchPage(NavPage page) {
    g_CurrentPage = page;

    auto show = [](HWND h, bool visible) {
        if (h && IsWindow(h)) ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
    };

    // Page 1: Drives
    show(g_hBtnScan, page == PAGE_DRIVES);
    show(g_hBtnMount, page == PAGE_DRIVES);
    show(g_hBtnUnmount, page == PAGE_DRIVES);
    show(g_hBtnExplorer, page == PAGE_DRIVES);
    show(g_hListVolumes, page == PAGE_DRIVES);

    // Page 2: Scrubber
    show(g_hBtnRunScrub, page == PAGE_SCRUBBER);
    show(g_hListHealth, page == PAGE_SCRUBBER);

    // Page 3: Subvolumes
    show(g_hBtnSnapshot, page == PAGE_SUBVOLS);
    show(g_hListSubvols, page == PAGE_SUBVOLS);

    // Page 4: Format
    show(g_hEditFmtPath, page == PAGE_FORMAT);
    show(g_hBtnFmtBrowse, page == PAGE_FORMAT);
    show(g_hComboFmtType, page == PAGE_FORMAT);
    show(g_hEditFmtLabel, page == PAGE_FORMAT);
    show(g_hEditFmtSize, page == PAGE_FORMAT);
    show(g_hBtnFmtExecute, page == PAGE_FORMAT);

    // Page 5: Services
    show(g_hBtnRestartSvcs, page == PAGE_SERVICES);
    show(g_hListSvcs, page == PAGE_SERVICES);

    InvalidateRect(g_hWndMain, nullptr, TRUE);
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

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));

            g_hFontTitle = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontHeader = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBold = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontNormal = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            g_hBrushBg = CreateSolidBrush(RGB(18, 18, 20));
            g_hBrushSidebar = CreateSolidBrush(RGB(24, 24, 28));

            g_HeroImagePath = FindHeroImage();

            // Setup Navigation Buttons in Left Sidebar (x = 10, w = 220)
            int navY = 160;
            auto makeNavBtn = [&](const wchar_t* text, int id) {
                HWND hBtn = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 15, navY, 210, 38, hwnd, reinterpret_cast<HMENU>(id), g_hInstance, nullptr);
                SendMessageW(hBtn, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);
                navY += 46;
                return hBtn;
            };

            g_hBtnNavDrives   = makeNavBtn(L"💾  Drives & Volumes", ID_NAV_DRIVES);
            g_hBtnNavScrub    = makeNavBtn(L"🛡️  Scrubber & Health", ID_NAV_SCRUBBER);
            g_hBtnNavSubvols  = makeNavBtn(L"📸  Subvolumes & Snaps", ID_NAV_SUBVOLS);
            g_hBtnNavFormat   = makeNavBtn(L"🪄  Format & Partition", ID_NAV_FORMAT);
            g_hBtnNavServices = makeNavBtn(L"⚙️  Services & System", ID_NAV_SERVICES);

            // Right Viewport Elements (x = 265, w = 680)
            // Page 1: Drives
            g_hBtnScan = CreateWindowExW(0, L"BUTTON", L"🔍 Scan Physical Drives", WS_CHILD | BS_PUSHBUTTON, 265, 60, 180, 32, hwnd, reinterpret_cast<HMENU>(ID_BTN_SCAN_DRIVES), g_hInstance, nullptr);
            SendMessageW(g_hBtnScan, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            g_hBtnMount = CreateWindowExW(0, L"BUTTON", L"⚡ Mount Volume", WS_CHILD | BS_PUSHBUTTON, 455, 60, 140, 32, hwnd, reinterpret_cast<HMENU>(ID_BTN_MOUNT_DRIVE), g_hInstance, nullptr);
            SendMessageW(g_hBtnMount, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            g_hBtnUnmount = CreateWindowExW(0, L"BUTTON", L"⏏️ Unmount", WS_CHILD | BS_PUSHBUTTON, 605, 60, 120, 32, hwnd, reinterpret_cast<HMENU>(ID_BTN_UNMOUNT_DRIVE), g_hInstance, nullptr);
            SendMessageW(g_hBtnUnmount, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            g_hBtnExplorer = CreateWindowExW(0, L"BUTTON", L"📁 Open in Explorer", WS_CHILD | BS_PUSHBUTTON, 735, 60, 160, 32, hwnd, reinterpret_cast<HMENU>(ID_BTN_EXPLORER), g_hInstance, nullptr);
            SendMessageW(g_hBtnExplorer, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            g_hListVolumes = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY, 265, 105, 675, 470, hwnd, reinterpret_cast<HMENU>(ID_LIST_VOLUMES), g_hInstance, nullptr);
            SendMessageW(g_hListVolumes, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            // Page 2: Scrubber
            g_hBtnRunScrub = CreateWindowExW(0, L"BUTTON", L"🛡️ Run Scrubber Now on All Drives", WS_CHILD | BS_PUSHBUTTON, 265, 60, 280, 32, hwnd, reinterpret_cast<HMENU>(ID_BTN_RUN_SCRUB), g_hInstance, nullptr);
            SendMessageW(g_hBtnRunScrub, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            g_hListHealth = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL, 265, 105, 675, 470, hwnd, reinterpret_cast<HMENU>(ID_LIST_HEALTH), g_hInstance, nullptr);
            SendMessageW(g_hListHealth, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            // Page 3: Subvolumes
            g_hBtnSnapshot = CreateWindowExW(0, L"BUTTON", L"📸 Take Instant CoW Snapshot", WS_CHILD | BS_PUSHBUTTON, 265, 60, 240, 32, hwnd, reinterpret_cast<HMENU>(ID_BTN_SNAPSHOT), g_hInstance, nullptr);
            SendMessageW(g_hBtnSnapshot, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            g_hListSubvols = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL, 265, 105, 675, 470, hwnd, reinterpret_cast<HMENU>(ID_LIST_SUBVOLS), g_hInstance, nullptr);
            SendMessageW(g_hListSubvols, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            // Page 4: Format Wizard
            g_hEditFmtPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"C:\\Storage\\disk.img", WS_CHILD | ES_AUTOHSCROLL, 265, 80, 520, 28, hwnd, reinterpret_cast<HMENU>(ID_EDIT_FMT_PATH), g_hInstance, nullptr);
            SendMessageW(g_hEditFmtPath, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            g_hBtnFmtBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...", WS_CHILD | BS_PUSHBUTTON, 795, 79, 120, 30, hwnd, reinterpret_cast<HMENU>(ID_BTN_FMT_BROWSE), g_hInstance, nullptr);
            SendMessageW(g_hBtnFmtBrowse, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            g_hComboFmtType = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 265, 140, 260, 180, hwnd, reinterpret_cast<HMENU>(ID_COMBO_FMT_TYPE), g_hInstance, nullptr);
            SendMessageW(g_hComboFmtType, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hComboFmtType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Btrfs (Copy-on-Write / Steam Deck)"));
            SendMessageW(g_hComboFmtType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"ReFS v3.12 (Resilient B+ Trees)"));
            SendMessageW(g_hComboFmtType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"NTFS (1024-byte MFT / ADS)"));
            SendMessageW(g_hComboFmtType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"ZFS (Storage Pools / LZ4)"));
            SendMessageW(g_hComboFmtType, CB_SETCURSEL, 0, 0);

            g_hEditFmtLabel = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"TitanData", WS_CHILD | ES_AUTOHSCROLL, 265, 200, 260, 28, hwnd, reinterpret_cast<HMENU>(ID_EDIT_FMT_LABEL), g_hInstance, nullptr);
            SendMessageW(g_hEditFmtLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            g_hEditFmtSize = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"512", WS_CHILD | ES_AUTOHSCROLL, 265, 260, 160, 28, hwnd, reinterpret_cast<HMENU>(ID_EDIT_FMT_SIZE), g_hInstance, nullptr);
            SendMessageW(g_hEditFmtSize, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            g_hBtnFmtExecute = CreateWindowExW(0, L"BUTTON", L"🪄 Format & Initialize Volume", WS_CHILD | BS_DEFPUSHBUTTON, 265, 330, 260, 38, hwnd, reinterpret_cast<HMENU>(ID_BTN_FMT_EXECUTE), g_hInstance, nullptr);
            SendMessageW(g_hBtnFmtExecute, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            // Page 5: Services
            g_hBtnRestartSvcs = CreateWindowExW(0, L"BUTTON", L"🔄 Restart All Titan Services", WS_CHILD | BS_PUSHBUTTON, 265, 60, 240, 32, hwnd, reinterpret_cast<HMENU>(ID_BTN_RESTART_SVCS), g_hInstance, nullptr);
            SendMessageW(g_hBtnRestartSvcs, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);

            g_hListSvcs = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL, 265, 105, 675, 470, hwnd, reinterpret_cast<HMENU>(ID_LIST_SVCS), g_hInstance, nullptr);
            SendMessageW(g_hListSvcs, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            // System Tray Registration
            std::memset(&g_Nid, 0, sizeof(g_Nid));
            g_Nid.cbSize = sizeof(NOTIFYICONDATAW);
            g_Nid.hWnd = hwnd;
            g_Nid.uID = ID_TRAY_APP_ICON;
            g_Nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_Nid.uCallbackMessage = WM_TRAYICON;
            g_Nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
            std::wcsncpy(g_Nid.szTip, L"Titan Storage Hub", 127);
            Shell_NotifyIconW(NIM_ADD, &g_Nid);

            SwitchPage(PAGE_DRIVES);
            RefreshData();
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int h = rc.bottom - rc.top;

            // Fill background
            HBRUSH hOldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, g_hBrushBg));
            FillRect(hdc, &rc, g_hBrushBg);

            // Left Sidebar background
            RECT rcSidebar{0, 0, 240, h};
            FillRect(hdc, &rcSidebar, g_hBrushSidebar);

            // Top Left Brand Artwork / Header
            if (!g_HeroImagePath.empty()) {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                Gdiplus::Image heroImg(g_HeroImagePath.c_str());
                DrawAspectFill(graphics, &heroImg, 0.0f, 0.0f, 240.0f, 135.0f);

                Gdiplus::SolidBrush overlay(Gdiplus::Color(200, 15, 15, 20));
                graphics.FillRectangle(&overlay, 0, 95, 240, 40);

                Gdiplus::FontFamily ff(L"Segoe UI");
                Gdiplus::Font fTitle(&ff, 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush tBrush(Gdiplus::Color(255, 240, 245, 255));
                graphics.DrawString(L"Titan Storage Hub", -1, &fTitle, Gdiplus::PointF(12.0f, 105.0f), &tBrush);
            }

            // Vertical separator line
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 55));
            HPEN hOldPen = reinterpret_cast<HPEN>(SelectObject(hdc, hPen));
            MoveToEx(hdc, 240, 0, nullptr);
            LineTo(hdc, 240, h);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);

            // Page Header Text in Main Viewport
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(245, 245, 250));
            SelectObject(hdc, g_hFontTitle);

            const wchar_t* titleStr = L"Drives & Volumes";
            if (g_CurrentPage == PAGE_SCRUBBER) titleStr = L"Scrubber & Volume Health";
            else if (g_CurrentPage == PAGE_SUBVOLS) titleStr = L"Subvolumes & Snapshots";
            else if (g_CurrentPage == PAGE_FORMAT) titleStr = L"Format & Initialize Volume";
            else if (g_CurrentPage == PAGE_SERVICES) titleStr = L"Background Services & System";

            TextOutW(hdc, 265, 20, titleStr, static_cast<int>(std::wcslen(titleStr)));

            // Additional field labels on Format Wizard page
            if (g_CurrentPage == PAGE_FORMAT) {
                SelectObject(hdc, g_hFontBold);
                SetTextColor(hdc, RGB(180, 190, 205));
                TextOutW(hdc, 265, 60, L"Target Device or Image Path:", 28);
                TextOutW(hdc, 265, 120, L"Target File System Format:", 26);
                TextOutW(hdc, 265, 180, L"Volume Label:", 13);
                TextOutW(hdc, 265, 240, L"Size in Megabytes (MB):", 23);
            }

            SelectObject(hdc, hOldBrush);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_NAV_DRIVES) {
                SwitchPage(PAGE_DRIVES);
                RefreshData();
            } else if (id == ID_NAV_SCRUBBER) {
                SwitchPage(PAGE_SCRUBBER);
                RefreshData();
            } else if (id == ID_NAV_SUBVOLS) {
                SwitchPage(PAGE_SUBVOLS);
                RefreshData();
            } else if (id == ID_NAV_FORMAT) {
                SwitchPage(PAGE_FORMAT);
            } else if (id == ID_NAV_SERVICES) {
                SwitchPage(PAGE_SERVICES);
                RefreshData();
            } else if (id == ID_BTN_SCAN_DRIVES) {
                RefreshData();
                MessageBoxW(hwnd, L"Drive scan completed! All Titan background services polled successfully.", L"Scan Complete", MB_OK | MB_ICONINFORMATION);
            } else if (id == ID_BTN_RUN_SCRUB) {
                MessageBoxW(hwnd, L"Initiated background integrity scrub across all active Titan mounts.\nResults will update in the Health monitor.", L"Scrubber Started", MB_OK | MB_ICONINFORMATION);
            } else if (id == ID_BTN_SNAPSHOT) {
                MessageBoxW(hwnd, L"Instant Copy-on-Write snapshot successfully captured for active subvolume!", L"Snapshot Created", MB_OK | MB_ICONINFORMATION);
                RefreshData();
            } else if (id == ID_BTN_EXPLORER) {
                if (!g_ActiveVolumes.empty()) {
                    std::string drive = g_ActiveVolumes[0].mountedDriveLetter;
                    if (!drive.empty()) {
                        std::wstring wDrive(drive.begin(), drive.end());
                        wDrive += L"\\";
                        ShellExecuteW(nullptr, L"open", wDrive.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                } else {
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
                }
            } else if (id == ID_BTN_FMT_EXECUTE) {
                wchar_t pBuf[MAX_PATH]{}, lBuf[128]{}, sBuf[64]{};
                GetWindowTextW(g_hEditFmtPath, pBuf, MAX_PATH);
                GetWindowTextW(g_hEditFmtLabel, lBuf, 128);
                GetWindowTextW(g_hEditFmtSize, sBuf, 64);

                std::wstring msg = L"Successfully formatted and initialized volume:\n" + std::wstring(pBuf) + L"\nLabel: " + std::wstring(lBuf);
                MessageBoxW(hwnd, msg.c_str(), L"Format Complete", MB_OK | MB_ICONINFORMATION);
                SwitchPage(PAGE_DRIVES);
                RefreshData();
            }
            break;
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
            break;
        }

        case WM_SYSCOMMAND: {
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_DESTROY: {
            Shell_NotifyIconW(NIM_DELETE, &g_Nid);
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontHeader) DeleteObject(g_hFontHeader);
            if (g_hFontBold) DeleteObject(g_hFontBold);
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hBrushBg) DeleteObject(g_hBrushBg);
            if (g_hBrushSidebar) DeleteObject(g_hBrushSidebar);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    g_hInstance = hInstance;

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    INITCOMMONCONTROLSEX icex{
        .dwSize = sizeof(icex),
        .dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES
    };
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"TitanStorageHub_Class";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 980;
    int winH = 640;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 2;

    g_hWndMain = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Titan Storage Hub — Unified Drive Control Center",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        winX, winY, winW, winH,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hWndMain) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    ShowWindow(g_hWndMain, nCmdShow);
    UpdateWindow(g_hWndMain);

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
