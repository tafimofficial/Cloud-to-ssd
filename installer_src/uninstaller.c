#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdbool.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

static void kill_processes_by_name(void) {
    /* Kill any active Tafim Drive or mount engine */
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmd[256];
    strcpy_s(cmd, sizeof(cmd), "taskkill /f /im \"Tafim Drive.exe\" /t");
    CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    WaitForSingleObject(pi.hProcess, 1000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

static void remove_registry_keys(void) {
    HKEY hRunKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hRunKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hRunKey, L"TafimDrive");
        RegCloseKey(hRunKey);
    }

    HKEY hUninstall;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                      0, KEY_ALL_ACCESS, &hUninstall) == ERROR_SUCCESS) {
        RegDeleteKeyW(hUninstall, L"TafimDrive");
        RegCloseKey(hUninstall);
    }
}

static void remove_shortcuts(void) {
    /* Desktop shortcut */
    WCHAR szDesktop[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, szDesktop))) {
        WCHAR szLnk[MAX_PATH];
        swprintf_s(szLnk, MAX_PATH, L"%s\\Tafim Drive.lnk", szDesktop);
        DeleteFileW(szLnk);
    }

    /* Start Menu shortcut */
    WCHAR szPrograms[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, szPrograms))) {
        WCHAR szLnk[MAX_PATH];
        swprintf_s(szLnk, MAX_PATH, L"%s\\Tafim Drive.lnk", szPrograms);
        DeleteFileW(szLnk);

        WCHAR szFolder[MAX_PATH];
        swprintf_s(szFolder, MAX_PATH, L"%s\\Tafim Drive", szPrograms);
        WCHAR szFolderLnk[MAX_PATH];
        swprintf_s(szFolderLnk, MAX_PATH, L"%s\\Tafim Drive\\Tafim Drive.lnk", szPrograms);
        DeleteFileW(szFolderLnk);
        RemoveDirectoryW(szFolder);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)pCmdLine; (void)nCmdShow;

    int res = MessageBoxW(NULL,
        L"Are you sure you want to completely uninstall Tafim Drive and disconnect all cloud storage drives?",
        L"Uninstall Tafim Drive",
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);

    if (res != IDYES) {
        return 0;
    }

    /* 1. Stop processes */
    kill_processes_by_name();

    /* 2. Remove registry keys */
    remove_registry_keys();

    /* 3. Remove shortcuts */
    remove_shortcuts();

    /* 4. Get current install directory */
    WCHAR szExePath[MAX_PATH];
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);
    WCHAR* last_slash = wcsrchr(szExePath, L'\\');
    if (last_slash) {
        *last_slash = L'\0';
    }

    /* 5. Launch self-deleting script for directory cleanup */
    WCHAR szCmd[MAX_PATH * 2];
    swprintf_s(szCmd, sizeof(szCmd)/sizeof(szCmd[0]),
               L"cmd.exe /c ping 127.0.0.1 -n 2 > nul & rmdir /s /q \"%s\"", szExePath);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    CreateProcessW(NULL, szCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    MessageBoxW(NULL,
        L"Tafim Drive has been successfully uninstalled from your computer.",
        L"Uninstall Complete",
        MB_OK | MB_ICONINFORMATION);

    return 0;
}
