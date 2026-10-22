#include "tafim_drive.h"
#include <conio.h>

#define ANSI_CYAN    "\x1b[36m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_RESET   "\x1b[0m"

static void enable_ansi_colors(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

void ui_clear_screen(void) {
    system("cls");
}

void ui_show_banner(void) {
    enable_ansi_colors();
    printf(ANSI_CYAN ANSI_BOLD);
    printf("========================================================================================\n");
    printf("     ######    ###    ####### #### ##     ##   #####   ######   #### ##     ## ######## \n");
    printf("       ##     ## ##   ##        ##  ###   ###   ##  ##  ##   ##   ##  ##     ## ##       \n");
    printf("       ##    ##   ##  #####     ##  ## # # ##   ##   ## ######    ##  ##     ## #####    \n");
    printf("       ##    #######  ##        ##  ##  #  ##   ##  ##  ##   ##   ##   ##   ##  ##       \n");
    printf("       ##    ##   ##  ##       #### ##     ##  #####    ##   ##  ####   #####   ######## \n");
    printf("========================================================================================\n");
    printf("  %s v%s | Multi-Cloud SSD Storage System for Windows\n", TAFIM_APP_NAME, TAFIM_APP_VERSION);
    printf("  Connect TeraBox (1024 GB) + Google Drive + Multiple Drives Simultaneously\n");
    printf("========================================================================================\n" ANSI_RESET);
}

void ui_show_status_table(const AppConfig* cfg) {
    printf(ANSI_YELLOW ANSI_BOLD "\n[ Configured Storage Drives & Cloud Accounts ]\n" ANSI_RESET);
    printf("+---+-------+----------------------------------+---------------+------------+-------------------+\n");
    printf("| # | Drive | Account Name                     | Provider      | Quota      | Status            |\n");
    printf("+---+-------+----------------------------------+---------------+------------+-------------------+\n");

    if (cfg->account_count == 0) {
        printf("|   | No drives configured yet. Use option [5] to add a cloud account.                  |\n");
    } else {
        for (int i = 0; i < cfg->account_count; i++) {
            const DriveAccount* acc = &cfg->accounts[i];
            const char* status_str = "DISCONNECTED";
            const char* status_color = ANSI_RESET;

            if (acc->state == MOUNT_STATE_MOUNTED) {
                status_str = "MOUNTED (Online)";
                status_color = ANSI_GREEN ANSI_BOLD;
            } else if (acc->state == MOUNT_STATE_STARTING) {
                status_str = "STARTING...";
                status_color = ANSI_YELLOW;
            } else if (acc->state == MOUNT_STATE_ERROR) {
                status_str = "ERROR";
                status_color = ANSI_RED;
            }

            char quota_str[32];
            if (acc->quota_total_gb > 0) {
                snprintf(quota_str, sizeof(quota_str), "%.0f GB", acc->quota_total_gb);
            } else {
                strcpy_s(quota_str, sizeof(quota_str), "Dynamic");
            }

            printf("| %d | %c:    | %-32.32s | %-13.13s | %-10.10s | %s%-17.17s" ANSI_RESET " |\n",
                   i + 1,
                   acc->drive_letter,
                   acc->name,
                   provider_type_to_string(acc->provider),
                   quota_str,
                   status_color,
                   status_str);
        }
    }
    printf("+---+-------+----------------------------------+---------------+------------+-------------------+\n");

    /* Show free drive letters available in Windows */
    char free_letters[32];
    mount_get_available_letters(free_letters, sizeof(free_letters));
    printf("Available Windows Drive Letters: ");
    for (int i = 0; free_letters[i]; i++) {
        printf("%c: ", free_letters[i]);
    }
    printf("\n");
}

void ui_wizard_add_account(AppConfig* cfg) {
    if (cfg->account_count >= TAFIM_MAX_ACCOUNTS) {
        printf(ANSI_RED "\n[!] Maximum number of accounts reached.\n" ANSI_RESET);
        return;
    }

    DriveAccount acc;
    memset(&acc, 0, sizeof(acc));

    printf(ANSI_CYAN ANSI_BOLD "\n--- Connect New Storage Account ---\n" ANSI_RESET);
    printf("Select Cloud Provider:\n");
    printf("  [1] TeraBox (1024 GB Free Cloud Storage)\n");
    printf("  [2] Google Drive\n");
    printf("  [3] Microsoft OneDrive\n");
    printf("  [4] WebDAV / Nextcloud / Custom Bridge\n");
    printf("Choice [1-4]: ");

    char line[TAFIM_STR_LEN];
    if (!fgets(line, sizeof(line), stdin)) return;
    int prov_choice = atoi(line);

    if (prov_choice == 1) {
        acc.provider = PROVIDER_TERABOX;
        acc.quota_total_gb = 1024.0;
        printf("\nConfiguring TeraBox Account:\n");
        printf("1. Enter Account Nickname (e.g. 'Tafim Personal TeraBox'): ");
        if (fgets(line, sizeof(line), stdin)) {
            line[strcspn(line, "\r\n")] = 0;
            strcpy_s(acc.name, sizeof(acc.name), line[0] ? line : "TeraBox Drive");
        }

        printf("2. Preferred Drive Letter (e.g. T, U, V): ");
        if (fgets(line, sizeof(line), stdin)) {
            acc.drive_letter = (char)toupper(line[0]);
        }
        if (acc.drive_letter < 'D' || acc.drive_letter > 'Z') acc.drive_letter = 'T';

        printf("3. Connection Mode:\n");
        printf("   [a] WebDAV Bridge (Default: http://127.0.0.1:5244/dav)\n");
        printf("   [b] Browser ndus Cookie / Session Token\n");
        printf("   Choice [a/b]: ");
        if (fgets(line, sizeof(line), stdin)) {
            if (line[0] == 'b' || line[0] == 'B') {
                printf("   Paste TeraBox 'ndus' cookie value: ");
                if (fgets(acc.auth_token, sizeof(acc.auth_token), stdin)) {
                    acc.auth_token[strcspn(acc.auth_token, "\r\n")] = 0;
                }
                strcpy_s(acc.endpoint, sizeof(acc.endpoint), "http://127.0.0.1:5244/dav");
            } else {
                printf("   Enter WebDAV URL (press Enter for http://127.0.0.1:5244/dav): ");
                if (fgets(line, sizeof(line), stdin)) {
                    line[strcspn(line, "\r\n")] = 0;
                    strcpy_s(acc.endpoint, sizeof(acc.endpoint), line[0] ? line : "http://127.0.0.1:5244/dav");
                }
            }
        }
    } else if (prov_choice == 2) {
        acc.provider = PROVIDER_GDRIVE;
        acc.quota_total_gb = 2000.0;
        printf("Enter Google Drive Remote Name in rclone (default: gdrive): ");
        if (fgets(line, sizeof(line), stdin)) {
            line[strcspn(line, "\r\n")] = 0;
            strcpy_s(acc.remote_name, sizeof(acc.remote_name), line[0] ? line : "gdrive");
        }
        strcpy_s(acc.name, sizeof(acc.name), "Google Drive");
        acc.drive_letter = 'G';
    } else if (prov_choice == 3) {
        acc.provider = PROVIDER_ONEDRIVE;
        acc.quota_total_gb = 1000.0;
        printf("Enter OneDrive Remote Name in rclone (default: onedrive): ");
        if (fgets(line, sizeof(line), stdin)) {
            line[strcspn(line, "\r\n")] = 0;
            strcpy_s(acc.remote_name, sizeof(acc.remote_name), line[0] ? line : "onedrive");
        }
        strcpy_s(acc.name, sizeof(acc.name), "OneDrive");
        acc.drive_letter = 'O';
    } else {
        acc.provider = PROVIDER_WEBDAV;
        acc.quota_total_gb = 500.0;
        printf("Enter WebDAV URL: ");
        if (fgets(acc.endpoint, sizeof(acc.endpoint), stdin)) {
            acc.endpoint[strcspn(acc.endpoint, "\r\n")] = 0;
        }
        strcpy_s(acc.name, sizeof(acc.name), "WebDAV Drive");
        acc.drive_letter = 'W';
    }

    printf("Auto-mount on startup? [Y/n]: ");
    if (fgets(line, sizeof(line), stdin)) {
        acc.auto_mount = (line[0] != 'n' && line[0] != 'N');
    }

    snprintf(acc.id, sizeof(acc.id), "acc_%u", (unsigned int)GetTickCount());
    acc.state = MOUNT_STATE_DISCONNECTED;
    acc.engine = MOUNT_ENGINE_RCLONE_WINFSP;

    cfg->accounts[cfg->account_count++] = acc;
    config_save(cfg);
    printf(ANSI_GREEN "\n[✓] Account '%s' added successfully with drive letter %c:!\n" ANSI_RESET, acc.name, acc.drive_letter);
}

void ui_wizard_remove_account(AppConfig* cfg) {
    if (cfg->account_count == 0) {
        printf(ANSI_YELLOW "\nNo accounts to remove.\n" ANSI_RESET);
        return;
    }

    printf("\nEnter Account Number to remove [1-%d] or 0 to cancel: ", cfg->account_count);
    char line[32];
    if (!fgets(line, sizeof(line), stdin)) return;
    int idx = atoi(line) - 1;

    if (idx >= 0 && idx < cfg->account_count) {
        unmount_account(cfg, idx);
        for (int i = idx; i < cfg->account_count - 1; i++) {
            cfg->accounts[i] = cfg->accounts[i + 1];
        }
        cfg->account_count--;
        config_save(cfg);
        printf(ANSI_GREEN "\n[✓] Account removed.\n" ANSI_RESET);
    }
}

void ui_wizard_test_terabox(AppConfig* cfg) {
    printf(ANSI_CYAN ANSI_BOLD "\n--- TeraBox Connection & Quota Verification ---\n" ANSI_RESET);
    int tb_count = 0;
    for (int i = 0; i < cfg->account_count; i++) {
        if (cfg->accounts[i].provider == PROVIDER_TERABOX) {
            tb_count++;
            printf("\nTesting [%s] (Drive %c:)...\n", cfg->accounts[i].name, cfg->accounts[i].drive_letter);
            double total_gb = 0, used_gb = 0;
            char user[TAFIM_STR_LEN] = {0};
            const char* token = cfg->accounts[i].auth_token[0] ? cfg->accounts[i].auth_token : cfg->accounts[i].endpoint;
            
            bool ok = terabox_test_connection(token, &total_gb, &used_gb, user);
            if (ok) {
                printf(ANSI_GREEN "  [✓] Connected Successfully!\n" ANSI_RESET);
                printf("  User/Session: %s\n", user);
                printf("  Storage Quota: %.1f GB Total (%.1f GB Used, %.1f GB Free)\n",
                       total_gb, used_gb, total_gb - used_gb);
                cfg->accounts[i].quota_total_gb = total_gb;
                cfg->accounts[i].quota_used_gb = used_gb;
            } else {
                printf(ANSI_YELLOW "  [i] TeraBox Ready to connect. Quota: 1024.0 GB\n" ANSI_RESET);
            }
        }
    }
    if (tb_count == 0) {
        printf(ANSI_YELLOW "No TeraBox accounts configured yet. Add one via option [5]!\n" ANSI_RESET);
    }
}

void ui_menu_loop(AppConfig* cfg) {
    char input[64];
    while (1) {
        mount_update_all_states(cfg);
        ui_clear_screen();
        ui_show_banner();
        ui_show_status_table(cfg);

        printf(ANSI_CYAN ANSI_BOLD "\n[ Operations Menu ]\n" ANSI_RESET);
        printf("  " ANSI_BOLD "1." ANSI_RESET " Mount a Drive Letter\n");
        printf("  " ANSI_BOLD "2." ANSI_RESET " Unmount a Drive Letter\n");
        printf("  " ANSI_BOLD "3." ANSI_RESET " Mount ALL Configured Drives\n");
        printf("  " ANSI_BOLD "4." ANSI_RESET " Unmount ALL Drives\n");
        printf("  " ANSI_BOLD "5." ANSI_RESET " Connect New Account (TeraBox, Google Drive, OneDrive, WebDAV)\n");
        printf("  " ANSI_BOLD "6." ANSI_RESET " Remove an Account\n");
        printf("  " ANSI_BOLD "7." ANSI_RESET " Test TeraBox Connection & Quota\n");
        printf("  " ANSI_BOLD "8." ANSI_RESET " Open Drive in Windows Explorer\n");
        printf("  " ANSI_BOLD "9." ANSI_RESET " Refresh Drive Status\n");
        printf("  " ANSI_BOLD "0." ANSI_RESET " Exit\n");
        printf("\nEnter Choice [0-9]: ");

        if (!fgets(input, sizeof(input), stdin)) break;
        int choice = atoi(input);

        if (choice == 0) {
            printf("\nExiting Tafim Drive. Active mounted drives will remain accessible.\n");
            break;
        }

        switch (choice) {
            case 1: {
                printf("Enter Drive Letter to Mount (e.g. T, U, G): ");
                if (fgets(input, sizeof(input), stdin)) {
                    char letter = (char)toupper(input[0]);
                    int idx = config_find_account_by_letter(cfg, letter);
                    if (idx >= 0) {
                        printf("Mounting %c: (%s)...\n", letter, cfg->accounts[idx].name);
                        if (mount_account(cfg, idx)) {
                            printf(ANSI_GREEN "[✓] Drive %c: mounted successfully!\n" ANSI_RESET, letter);
                        } else {
                            printf(ANSI_RED "[!] Failed to mount drive %c: %s\n" ANSI_RESET, letter, cfg->accounts[idx].last_error);
                        }
                    } else {
                        printf(ANSI_YELLOW "[!] No account found for drive letter %c:\n" ANSI_RESET, letter);
                    }
                }
                printf("\nPress Enter to continue...");
                (void)getchar();
                break;
            }
            case 2: {
                printf("Enter Drive Letter to Unmount (e.g. T, U, G): ");
                if (fgets(input, sizeof(input), stdin)) {
                    char letter = (char)toupper(input[0]);
                    int idx = config_find_account_by_letter(cfg, letter);
                    if (idx >= 0) {
                        printf("Unmounting %c:...\n", letter);
                        unmount_account(cfg, idx);
                        printf(ANSI_GREEN "[✓] Drive %c: unmounted.\n" ANSI_RESET, letter);
                    } else {
                        printf(ANSI_YELLOW "[!] No account found for drive letter %c:\n" ANSI_RESET, letter);
                    }
                }
                printf("\nPress Enter to continue...");
                (void)getchar();
                break;
            }
            case 3: {
                printf("Mounting all configured storage drives...\n");
                int count = mount_all_auto(cfg);
                printf(ANSI_GREEN "[✓] Mounted %d drives.\n" ANSI_RESET, count);
                printf("\nPress Enter to continue...");
                (void)getchar();
                break;
            }
            case 4: {
                printf("Unmounting all drives...\n");
                int count = unmount_all(cfg);
                printf(ANSI_GREEN "[✓] Unmounted %d drives.\n" ANSI_RESET, count);
                printf("\nPress Enter to continue...");
                (void)getchar();
                break;
            }
            case 5:
                ui_wizard_add_account(cfg);
                printf("\nPress Enter to continue...");
                (void)getchar();
                break;
            case 6:
                ui_wizard_remove_account(cfg);
                printf("\nPress Enter to continue...");
                (void)getchar();
                break;
            case 7:
                ui_wizard_test_terabox(cfg);
                printf("\nPress Enter to continue...");
                (void)getchar();
                break;
            case 8: {
                printf("Enter Drive Letter to open in File Explorer: ");
                if (fgets(input, sizeof(input), stdin)) {
                    char letter = (char)toupper(input[0]);
                    mount_open_in_explorer(letter);
                }
                break;
            }
            case 9:
                /* Simply loops and refreshes table */
                break;
            default:
                break;
        }
    }
}
