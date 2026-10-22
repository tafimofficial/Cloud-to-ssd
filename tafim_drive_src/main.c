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
    AppConfig cfg;
    config_load(&cfg);
    mount_update_all_states(&cfg);

    /* If CLI arguments were provided, attach to caller's console */
    if (pCmdLine && *pCmdLine) {
        bool hasConsole = false;
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            hasConsole = true;
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            freopen_s(&fp, "CONIN$", "r", stdin);
        }

        if (wcsstr(pCmdLine, L"--help") || wcsstr(pCmdLine, L"-h") || wcsstr(pCmdLine, L"/?")) {
            print_help();
            return 0;
        }

        if (wcsstr(pCmdLine, L"--status")) {
            ui_show_banner();
            ui_show_status_table(&cfg);
            return 0;
        }

        if (wcsstr(pCmdLine, L"--cli")) {
            if (!hasConsole) AllocConsole();
            ui_menu_loop(&cfg);
            return 0;
        }

        if (wcsstr(pCmdLine, L"--mount-all")) {
            printf("Mounting all configured drives...\n");
            int n = mount_all_auto(&cfg);
            printf("Mounted %d drives.\n", n);
            return 0;
        }

        if (wcsstr(pCmdLine, L"--unmount-all")) {
            printf("Unmounting all drives...\n");
            int n = unmount_all(&cfg);
            printf("Unmounted %d drives.\n", n);
            return 0;
        }

        const wchar_t* pMount = wcsstr(pCmdLine, L"--mount");
        if (pMount && !wcsstr(pCmdLine, L"--mount-all")) {
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

        const wchar_t* pUnmount = wcsstr(pCmdLine, L"--unmount");
        if (pUnmount && !wcsstr(pCmdLine, L"--unmount-all")) {
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

        if (wcsstr(pCmdLine, L"--test-terabox")) {
            ui_wizard_test_terabox(&cfg);
            return 0;
        }

        if (wcsstr(pCmdLine, L"--boot") || wcsstr(pCmdLine, L"--startup") || wcsstr(pCmdLine, L"--silent")) {
            mount_all_auto(&cfg);
            return gui_run(&cfg, hInstance, SW_HIDE);
        }
    }

    /* By default (e.g. double-click), launch the GUI with NO background terminal! */
    return gui_run(&cfg, hInstance, nCmdShow);
}
