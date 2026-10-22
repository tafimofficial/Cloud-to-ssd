#include "tafim_drive.h"

static void print_help(void) {
    printf("Tafim Drive - Multi-Cloud Storage Mount Manager for Windows\n");
    printf("Usage: \"Tafim Drive.exe\" [options]\n\n");
    printf("Options:\n");
    printf("  (no args)              Launch the Modern Desktop GUI Application\n");
    printf("  --cli                  Launch Interactive Terminal Menu\n");
    printf("  --status               Show current status of all storage drives\n");
    printf("  --mount <letter>       Mount specific drive letter (e.g. --mount T:)\n");
    printf("  --unmount <letter>     Unmount specific drive letter (e.g. --unmount T:)\n");
    printf("  --mount-all            Mount all configured drives\n");
    printf("  --unmount-all          Unmount all connected drives\n");
    printf("  --open <letter>        Open mounted drive in Windows File Explorer\n");
    printf("  --test-terabox         Test TeraBox connectivity and quota\n");
    printf("  --help                 Show this help screen\n");
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;

    /* Always ensure Current Working Directory is the directory containing Tafim Drive.exe */
    WCHAR szExePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, szExePath, MAX_PATH)) {
        WCHAR* pLastSlash = wcsrchr(szExePath, L'\\');
        if (pLastSlash) {
            *pLastSlash = L'\0';
            SetCurrentDirectoryW(szExePath);
        }
    }

    AppConfig cfg;
    config_load(&cfg);
    mount_update_all_states(&cfg);

    PWSTR fullCmd = GetCommandLineW();
    const wchar_t* pArgs = (pCmdLine && *pCmdLine) ? pCmdLine : fullCmd;

    if (wcsstr(fullCmd, L"--boot") || wcsstr(fullCmd, L"--startup") || wcsstr(fullCmd, L"--silent")) {
        mount_all_auto(&cfg);
        return gui_run(&cfg, hInstance, SW_HIDE);
    }

    /* If CLI arguments were provided, attach to caller's console */
    if (wcsstr(pArgs, L"--") || wcsstr(pArgs, L"/?")) {
        bool hasConsole = false;
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            hasConsole = true;
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            freopen_s(&fp, "CONIN$", "r", stdin);
        }

        if (wcsstr(pArgs, L"--help") || wcsstr(pArgs, L"-h") || wcsstr(pArgs, L"/?")) {
            print_help();
            return 0;
        }

        if (wcsstr(pArgs, L"--status")) {
            ui_show_banner();
            ui_show_status_table(&cfg);
            return 0;
        }

        if (wcsstr(pArgs, L"--cli")) {
            if (!hasConsole) AllocConsole();
            ui_menu_loop(&cfg);
            return 0;
        }

        if (wcsstr(pArgs, L"--mount-all")) {
            printf("Mounting all configured drives...\n");
            int n = mount_all_auto(&cfg);
            printf("Mounted %d drives.\n", n);
            return 0;
        }

        if (wcsstr(pArgs, L"--unmount-all")) {
            printf("Unmounting all drives...\n");
            int n = unmount_all(&cfg);
            printf("Unmounted %d drives.\n", n);
            return 0;
        }

        const wchar_t* pMount = wcsstr(pArgs, L"--mount");
        if (pMount && !wcsstr(pArgs, L"--mount-all")) {
            pMount += 7;
            while (*pMount == L' ') pMount++;
            if (*pMount) {
                char letter = (char)toupper((char)*pMount);
                int idx = config_find_account_by_letter(&cfg, letter);
                if (idx >= 0) {
                    printf("Mounting drive %c: (%s)...\n", letter, cfg.accounts[idx].name);
                    if (mount_account(&cfg, idx)) {
                        printf("Success! Drive %c: is now online.\n", letter);
                        return 0;
                    } else {
                        printf("Failed to mount %c:: %s\n", letter, cfg.accounts[idx].last_error);
                        return 1;
                    }
                }
            }
        }

        const wchar_t* pUnmount = wcsstr(pArgs, L"--unmount");
        if (pUnmount && !wcsstr(pArgs, L"--unmount-all")) {
            pUnmount += 9;
            while (*pUnmount == L' ') pUnmount++;
            if (*pUnmount) {
                char letter = (char)toupper((char)*pUnmount);
                int idx = config_find_account_by_letter(&cfg, letter);
                if (idx >= 0) {
                    unmount_account(&cfg, idx);
                    printf("Drive %c: unmounted successfully.\n", letter);
                    return 0;
                }
            }
        }

        if (wcsstr(pArgs, L"--test-terabox")) {
            ui_wizard_test_terabox(&cfg);
            return 0;
        }
    }

    /* Single instance guard for GUI: bring existing window to front */
    HANDLE hSingleMutex = CreateMutexW(NULL, FALSE, L"Local\\TafimDrive_SingleInstance_Mutex_v1");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"TafimDriveWindowClass", NULL);
        if (hExisting) {
            ShowWindow(hExisting, SW_RESTORE);
            SetForegroundWindow(hExisting);
        }
        if (hSingleMutex) CloseHandle(hSingleMutex);
        return 0;
    }

    int res = gui_run(&cfg, hInstance, nCmdShow);
    if (hSingleMutex) CloseHandle(hSingleMutex);
    return res;
}
