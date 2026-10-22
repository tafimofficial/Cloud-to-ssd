#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdbool.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define IDC_EDIT_PATH          2001
#define IDC_BTN_BROWSE         2002
#define IDC_CHK_STARTUP        2003
#define IDC_CHK_DESKTOP        2004
#define IDC_CHK_STARTMENU      2005
#define IDC_BTN_INSTALL        2006
#define IDC_BTN_CANCEL         2007
#define IDC_PROGRESS           2008
#define IDC_LBL_STATUS         2009

static HWND g_hMainWnd = NULL;
static HWND g_hEditPath = NULL;
static HWND g_hChkStartup = NULL;
static HWND g_hChkDesktop = NULL;
static HWND g_hChkStartMenu = NULL;
static HWND g_hBtnInstall = NULL;
static HWND g_hBtnCancel = NULL;
static HWND g_hProgress = NULL;
static HWND g_hLblStatus = NULL;
static HFONT g_hFontTitle = NULL;
static HFONT g_hFontUI = NULL;
static HFONT g_hFontBtn = NULL;
static HBRUSH g_hDarkBgBrush = NULL;
static bool g_bInstalled = false;

static HRESULT create_shortcut(LPCWSTR lpszTarget, LPCWSTR lpszLnkPath, LPCWSTR lpszDesc, LPCWSTR lpszIconPath) {
    CoInitialize(NULL);
    IShellLinkW* psl = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (LPVOID*)&psl);
    if (SUCCEEDED(hr)) {
        psl->lpVtbl->SetPath(psl, lpszTarget);
        psl->lpVtbl->SetDescription(psl, lpszDesc);
        if (lpszIconPath) {
            psl->lpVtbl->SetIconLocation(psl, lpszIconPath, 0);
        }
        IPersistFile* ppf = NULL;
        hr = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->lpVtbl->Save(ppf, lpszLnkPath, TRUE);
            ppf->lpVtbl->Release(ppf);
        }
        psl->lpVtbl->Release(psl);
    }
    CoUninitialize();
    return hr;
}

static bool browse_folder(HWND hWnd, WCHAR* out_path, size_t max_chars) {
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = hWnd;
    bi.pszDisplayName = out_path;
    bi.lpszTitle = L"Select Installation Directory for Tafim Drive:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, out_path)) {
            CoTaskMemFree(pidl);
            return true;
        }
        CoTaskMemFree(pidl);
    }
    return false;
}

static DWORD WINAPI install_thread_proc(LPVOID lpParam) {
    (void)lpParam;
    WCHAR szInstallDir[MAX_PATH];
    GetWindowTextW(g_hEditPath, szInstallDir, MAX_PATH);

    bool bStartup = (SendMessageW(g_hChkStartup, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool bDesktop = (SendMessageW(g_hChkDesktop, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool bStartMenu = (SendMessageW(g_hChkStartMenu, BM_GETCHECK, 0, 0) == BST_CHECKED);

    EnableWindow(g_hBtnInstall, FALSE);
    EnableWindow(g_hBtnCancel, FALSE);
    ShowWindow(g_hProgress, SW_SHOW);
    SendMessageW(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(g_hProgress, PBM_SETPOS, 10, 0);

    SetWindowTextW(g_hLblStatus, L"Creating installation directories...");
    CreateDirectoryW(szInstallDir, NULL);
    SendMessageW(g_hProgress, PBM_SETPOS, 20, 0);

    /* 1. Extract embedded payload resource 102 */
    SetWindowTextW(g_hLblStatus, L"Extracting Tafim Drive & cloud engine...");
    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(102), (LPCWSTR)RT_RCDATA);
    if (hRes) {
        HGLOBAL hMem = LoadResource(NULL, hRes);
        DWORD dwSize = SizeofResource(NULL, hRes);
        void* pData = LockResource(hMem);

        if (pData && dwSize > 0) {
            WCHAR szTempZip[MAX_PATH];
            GetTempPathW(MAX_PATH, szTempZip);
            wcscat_s(szTempZip, MAX_PATH, L"tafim_payload.zip");

            FILE* fp = NULL;
            if (_wfopen_s(&fp, szTempZip, L"wb") == 0 && fp) {
                fwrite(pData, 1, dwSize, fp);
                fclose(fp);

                SendMessageW(g_hProgress, PBM_SETPOS, 45, 0);
                SetWindowTextW(g_hLblStatus, L"Unpacking files into program folder...");

                WCHAR szCmd[MAX_PATH * 3];
                swprintf_s(szCmd, sizeof(szCmd)/sizeof(szCmd[0]),
                           L"tar.exe -xf \"%s\" -C \"%s\"", szTempZip, szInstallDir);

                STARTUPINFOW si;
                PROCESS_INFORMATION pi;
                memset(&si, 0, sizeof(si));
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;

                if (CreateProcessW(NULL, szCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, 60000);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }

                DeleteFileW(szTempZip);
            }
        }
    }

    SendMessageW(g_hProgress, PBM_SETPOS, 70, 0);

    /* 2. Create Shortcuts */
    WCHAR szTargetExe[MAX_PATH];
    swprintf_s(szTargetExe, MAX_PATH, L"%s\\Tafim Drive.exe", szInstallDir);
    WCHAR szIconPath[MAX_PATH];
    swprintf_s(szIconPath, MAX_PATH, L"%s\\tafim_drive.ico", szInstallDir);

    if (bDesktop) {
        SetWindowTextW(g_hLblStatus, L"Creating Desktop shortcut...");
        WCHAR szDesktop[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, szDesktop))) {
            WCHAR szLnk[MAX_PATH];
            swprintf_s(szLnk, MAX_PATH, L"%s\\Tafim Drive.lnk", szDesktop);
            create_shortcut(szTargetExe, szLnk, L"Tafim Drive - Multi-Cloud Storage Manager", szIconPath);
        }
    }

    if (bStartMenu) {
        SetWindowTextW(g_hLblStatus, L"Creating Start Menu shortcut...");
        WCHAR szPrograms[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, szPrograms))) {
            WCHAR szFolder[MAX_PATH];
            swprintf_s(szFolder, MAX_PATH, L"%s\\Tafim Drive", szPrograms);
            CreateDirectoryW(szFolder, NULL);

            WCHAR szLnk[MAX_PATH];
            swprintf_s(szLnk, MAX_PATH, L"%s\\Tafim Drive\\Tafim Drive.lnk", szPrograms);
            create_shortcut(szTargetExe, szLnk, L"Tafim Drive - Multi-Cloud Storage Manager", szIconPath);

            WCHAR szLnkRoot[MAX_PATH];
            swprintf_s(szLnkRoot, MAX_PATH, L"%s\\Tafim Drive.lnk", szPrograms);
            create_shortcut(szTargetExe, szLnkRoot, L"Tafim Drive - Multi-Cloud Storage Manager", szIconPath);
        }
    }

    SendMessageW(g_hProgress, PBM_SETPOS, 85, 0);

    /* 3. Register Startup Run Key (Auto-Mount on Reboot / Start) */
    if (bStartup) {
        SetWindowTextW(g_hLblStatus, L"Configuring Windows Startup auto-mount...");
        HKEY hRunKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_SET_VALUE, &hRunKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hRunKey, L"Tafim");
            WCHAR szRunVal[MAX_PATH * 2];
            swprintf_s(szRunVal, sizeof(szRunVal)/sizeof(szRunVal[0]), L"\"%s\" --boot", szTargetExe);
            RegSetValueExW(hRunKey, L"TafimDrive", 0, REG_SZ, (const BYTE*)szRunVal,
                           (DWORD)((wcslen(szRunVal) + 1) * sizeof(WCHAR)));
            RegCloseKey(hRunKey);
        }
    }

    /* 4. Register in Windows Settings (Add or Remove Programs) */
    SetWindowTextW(g_hLblStatus, L"Registering Windows Settings Uninstall entry...");
    HKEY hUninstall;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TafimDrive",
                        0, NULL, 0, KEY_ALL_ACCESS, NULL, &hUninstall, NULL) == ERROR_SUCCESS) {

        WCHAR szUninstallExe[MAX_PATH * 2];
        swprintf_s(szUninstallExe, sizeof(szUninstallExe)/sizeof(szUninstallExe[0]), L"\"%s\\Uninstall.exe\"", szInstallDir);

        const WCHAR* szName = L"Tafim Drive";
        const WCHAR* szVer = L"1.0.0";
        const WCHAR* szPublisher = L"Tafim";
        const WCHAR* szUrl = L"https://github.com/tafim";
        DWORD dwSizeKb = 115000;
        DWORD dwOne = 1;

        RegSetValueExW(hUninstall, L"DisplayName", 0, REG_SZ, (const BYTE*)szName, (DWORD)((wcslen(szName) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hUninstall, L"DisplayVersion", 0, REG_SZ, (const BYTE*)szVer, (DWORD)((wcslen(szVer) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hUninstall, L"Publisher", 0, REG_SZ, (const BYTE*)szPublisher, (DWORD)((wcslen(szPublisher) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hUninstall, L"DisplayIcon", 0, REG_SZ, (const BYTE*)szIconPath, (DWORD)((wcslen(szIconPath) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hUninstall, L"InstallLocation", 0, REG_SZ, (const BYTE*)szInstallDir, (DWORD)((wcslen(szInstallDir) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hUninstall, L"UninstallString", 0, REG_SZ, (const BYTE*)szUninstallExe, (DWORD)((wcslen(szUninstallExe) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hUninstall, L"HelpLink", 0, REG_SZ, (const BYTE*)szUrl, (DWORD)((wcslen(szUrl) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hUninstall, L"EstimatedSize", 0, REG_DWORD, (const BYTE*)&dwSizeKb, sizeof(DWORD));
        RegSetValueExW(hUninstall, L"NoModify", 0, REG_DWORD, (const BYTE*)&dwOne, sizeof(DWORD));
        RegSetValueExW(hUninstall, L"NoRepair", 0, REG_DWORD, (const BYTE*)&dwOne, sizeof(DWORD));

        RegCloseKey(hUninstall);
    }

    SendMessageW(g_hProgress, PBM_SETPOS, 100, 0);
    SetWindowTextW(g_hLblStatus, L"Setup Finished! Tafim Drive is ready to use.");

    g_bInstalled = true;
    SetWindowTextW(g_hBtnInstall, L"Finish & Launch");
    EnableWindow(g_hBtnInstall, TRUE);
    EnableWindow(g_hBtnCancel, TRUE);
    SetWindowTextW(g_hBtnCancel, L"Close");

    return 0;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hMainWnd = hWnd;
            g_hFontTitle = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontUI = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBtn = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hDarkBgBrush = CreateSolidBrush(RGB(15, 23, 42));

            /* Install path label & edit */
            HWND lblPath = CreateWindowW(L"STATIC", L"Install Location (User Programs Directory):",
                                         WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         28, 85, 480, 20, hWnd, NULL, NULL, NULL);
            SendMessageW(lblPath, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

            g_hEditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                          28, 108, 395, 28, hWnd, (HMENU)IDC_EDIT_PATH, NULL, NULL);
            SendMessageW(g_hEditPath, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

            HWND btnBrowse = CreateWindowW(L"BUTTON", L"Browse...",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           430, 108, 90, 28, hWnd, (HMENU)IDC_BTN_BROWSE, NULL, NULL);
            SendMessageW(btnBrowse, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            /* Default install directory in %LOCALAPPDATA%\Programs\Tafim Drive */
            WCHAR szDefaultDir[MAX_PATH];
            SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, szDefaultDir);
            wcscat_s(szDefaultDir, MAX_PATH, L"\\Programs\\Tafim Drive");
            SetWindowTextW(g_hEditPath, szDefaultDir);

            /* Checkboxes */
            g_hChkStartup = CreateWindowW(L"BUTTON", L"Launch on Windows Startup (Keep cloud drives auto-mounted on reboot)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          28, 155, 495, 24, hWnd, (HMENU)IDC_CHK_STARTUP, NULL, NULL);
            SendMessageW(g_hChkStartup, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            SendMessageW(g_hChkStartup, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkDesktop = CreateWindowW(L"BUTTON", L"Create Desktop shortcut",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          28, 185, 495, 24, hWnd, (HMENU)IDC_CHK_DESKTOP, NULL, NULL);
            SendMessageW(g_hChkDesktop, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            SendMessageW(g_hChkDesktop, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkStartMenu = CreateWindowW(L"BUTTON", L"Create Start Menu shortcut",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                            28, 215, 495, 24, hWnd, (HMENU)IDC_CHK_STARTMENU, NULL, NULL);
            SendMessageW(g_hChkStartMenu, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            SendMessageW(g_hChkStartMenu, BM_SETCHECK, BST_CHECKED, 0);

            /* Progress Bar */
            g_hProgress = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
                                          WS_CHILD | PBS_SMOOTH,
                                          28, 260, 492, 18, hWnd, (HMENU)IDC_PROGRESS, NULL, NULL);

            /* Status Label */
            g_hLblStatus = CreateWindowW(L"STATIC", L"Ready to install. Click Install Now to begin.",
                                         WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         28, 288, 492, 22, hWnd, (HMENU)IDC_LBL_STATUS, NULL, NULL);
            SendMessageW(g_hLblStatus, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

            /* Bottom Buttons */
            g_hBtnInstall = CreateWindowW(L"BUTTON", L"Install Now",
                                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                          280, 325, 140, 36, hWnd, (HMENU)IDC_BTN_INSTALL, NULL, NULL);
            SendMessageW(g_hBtnInstall, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            g_hBtnCancel = CreateWindowW(L"BUTTON", L"Cancel",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         430, 325, 90, 36, hWnd, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
            SendMessageW(g_hBtnCancel, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDC_BTN_BROWSE) {
                WCHAR szSelected[MAX_PATH];
                if (browse_folder(hWnd, szSelected, MAX_PATH)) {
                    SetWindowTextW(g_hEditPath, szSelected);
                }
            } else if (wmId == IDC_BTN_INSTALL) {
                if (g_bInstalled) {
                    /* Launch app and exit */
                    WCHAR szInstallDir[MAX_PATH];
                    GetWindowTextW(g_hEditPath, szInstallDir, MAX_PATH);
                    WCHAR szExe[MAX_PATH];
                    swprintf_s(szExe, MAX_PATH, L"%s\\Tafim Drive.exe", szInstallDir);
                    ShellExecuteW(NULL, L"open", szExe, NULL, NULL, SW_SHOWNORMAL);
                    PostQuitMessage(0);
                } else {
                    CreateThread(NULL, 0, install_thread_proc, NULL, 0, NULL);
                }
            } else if (wmId == IDC_BTN_CANCEL) {
                PostQuitMessage(0);
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rcHeader = { 0, 0, 560, 68 };
            HBRUSH hHeaderBrush = CreateSolidBrush(RGB(11, 15, 25));
            FillRect(hdc, &rcHeader, hHeaderBrush);
            DeleteObject(hHeaderBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(56, 189, 248));
            SelectObject(hdc, g_hFontTitle);
            TextOutW(hdc, 28, 14, L"TAFIM DRIVE SETUP", 17);

            SetTextColor(hdc, RGB(148, 163, 184));
            SelectObject(hdc, g_hFontUI);
            TextOutW(hdc, 28, 40, L"Install Multi-Cloud Storage System with Persistent Windows Mounts", 65);

            EndPaint(hWnd, &ps);
            break;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORDLG: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(226, 232, 240));
            SetBkColor(hdcStatic, RGB(15, 23, 42));
            return (LRESULT)g_hDarkBgBrush;
        }

        case WM_DESTROY: {
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontUI) DeleteObject(g_hFontUI);
            if (g_hFontBtn) DeleteObject(g_hFontBtn);
            if (g_hDarkBgBrush) DeleteObject(g_hDarkBgBrush);
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)pCmdLine;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wcex;
    memset(&wcex, 0, sizeof(wcex));
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIconW(hInstance, (LPCWSTR)MAKEINTRESOURCE(101));
    wcex.hIconSm = (HICON)LoadImageW(hInstance, (LPCWSTR)MAKEINTRESOURCE(101), IMAGE_ICON, 16, 16, 0);
    wcex.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(15, 23, 42));
    wcex.lpszClassName = L"TafimDriveSetupClass";

    RegisterClassExW(&wcex);

    HWND hWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"TafimDriveSetupClass",
        L"Tafim Drive Setup - Installation Wizard",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 420,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) return 1;

    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    ShowWindow(hWnd, nCmdShow ? nCmdShow : SW_SHOWNORMAL);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
