#include "tafim_drive.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shlobj.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define IDC_BTN_CONNECT_TERABOX  1001
#define IDC_BTN_CONNECT_GDRIVE   1002
#define IDC_BTN_CONNECT_ONEDRIVE 1003
#define IDC_BTN_CONNECT_DROPBOX  1004
#define IDC_BTN_CONNECT_MEGA     1005
#define IDC_BTN_CONNECT_WEBDAV   1006
#define IDC_BTN_MOUNT_ALL        1007
#define IDC_BTN_UNMOUNT_ALL      1008
#define IDC_BTN_EXPLORER         1009
#define IDC_BTN_TOGGLE_MOUNT     1010
#define IDC_BTN_REMOVE           1011
#define IDC_BTN_REFRESH          1012
#define IDC_LISTVIEW_DRIVES      1013
#define IDC_STATUS_LABEL         1014
#define IDC_EDIT_CACHE_PATH      1015
#define IDC_BTN_BROWSE_CACHE     1016
#define IDC_EDIT_CACHE_SIZE      1017
#define IDC_BTN_SAVE_CACHE       1018

#define WM_TRAYICON (WM_USER + 1)
#define WM_DRIVE_CONNECTED (WM_USER + 100)

static HWND g_hMainWnd = NULL;
static HWND g_hListView = NULL;
static HWND g_hStatusLabel = NULL;
static HWND g_hEditCachePath = NULL;
static HWND g_hEditCacheSize = NULL;
static AppConfig* g_pConfig = NULL;
static NOTIFYICONDATAW g_nid;
static HFONT g_hFontTitle = NULL;
static HFONT g_hFontUI = NULL;
static HFONT g_hFontBtn = NULL;
static HBRUSH g_hDarkBgBrush = NULL;

static bool browse_for_folder(HWND hWndOwner, WCHAR* out_path, size_t max_chars) {
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = hWndOwner;
    bi.pidlRoot = NULL;
    bi.pszDisplayName = out_path;
    bi.lpszTitle = L"Select Folder for Offline Cache Storage (SSD / Hard Disk):";
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

static void get_drive_storage_text(char drive_letter, MountState state, double quota_total_gb, WCHAR* out_str, size_t max_chars) {
    if (state == MOUNT_STATE_MOUNTED) {
        WCHAR root[8];
        swprintf_s(root, sizeof(root)/sizeof(root[0]), L"%c:\\", (wchar_t)toupper(drive_letter));

        ULARGE_INTEGER freeBytesCaller, totalBytes, totalFreeBytes;
        if (GetDiskFreeSpaceExW(root, &freeBytesCaller, &totalBytes, &totalFreeBytes)) {
            double totalGb = (double)totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
            double freeGb = (double)totalFreeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
            double usedGb = totalGb - freeGb;
            if (usedGb < 0) usedGb = 0;
            double pct = (totalGb > 0) ? (usedGb / totalGb * 100.0) : 0;
            swprintf_s(out_str, max_chars, L"%.1f GB / %.0f GB (%.0f%%)", usedGb, totalGb, pct);
            return;
        }
    }
    if (quota_total_gb > 0) {
        swprintf_s(out_str, max_chars, L"0 GB / %.0f GB (0%%)", quota_total_gb);
    } else {
        wcscpy_s(out_str, max_chars, L"Dynamic");
    }
}

static void update_listview(void) {
    if (!g_hListView || !g_pConfig) return;

    mount_update_all_states(g_pConfig);
    int currentItemCount = (int)SendMessageW(g_hListView, LVM_GETITEMCOUNT, 0, 0);

    /* If item count matches, update storage numbers and status in-place so row selection is NEVER lost! */
    if (currentItemCount == g_pConfig->account_count) {
        for (int i = 0; i < g_pConfig->account_count; i++) {
            DriveAccount* acc = &g_pConfig->accounts[i];
            
            WCHAR wQuota[64];
            get_drive_storage_text(acc->drive_letter, acc->state, acc->quota_total_gb, wQuota, sizeof(wQuota)/sizeof(wQuota[0]));
            LVITEMW lviQ;
            memset(&lviQ, 0, sizeof(lviQ));
            lviQ.iSubItem = 3;
            lviQ.pszText = wQuota;
            SendMessageW(g_hListView, LVM_SETITEMTEXTW, i, (LPARAM)&lviQ);

            const wchar_t* wStatus = L"[OFFLINE] Disconnected";
            if (acc->state == MOUNT_STATE_MOUNTED) {
                wStatus = L"[ONLINE] Mounted";
            } else if (acc->state == MOUNT_STATE_STARTING) {
                wStatus = L"Starting...";
            } else if (acc->state == MOUNT_STATE_ERROR) {
                wStatus = L"Error";
            }

            LVITEMW lvi;
            memset(&lvi, 0, sizeof(lvi));
            lvi.iSubItem = 4;
            lvi.pszText = (LPWSTR)wStatus;
            SendMessageW(g_hListView, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);
        }
        return;
    }

    /* If item count changed, preserve previous selection */
    int prev_sel = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);

    SendMessageW(g_hListView, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_hListView, LVM_DELETEALLITEMS, 0, 0);

    for (int i = 0; i < g_pConfig->account_count; i++) {
        DriveAccount* acc = &g_pConfig->accounts[i];

        WCHAR wDrive[16];
        swprintf_s(wDrive, sizeof(wDrive) / sizeof(wDrive[0]), L"  %c:", (wchar_t)toupper(acc->drive_letter));

        LVITEMW lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = wDrive;
        SendMessageW(g_hListView, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        WCHAR wName[TAFIM_STR_LEN];
        MultiByteToWideChar(CP_UTF8, 0, acc->name, -1, wName, TAFIM_STR_LEN);
        lvi.iSubItem = 1;
        lvi.pszText = wName;
        SendMessageW(g_hListView, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

        WCHAR wProv[64];
        MultiByteToWideChar(CP_UTF8, 0, provider_type_to_string(acc->provider), -1, wProv, 64);
        lvi.iSubItem = 2;
        lvi.pszText = wProv;
        SendMessageW(g_hListView, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

        WCHAR wQuota[64];
        get_drive_storage_text(acc->drive_letter, acc->state, acc->quota_total_gb, wQuota, sizeof(wQuota)/sizeof(wQuota[0]));
        lvi.iSubItem = 3;
        lvi.pszText = wQuota;
        SendMessageW(g_hListView, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

        const wchar_t* wStatus = L"[OFFLINE] Disconnected";
        if (acc->state == MOUNT_STATE_MOUNTED) {
            wStatus = L"[ONLINE] Mounted";
        } else if (acc->state == MOUNT_STATE_STARTING) {
            wStatus = L"Starting...";
        } else if (acc->state == MOUNT_STATE_ERROR) {
            wStatus = L"Error";
        }
        lvi.iSubItem = 4;
        lvi.pszText = (LPWSTR)wStatus;
        SendMessageW(g_hListView, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);
    }

    if (prev_sel >= 0 && prev_sel < g_pConfig->account_count) {
        LVITEMW selItem;
        memset(&selItem, 0, sizeof(selItem));
        selItem.state = LVIS_SELECTED | LVIS_FOCUSED;
        selItem.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(g_hListView, LVM_SETITEMSTATE, prev_sel, (LPARAM)&selItem);
    }

    SendMessageW(g_hListView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hListView, NULL, TRUE);
}

static char pick_next_free_drive_letter(void) {
    char free_letters[32];
    mount_get_available_letters(free_letters, sizeof(free_letters));
    /* Pick first available drive letter that is not already configured */
    for (int i = 0; free_letters[i]; i++) {
        char letter = free_letters[i];
        if (config_find_account_by_letter(g_pConfig, letter) < 0) {
            return letter;
        }
    }
    return 'Z';
}

static int get_selected_account_index(void) {
    if (!g_hListView) return -1;
    return (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hMainWnd = hWnd;
            server_set_gui_hwnd(hWnd);

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

            /* Cloud Provider Connect Buttons */
            HWND btnTb = CreateWindowW(L"BUTTON", L"+ TeraBox (1TB)",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       24, 75, 105, 34, hWnd, (HMENU)IDC_BTN_CONNECT_TERABOX, NULL, NULL);
            SendMessageW(btnTb, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnGd = CreateWindowW(L"BUTTON", L"+ Google Drive",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       134, 75, 110, 34, hWnd, (HMENU)IDC_BTN_CONNECT_GDRIVE, NULL, NULL);
            SendMessageW(btnGd, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnOd = CreateWindowW(L"BUTTON", L"+ OneDrive",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       249, 75, 95, 34, hWnd, (HMENU)IDC_BTN_CONNECT_ONEDRIVE, NULL, NULL);
            SendMessageW(btnOd, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnDb = CreateWindowW(L"BUTTON", L"+ Dropbox",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       349, 75, 85, 34, hWnd, (HMENU)IDC_BTN_CONNECT_DROPBOX, NULL, NULL);
            SendMessageW(btnDb, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnMg = CreateWindowW(L"BUTTON", L"+ Mega (20GB)",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       439, 75, 95, 34, hWnd, (HMENU)IDC_BTN_CONNECT_MEGA, NULL, NULL);
            SendMessageW(btnMg, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnWd = CreateWindowW(L"BUTTON", L"+ WebDAV",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       539, 75, 80, 34, hWnd, (HMENU)IDC_BTN_CONNECT_WEBDAV, NULL, NULL);
            SendMessageW(btnWd, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnMountAll = CreateWindowW(L"BUTTON", L"Mount All",
                                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                             624, 75, 95, 34, hWnd, (HMENU)IDC_BTN_MOUNT_ALL, NULL, NULL);
            SendMessageW(btnMountAll, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnUnmountAll = CreateWindowW(L"BUTTON", L"Unmount All",
                                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                               724, 75, 96, 34, hWnd, (HMENU)IDC_BTN_UNMOUNT_ALL, NULL, NULL);
            SendMessageW(btnUnmountAll, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            /* ListView for Drives */
            g_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                         24, 125, 796, 255, hWnd, (HMENU)IDC_LISTVIEW_DRIVES, NULL, NULL);
            SendMessageW(g_hListView, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            SendMessageW(g_hListView, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                         LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            SetWindowTheme(g_hListView, L"Explorer", NULL);

            /* Insert Columns with explicit SendMessageW LVM_INSERTCOLUMNW so header text is never truncated! */
            LVCOLUMNW lvc;
            memset(&lvc, 0, sizeof(lvc));
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
            lvc.fmt = LVCFMT_LEFT;

            lvc.pszText = L"Drive";
            lvc.cx = 75;
            SendMessageW(g_hListView, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

            lvc.pszText = L"Account Name";
            lvc.cx = 220;
            SendMessageW(g_hListView, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

            lvc.pszText = L"Provider";
            lvc.cx = 130;
            SendMessageW(g_hListView, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

            lvc.pszText = L"Storage (Used / Total)";
            lvc.cx = 175;
            SendMessageW(g_hListView, LVM_INSERTCOLUMNW, 3, (LPARAM)&lvc);

            lvc.pszText = L"Mount Status";
            lvc.cx = 180;
            SendMessageW(g_hListView, LVM_INSERTCOLUMNW, 4, (LPARAM)&lvc);

            /* Bottom Action Buttons */
            HWND btnOpen = CreateWindowW(L"BUTTON", L"Open in Explorer",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         24, 390, 160, 34, hWnd, (HMENU)IDC_BTN_EXPLORER, NULL, NULL);
            SendMessageW(btnOpen, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnToggle = CreateWindowW(L"BUTTON", L"Mount / Unmount",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           194, 390, 150, 34, hWnd, (HMENU)IDC_BTN_TOGGLE_MOUNT, NULL, NULL);
            SendMessageW(btnToggle, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnRemove = CreateWindowW(L"BUTTON", L"Disconnect Account",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           354, 390, 160, 34, hWnd, (HMENU)IDC_BTN_REMOVE, NULL, NULL);
            SendMessageW(btnRemove, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND btnRefresh = CreateWindowW(L"BUTTON", L"Refresh Status",
                                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                            524, 390, 140, 34, hWnd, (HMENU)IDC_BTN_REFRESH, NULL, NULL);
            SendMessageW(btnRefresh, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            /* Offline Cache Settings Controls */
            HWND lblCachePath = CreateWindowW(L"STATIC", L"Offline Cache Location (SSD / HDD Path):",
                                              WS_CHILD | WS_VISIBLE | SS_LEFT,
                                              24, 434, 430, 18, hWnd, NULL, NULL, NULL);
            SendMessageW(lblCachePath, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

            g_hEditCachePath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                               24, 454, 430, 26, hWnd, (HMENU)IDC_EDIT_CACHE_PATH, NULL, NULL);
            SendMessageW(g_hEditCachePath, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

            HWND btnBrowse = CreateWindowW(L"BUTTON", L"Browse...",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           460, 454, 85, 26, hWnd, (HMENU)IDC_BTN_BROWSE_CACHE, NULL, NULL);
            SendMessageW(btnBrowse, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            HWND lblCacheSize = CreateWindowW(L"STATIC", L"Max Size (GB):",
                                              WS_CHILD | WS_VISIBLE | SS_LEFT,
                                              555, 434, 95, 18, hWnd, NULL, NULL, NULL);
            SendMessageW(lblCacheSize, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

            g_hEditCacheSize = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                               WS_CHILD | WS_VISIBLE | ES_NUMBER,
                                               555, 454, 75, 26, hWnd, (HMENU)IDC_EDIT_CACHE_SIZE, NULL, NULL);
            SendMessageW(g_hEditCacheSize, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

            HWND btnSaveCache = CreateWindowW(L"BUTTON", L"Save Cache Settings",
                                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              640, 454, 180, 26, hWnd, (HMENU)IDC_BTN_SAVE_CACHE, NULL, NULL);
            SendMessageW(btnSaveCache, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

            /* Initialize with current config values */
            WCHAR wCacheDir[MAX_PATH];
            MultiByteToWideChar(CP_UTF8, 0, g_pConfig->cache_dir, -1, wCacheDir, MAX_PATH);
            SetWindowTextW(g_hEditCachePath, wCacheDir);

            WCHAR wSize[32];
            swprintf_s(wSize, sizeof(wSize)/sizeof(wSize[0]), L"%d", g_pConfig->cache_max_gb > 0 ? g_pConfig->cache_max_gb : 50);
            SetWindowTextW(g_hEditCacheSize, wSize);

            /* Status Label */
            g_hStatusLabel = CreateWindowW(L"STATIC", L"Ready | Browser Auth Server listening on http://127.0.0.1:8989",
                                           WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           24, 494, 796, 22, hWnd, (HMENU)IDC_STATUS_LABEL, NULL, NULL);
            SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

            /* System Tray Icon */
            memset(&g_nid, 0, sizeof(g_nid));
            g_nid.cbSize = sizeof(g_nid);
            g_nid.hWnd = hWnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAYICON;
            g_nid.hIcon = (HICON)GetClassLongPtr(hWnd, GCLP_HICON);
            wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(g_nid.szTip[0]), L"Tafim Drive - Multi-Cloud Storage");
            Shell_NotifyIconW(NIM_ADD, &g_nid);

            if (g_pConfig->auto_mount_on_startup) {
                mount_all_auto(g_pConfig);
            }

            update_listview();
            SetTimer(hWnd, 1, 2000, NULL);
            break;
        }

        case WM_TIMER: {
            update_listview();
            break;
        }

        case WM_DRIVE_CONNECTED: {
            char drive = (char)wParam;
            update_listview();
            MessageBeep(MB_ICONASTERISK);
            mount_open_in_explorer(drive);
            SetForegroundWindow(hWnd);

            WCHAR msgBuf[128];
            swprintf_s(msgBuf, sizeof(msgBuf)/sizeof(msgBuf[0]), L"Storage Drive %c: Connected & Mounted in Windows Explorer!", drive);
            SetWindowTextW(g_hStatusLabel, msgBuf);
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDC_BTN_CONNECT_TERABOX: {
                    char target = pick_next_free_drive_letter();
                    WCHAR wUrl[256];
                    swprintf_s(wUrl, sizeof(wUrl) / sizeof(wUrl[0]),
                               L"http://127.0.0.1:8989/connect/terabox?drive=%c", target);

                    SetWindowTextW(g_hStatusLabel, L"Opening browser to authenticate TeraBox...");
                    ShellExecuteW(NULL, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
                    break;
                }

                case IDC_BTN_CONNECT_GDRIVE: {
                    char target = pick_next_free_drive_letter();
                    WCHAR wUrl[256];
                    swprintf_s(wUrl, sizeof(wUrl) / sizeof(wUrl[0]),
                               L"http://127.0.0.1:8989/connect/gdrive?drive=%c", target);

                    SetWindowTextW(g_hStatusLabel, L"Opening browser to authenticate Google Drive...");
                    ShellExecuteW(NULL, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
                    break;
                }

                case IDC_BTN_CONNECT_ONEDRIVE: {
                    char target = pick_next_free_drive_letter();
                    WCHAR wUrl[256];
                    swprintf_s(wUrl, sizeof(wUrl) / sizeof(wUrl[0]),
                               L"http://127.0.0.1:8989/connect/onedrive?drive=%c", target);

                    SetWindowTextW(g_hStatusLabel, L"Opening browser to authenticate Microsoft OneDrive...");
                    ShellExecuteW(NULL, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
                    break;
                }

                case IDC_BTN_CONNECT_DROPBOX: {
                    char target = pick_next_free_drive_letter();
                    WCHAR wUrl[256];
                    swprintf_s(wUrl, sizeof(wUrl) / sizeof(wUrl[0]),
                               L"http://127.0.0.1:8989/connect/dropbox?drive=%c", target);

                    SetWindowTextW(g_hStatusLabel, L"Opening browser to authenticate Dropbox...");
                    ShellExecuteW(NULL, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
                    break;
                }

                case IDC_BTN_CONNECT_MEGA: {
                    char target = pick_next_free_drive_letter();
                    WCHAR wUrl[256];
                    swprintf_s(wUrl, sizeof(wUrl) / sizeof(wUrl[0]),
                               L"http://127.0.0.1:8989/connect/mega?drive=%c", target);

                    SetWindowTextW(g_hStatusLabel, L"Opening browser to authenticate Mega.nz...");
                    ShellExecuteW(NULL, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
                    break;
                }

                case IDC_BTN_CONNECT_WEBDAV: {
                    char target = pick_next_free_drive_letter();
                    WCHAR wUrl[256];
                    swprintf_s(wUrl, sizeof(wUrl) / sizeof(wUrl[0]),
                               L"http://127.0.0.1:8989/connect/webdav?drive=%c", target);

                    SetWindowTextW(g_hStatusLabel, L"Opening browser to authenticate WebDAV / NAS...");
                    ShellExecuteW(NULL, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
                    break;
                }

                case IDC_BTN_MOUNT_ALL: {
                    SetWindowTextW(g_hStatusLabel, L"Mounting all configured storage drives...");
                    int count = mount_all_auto(g_pConfig);
                    update_listview();
                    WCHAR buf[128];
                    swprintf_s(buf, sizeof(buf)/sizeof(buf[0]), L"Mounted %d storage drives successfully.", count);
                    SetWindowTextW(g_hStatusLabel, buf);
                    break;
                }

                case IDC_BTN_UNMOUNT_ALL: {
                    SetWindowTextW(g_hStatusLabel, L"Unmounting all drives...");
                    int count = unmount_all(g_pConfig);
                    update_listview();
                    WCHAR buf[128];
                    swprintf_s(buf, sizeof(buf)/sizeof(buf[0]), L"Unmounted %d drives.", count);
                    SetWindowTextW(g_hStatusLabel, buf);
                    break;
                }

                case IDC_BTN_EXPLORER: {
                    int sel = get_selected_account_index();
                    if (sel >= 0 && sel < g_pConfig->account_count) {
                        char letter = g_pConfig->accounts[sel].drive_letter;
                        mount_open_in_explorer(letter);
                    } else {
                        MessageBoxW(hWnd, L"Please select a drive from the list first by clicking on its row.", L"Tafim Drive", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }

                case IDC_BTN_TOGGLE_MOUNT: {
                    int sel = get_selected_account_index();
                    if (sel >= 0 && sel < g_pConfig->account_count) {
                        DriveAccount* acc = &g_pConfig->accounts[sel];
                        if (acc->state == MOUNT_STATE_MOUNTED) {
                            unmount_account(g_pConfig, sel);
                        } else {
                            mount_account(g_pConfig, sel);
                        }
                        update_listview();
                    } else {
                        MessageBoxW(hWnd, L"Please select a drive from the list first by clicking on its row.", L"Tafim Drive", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }

                case IDC_BTN_REMOVE: {
                    int sel = get_selected_account_index();
                    if (sel >= 0 && sel < g_pConfig->account_count) {
                        WCHAR prompt[256];
                        WCHAR wName[TAFIM_STR_LEN];
                        MultiByteToWideChar(CP_UTF8, 0, g_pConfig->accounts[sel].name, -1, wName, TAFIM_STR_LEN);
                        swprintf_s(prompt, sizeof(prompt)/sizeof(prompt[0]), L"Are you sure you want to disconnect '%s' (Drive %c:)?",
                                  wName, g_pConfig->accounts[sel].drive_letter);

                        if (MessageBoxW(hWnd, prompt, L"Confirm Disconnect", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                            unmount_account(g_pConfig, sel);
                            for (int i = sel; i < g_pConfig->account_count - 1; i++) {
                                g_pConfig->accounts[i] = g_pConfig->accounts[i + 1];
                            }
                            g_pConfig->account_count--;
                            config_save(g_pConfig);
                            update_listview();
                        }
                    } else {
                        MessageBoxW(hWnd, L"Please select a drive from the list first by clicking on its row.", L"Tafim Drive", MB_OK | MB_ICONINFORMATION);
                    }
                    break;
                }

                case IDC_BTN_REFRESH: {
                    update_listview();
                    SetWindowTextW(g_hStatusLabel, L"Drive status refreshed.");
                    break;
                }

                case IDC_BTN_BROWSE_CACHE: {
                    WCHAR szPath[MAX_PATH] = {0};
                    if (browse_for_folder(hWnd, szPath, MAX_PATH)) {
                        SetWindowTextW(g_hEditCachePath, szPath);
                    }
                    break;
                }

                case IDC_BTN_SAVE_CACHE: {
                    WCHAR wPath[MAX_PATH] = {0};
                    GetWindowTextW(g_hEditCachePath, wPath, MAX_PATH);
                    WCHAR wSize[32] = {0};
                    GetWindowTextW(g_hEditCacheSize, wSize, 32);

                    int new_size_gb = _wtoi(wSize);
                    if (new_size_gb <= 0) new_size_gb = 50;

                    char szPathA[MAX_PATH] = {0};
                    WideCharToMultiByte(CP_UTF8, 0, wPath, -1, szPathA, MAX_PATH, NULL, NULL);

                    if (szPathA[0] == '\0') {
                        MessageBoxW(hWnd, L"Cache directory path cannot be empty.", L"Invalid Path", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    CreateDirectoryA(szPathA, NULL);
                    strncpy_s(g_pConfig->cache_dir, sizeof(g_pConfig->cache_dir), szPathA, _TRUNCATE);
                    g_pConfig->cache_max_gb = new_size_gb;
                    config_save(g_pConfig);

                    WCHAR msg[256];
                    swprintf_s(msg, sizeof(msg)/sizeof(msg[0]), L"Offline Cache updated: %s (%d GB limit).", wPath, new_size_gb);
                    SetWindowTextW(g_hStatusLabel, msg);
                    MessageBoxW(hWnd, L"Offline cache folder path and size limit saved successfully!\n\nAll subsequent mounts and background syncs will use this folder.", L"Cache Settings Saved", MB_OK | MB_ICONINFORMATION);
                    break;
                }
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rcHeader = { 0, 0, 860, 65 };
            HBRUSH hHeaderBrush = CreateSolidBrush(RGB(11, 15, 25));
            FillRect(hdc, &rcHeader, hHeaderBrush);
            DeleteObject(hHeaderBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(56, 189, 248));
            SelectObject(hdc, g_hFontTitle);
            TextOutW(hdc, 24, 12, L"TAFIM DRIVE", 11);

            SetTextColor(hdc, RGB(148, 163, 184));
            SelectObject(hdc, g_hFontUI);
            TextOutW(hdc, 24, 38, L"Multi-Cloud SSD Storage System - TeraBox (1024 GB) - Google Drive - Multi-Account", 81);

            EndPaint(hWnd, &ps);
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(148, 163, 184));
            SetBkColor(hdcStatic, RGB(15, 23, 42));
            return (LRESULT)g_hDarkBgBrush;
        }

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORBTN: {
            return (LRESULT)g_hDarkBgBrush;
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONUP) {
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
            }
            break;
        }

        case WM_DESTROY: {
            KillTimer(hWnd, 1);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontUI) DeleteObject(g_hFontUI);
            if (g_hFontBtn) DeleteObject(g_hFontBtn);
            if (g_hDarkBgBrush) DeleteObject(g_hDarkBgBrush);
            server_stop();
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int gui_run(AppConfig* cfg, HINSTANCE hInstance, int nCmdShow) {
    g_pConfig = cfg;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    /* Start the background HTTP browser-auth server */
    server_start(cfg);

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
    wcex.lpszClassName = L"TafimDriveWindowClass";

    RegisterClassExW(&wcex);

    HWND hWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"TafimDriveWindowClass",
        L"Tafim Drive - Multi-Cloud Storage Manager",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 580,
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
