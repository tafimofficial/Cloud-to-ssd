#include "tafim_drive.h"
#include <tlhelp32.h>
#include <shlobj.h>

#pragma comment(lib, "mpr.lib")
#pragma comment(lib, "shell32.lib")

bool mount_is_letter_in_use(char drive_letter) {
    char letter = (char)toupper(drive_letter);
    if (letter < 'A' || letter > 'Z') return true;

    DWORD mask = 1 << (letter - 'A');
    DWORD drives = GetLogicalDrives();
    return (drives & mask) != 0;
}

void mount_get_available_letters(char* out_letters, int max_count) {
    DWORD drives = GetLogicalDrives();
    int count = 0;
    /* Common cloud drive letters */
    for (char c = 'D'; c <= 'Z' && count + 1 < max_count; c++) {
        DWORD mask = 1 << (c - 'A');
        if (!(drives & mask)) {
            out_letters[count++] = c;
        }
    }
    out_letters[count] = '\0';
}

static bool is_process_running(DWORD pid) {
    if (pid == 0) return false;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(hProc, &exitCode) && exitCode == STILL_ACTIVE) {
        CloseHandle(hProc);
        return true;
    }
    CloseHandle(hProc);
    return false;
}

static void find_rclone_exe(char* out_path, size_t max_len) {
    const char* candidates[] = {
        "tools\\rclone\\rclone.exe",
        "t:\\Cloud-SSD\\tools\\rclone\\rclone.exe",
        "rclone.exe",
        "C:\\Users\\T\\AppData\\Local\\Programs\\CloudPlayX\\tools\\rclone\\rclone.exe"
    };

    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        if (file_exists(candidates[i])) {
            GetFullPathNameA(candidates[i], (DWORD)max_len, out_path, NULL);
            return;
        }
    }
    strncpy_s(out_path, max_len, "rclone.exe", _TRUNCATE);
}

static void find_rclone_conf(char* out_path, size_t max_len) {
    const char* candidates[] = {
        "rclone.conf",
        "t:\\Cloud-SSD\\rclone.conf",
        "C:\\Users\\T\\AppData\\Local\\CloudPlayX\\rclone.conf"
    };

    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        if (file_exists(candidates[i])) {
            GetFullPathNameA(candidates[i], (DWORD)max_len, out_path, NULL);
            return;
        }
    }

    char appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        snprintf(out_path, max_len, "%s\\rclone\\rclone.conf", appdata);
    } else {
        strncpy_s(out_path, max_len, "rclone.conf", _TRUNCATE);
    }
}

void mount_update_all_states(AppConfig* cfg) {
    for (int i = 0; i < cfg->account_count; i++) {
        DriveAccount* acc = &cfg->accounts[i];
        bool in_use = mount_is_letter_in_use(acc->drive_letter);
        bool proc_alive = is_process_running(acc->process_id);

        if (in_use && (proc_alive || acc->process_id == 0)) {
            acc->state = MOUNT_STATE_MOUNTED;
        } else if (proc_alive && !in_use) {
            acc->state = MOUNT_STATE_STARTING;
        } else {
            acc->state = MOUNT_STATE_DISCONNECTED;
            acc->process_id = 0;
        }
    }
}

bool mount_account(AppConfig* cfg, int account_index) {
    if (account_index < 0 || account_index >= cfg->account_count) return false;
    DriveAccount* acc = &cfg->accounts[account_index];

    char drive_str[4] = { (char)toupper(acc->drive_letter), ':', '\0', '\0' };

    /* If already mounted in Windows, ensure state is MOUNTED */
    if (mount_is_letter_in_use(acc->drive_letter)) {
        acc->state = MOUNT_STATE_MOUNTED;
        return true;
    }

    acc->state = MOUNT_STATE_STARTING;
    acc->last_error[0] = '\0';

    char rclone_exe[MAX_PATH];
    find_rclone_exe(rclone_exe, sizeof(rclone_exe));

    char rclone_conf[MAX_PATH];
    find_rclone_conf(rclone_conf, sizeof(rclone_conf));

    /* Prepare cache directory */
    char mount_cache[MAX_PATH];
    snprintf(mount_cache, sizeof(mount_cache), "%s\\m-%s", cfg->cache_dir, acc->id);
    CreateDirectoryA(cfg->cache_dir, NULL);
    CreateDirectoryA(mount_cache, NULL);

    /* Determine remote target */
    char remote_target[TAFIM_STR_LEN * 2];
    if (acc->provider == PROVIDER_TERABOX) {
        if (acc->remote_name[0] && strcmp(acc->remote_name, "terabox_main") != 0 && strcmp(acc->remote_name, "terabox_second") != 0) {
            snprintf(remote_target, sizeof(remote_target), "%s:", acc->remote_name);
        } else {
            /* Default to the proven terabox-chunked: backend with 1024 GB volume capacity */
            strcpy_s(remote_target, sizeof(remote_target), "terabox-chunked:");
        }
    } else if (acc->provider == PROVIDER_GDRIVE) {
        snprintf(remote_target, sizeof(remote_target), "%s:", acc->remote_name[0] ? acc->remote_name : "gdrive");
    } else if (acc->provider == PROVIDER_ONEDRIVE) {
        snprintf(remote_target, sizeof(remote_target), "%s:", acc->remote_name[0] ? acc->remote_name : "onedrive");
    } else if (acc->provider == PROVIDER_DROPBOX) {
        snprintf(remote_target, sizeof(remote_target), "%s:", acc->remote_name[0] ? acc->remote_name : "dropbox");
    } else if (acc->provider == PROVIDER_MEGA) {
        snprintf(remote_target, sizeof(remote_target), "%s:", acc->remote_name[0] ? acc->remote_name : "mega");
    } else if (acc->provider == PROVIDER_WEBDAV) {
        snprintf(remote_target, sizeof(remote_target), "%s:", acc->remote_name[0] ? acc->remote_name : "webdav");
    } else {
        snprintf(remote_target, sizeof(remote_target), "%s:", acc->remote_name[0] ? acc->remote_name : "remote");
    }

    /* Format clean Volume Label for Windows Explorer */
    char volname[128];
    snprintf(volname, sizeof(volname), "%s", acc->name[0] ? acc->name : "TeraBox Cloud SSD");

    /* Format dynamic total disk space flag */
    char total_size_flag[64] = "";
    if (acc->quota_total_gb > 0) {
        snprintf(total_size_flag, sizeof(total_size_flag), "--vfs-disk-space-total-size %.0fG", acc->quota_total_gb);
    } else {
        strcpy_s(total_size_flag, sizeof(total_size_flag), "--vfs-disk-space-total-size 1024G");
    }

    /*
     * Build rclone mount command line:
     * - Uses WinFsp (FUSE-rclone) as a FIXED local disk volume
     * - Reports exact dynamic volume capacity to Windows
     * - Enables --vfs-used-is-size so used space updates dynamically as files are added
     * - Windows Explorer renders the blue storage bar and "X GB free of 1024 GB"
     * - Full offline caching support via --vfs-cache-mode full
     */
    int max_cache = (cfg->cache_max_gb > 0) ? cfg->cache_max_gb : 50;
    char cmd[MAX_PATH * 4];
    snprintf(cmd, sizeof(cmd),
        "\"%s\" mount %s %s "
        "--config \"%s\" "
        "--vfs-cache-mode full "
        "--cache-dir \"%s\" "
        "--vfs-cache-max-size %dGi "
        "--vfs-cache-max-age 8760h "
        "--buffer-size 0 "
        "--file-perms 0777 "
        "--dir-perms 0777 "
        "--volname \"%s\" "
        "%s "
        "--vfs-used-is-size "
        "--no-modtime",
        rclone_exe, remote_target, drive_str, rclone_conf, mount_cache, max_cache, volname, total_size_flag);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    DWORD creationFlags = CREATE_NO_WINDOW | DETACHED_PROCESS;
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, creationFlags, NULL, NULL, &si, &pi)) {
        acc->process_id = pi.dwProcessId;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        /* Wait up to 5 seconds for the drive to appear with filesystem in Windows */
        for (int wait = 0; wait < 50; wait++) {
            Sleep(100);
            if (mount_is_letter_in_use(acc->drive_letter)) {
                acc->state = MOUNT_STATE_MOUNTED;
                config_save(cfg);
                return true;
            }
        }

        /* Check if process is still running */
        if (is_process_running(acc->process_id)) {
            acc->state = MOUNT_STATE_MOUNTED;
            config_save(cfg);
            return true;
        }
    }

    acc->state = MOUNT_STATE_ERROR;
    strcpy_s(acc->last_error, sizeof(acc->last_error), "Failed to start rclone mount with WinFsp.");
    return false;
}

bool unmount_account(AppConfig* cfg, int account_index) {
    if (account_index < 0 || account_index >= cfg->account_count) return false;
    DriveAccount* acc = &cfg->accounts[account_index];

    char drive_str[4] = { (char)toupper(acc->drive_letter), ':', '\0', '\0' };

    /* Terminate mount process if running */
    if (acc->process_id != 0) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, acc->process_id);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
        }
        acc->process_id = 0;
    }

    /* Cancel any network connection */
    WNetCancelConnection2A(drive_str, 0, TRUE);

    /* Clean up any legacy DOS device definition */
    DefineDosDeviceA(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE, drive_str, NULL);
    DefineDosDeviceA(DDD_REMOVE_DEFINITION, drive_str, NULL);

    acc->state = MOUNT_STATE_DISCONNECTED;
    config_save(cfg);
    return true;
}

int mount_all_auto(AppConfig* cfg) {
    int count = 0;
    for (int i = 0; i < cfg->account_count; i++) {
        if (cfg->accounts[i].auto_mount || cfg->accounts[i].state == MOUNT_STATE_MOUNTED) {
            if (mount_account(cfg, i)) {
                count++;
            }
        }
    }
    return count;
}

int unmount_all(AppConfig* cfg) {
    int count = 0;
    for (int i = 0; i < cfg->account_count; i++) {
        if (unmount_account(cfg, i)) {
            count++;
        }
    }
    return count;
}

bool mount_open_in_explorer(char drive_letter) {
    char path[8];
    snprintf(path, sizeof(path), "%c:\\", (char)toupper(drive_letter));
    HINSTANCE res = ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)res > 32);
}
