#include "tafim_drive.h"
#include <tlhelp32.h>
#include <shlobj.h>
#include <stdarg.h>

#pragma comment(lib, "mpr.lib")
#pragma comment(lib, "shell32.lib")

static void log_mount_debug(const char* fmt, ...) {
    char local_appdata[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata))) return;
    char log_path[MAX_PATH];
    snprintf(log_path, sizeof(log_path), "%s\\TafimDrive\\mount_debug.log", local_appdata);
    FILE* dbg = fopen(log_path, "a");
    if (!dbg) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(dbg, fmt, args);
    va_end(args);
    fclose(dbg);
}

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

static HANDLE g_hMountProcesses[TAFIM_MAX_ACCOUNTS] = { 0 };

static void get_app_dir(char* out_dir, size_t max_len) {
    if (GetModuleFileNameA(NULL, out_dir, (DWORD)max_len)) {
        char* last = strrchr(out_dir, '\\');
        if (last) {
            *last = '\0';
            return;
        }
    }
    out_dir[0] = '\0';
}

static bool is_process_running(HANDLE hProc, DWORD pid) {
    if (hProc) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(hProc, &exitCode) && exitCode == STILL_ACTIVE) {
            return true;
        }
        return false;
    }
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD exitCode = 0;
    bool alive = (GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE);
    CloseHandle(h);
    return alive;
}

static void find_cpx_exe(char* out_path, size_t max_len) {
    char app_dir[MAX_PATH];
    get_app_dir(app_dir, sizeof(app_dir));

    char p1[MAX_PATH];
    snprintf(p1, sizeof(p1), "%s\\cpx.exe", app_dir);
    if (file_exists(p1)) { strncpy_s(out_path, max_len, p1, _TRUNCATE); return; }

    char p2[MAX_PATH];
    snprintf(p2, sizeof(p2), "%s\\tools\\cpx.exe", app_dir);
    if (file_exists(p2)) { strncpy_s(out_path, max_len, p2, _TRUNCATE); return; }

    char local_appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata))) {
        char p3[MAX_PATH];
        snprintf(p3, sizeof(p3), "%s\\Programs\\Tafim Drive\\cpx.exe", local_appdata);
        if (file_exists(p3)) { strncpy_s(out_path, max_len, p3, _TRUNCATE); return; }
        snprintf(p3, sizeof(p3), "%s\\Programs\\CloudPlayX\\cpx.exe", local_appdata);
        if (file_exists(p3)) { strncpy_s(out_path, max_len, p3, _TRUNCATE); return; }
    }

    if (file_exists("cpx.exe")) { GetFullPathNameA("cpx.exe", (DWORD)max_len, out_path, NULL); return; }
    if (file_exists("tools\\cpx.exe")) { GetFullPathNameA("tools\\cpx.exe", (DWORD)max_len, out_path, NULL); return; }

    out_path[0] = '\0';
}

static bool is_port_listening(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port = htons((u_short)port);
    unsigned long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
    connect(s, (struct sockaddr*)&sin, sizeof(sin));
    fd_set writes;
    FD_ZERO(&writes);
    FD_SET(s, &writes);
    struct timeval tv = { 0, 300000 };
    int res = select(0, NULL, &writes, NULL, &tv);
    closesocket(s);
    return (res > 0);
}

static void ensure_terabox_bridge(void) {
    if (is_port_listening(61869)) {
        return; /* Already running and ready */
    }

    char cpx_exe[MAX_PATH];
    find_cpx_exe(cpx_exe, sizeof(cpx_exe));
    if (!cpx_exe[0]) return;

    /* Check home directory: prefer TafimDrive, fallback to CloudPlayX */
    char local_appdata[MAX_PATH];
    char home_dir[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata))) {
        snprintf(home_dir, sizeof(home_dir), "%s\\TafimDrive", local_appdata);
        char testKey[MAX_PATH];
        snprintf(testKey, sizeof(testKey), "%s\\terabox.bridge.key", home_dir);
        if (!file_exists(testKey)) {
            snprintf(home_dir, sizeof(home_dir), "%s\\CloudPlayX", local_appdata);
        }
    } else {
        strcpy_s(home_dir, sizeof(home_dir), ".");
    }

    char cmd[MAX_PATH * 3];
    snprintf(cmd, sizeof(cmd), "\"%s\" terabox-bridge --port 61869 --home \"%s\"", cpx_exe, home_dir);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        /* Give bridge up to 2 seconds to bind */
        for (int i = 0; i < 20; i++) {
            Sleep(100);
            if (is_port_listening(61869)) break;
        }
    }
}

static void find_rclone_exe(char* out_path, size_t max_len) {
    char app_dir[MAX_PATH];
    get_app_dir(app_dir, sizeof(app_dir));

    char p1[MAX_PATH];
    snprintf(p1, sizeof(p1), "%s\\rclone.exe", app_dir);
    if (file_exists(p1)) {
        strncpy_s(out_path, max_len, p1, _TRUNCATE);
        return;
    }

    char p2[MAX_PATH];
    snprintf(p2, sizeof(p2), "%s\\tools\\rclone\\rclone.exe", app_dir);
    if (file_exists(p2)) {
        strncpy_s(out_path, max_len, p2, _TRUNCATE);
        return;
    }

    char local_appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata))) {
        char p3[MAX_PATH];
        snprintf(p3, sizeof(p3), "%s\\Programs\\Tafim Drive\\rclone.exe", local_appdata);
        if (file_exists(p3)) { strncpy_s(out_path, max_len, p3, _TRUNCATE); return; }
        snprintf(p3, sizeof(p3), "%s\\Programs\\CloudPlayX\\tools\\rclone\\rclone.exe", local_appdata);
        if (file_exists(p3)) { strncpy_s(out_path, max_len, p3, _TRUNCATE); return; }
    }

    const char* candidates[] = {
        "tools\\rclone\\rclone.exe",
        "tools\\rclone.exe",
        "rclone.exe"
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
    char app_dir[MAX_PATH];
    get_app_dir(app_dir, sizeof(app_dir));

    char p1[MAX_PATH];
    snprintf(p1, sizeof(p1), "%s\\rclone.conf", app_dir);
    if (file_exists(p1)) {
        strncpy_s(out_path, max_len, p1, _TRUNCATE);
        return;
    }

    char local_appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata))) {
        char p2[MAX_PATH];
        snprintf(p2, sizeof(p2), "%s\\Programs\\Tafim Drive\\rclone.conf", local_appdata);
        if (file_exists(p2)) { strncpy_s(out_path, max_len, p2, _TRUNCATE); return; }
        snprintf(p2, sizeof(p2), "%s\\TafimDrive\\rclone.conf", local_appdata);
        if (file_exists(p2)) { strncpy_s(out_path, max_len, p2, _TRUNCATE); return; }
        snprintf(p2, sizeof(p2), "%s\\CloudPlayX\\rclone.conf", local_appdata);
        if (file_exists(p2)) { strncpy_s(out_path, max_len, p2, _TRUNCATE); return; }
    }

    const char* candidates[] = {
        "rclone.conf"
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
        HANDLE hProc = (i < TAFIM_MAX_ACCOUNTS) ? g_hMountProcesses[i] : NULL;
        bool proc_alive = is_process_running(hProc, acc->process_id);

        if (in_use) {
            acc->state = MOUNT_STATE_MOUNTED;
        } else if (proc_alive) {
            acc->state = MOUNT_STATE_STARTING;
        } else {
            acc->state = MOUNT_STATE_DISCONNECTED;
            acc->process_id = 0;
            if (i < TAFIM_MAX_ACCOUNTS && g_hMountProcesses[i]) {
                CloseHandle(g_hMountProcesses[i]);
                g_hMountProcesses[i] = NULL;
            }
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

    /* If this is TeraBox, ensure the local TeraBox WebDAV daemon is running */
    if (acc->provider == PROVIDER_TERABOX || strstr(acc->remote_name, "terabox")) {
        ensure_terabox_bridge();
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

    log_mount_debug("[%u] mount_account: letter=%c, cmd=%s\n", (unsigned)GetTickCount(), acc->drive_letter, cmd);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    DWORD creationFlags = CREATE_NO_WINDOW | DETACHED_PROCESS;
    BOOL bCreated = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, creationFlags, NULL, NULL, &si, &pi);
    if (bCreated) {
        log_mount_debug("[%u] CreateProcessA OK, pid=%u\n", (unsigned)GetTickCount(), pi.dwProcessId);
        acc->process_id = pi.dwProcessId;
        if (account_index < TAFIM_MAX_ACCOUNTS) {
            if (g_hMountProcesses[account_index]) CloseHandle(g_hMountProcesses[account_index]);
            g_hMountProcesses[account_index] = pi.hProcess;
        } else {
            CloseHandle(pi.hProcess);
        }
        CloseHandle(pi.hThread);

        /* Wait up to 8 seconds for the drive to appear in Windows */
        for (int wait = 0; wait < 80; wait++) {
            Sleep(100);
            if (mount_is_letter_in_use(acc->drive_letter)) {
                log_mount_debug("[%u] Drive %c: is now in use! Success.\n", (unsigned)GetTickCount(), acc->drive_letter);
                acc->state = MOUNT_STATE_MOUNTED;
                config_save(cfg);
                return true;
            }
            if (account_index < TAFIM_MAX_ACCOUNTS && g_hMountProcesses[account_index]) {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(g_hMountProcesses[account_index], &exitCode) && exitCode != STILL_ACTIVE) {
                    log_mount_debug("[%u] Mount process exited early with code %u!\n", (unsigned)GetTickCount(), exitCode);
                    break;
                }
            }
        }

        if (mount_is_letter_in_use(acc->drive_letter)) {
            acc->state = MOUNT_STATE_MOUNTED;
            config_save(cfg);
            return true;
        }
    }

    if (account_index < TAFIM_MAX_ACCOUNTS && g_hMountProcesses[account_index]) {
        CloseHandle(g_hMountProcesses[account_index]);
        g_hMountProcesses[account_index] = NULL;
    }
    acc->process_id = 0;
    acc->state = MOUNT_STATE_ERROR;
    strcpy_s(acc->last_error, sizeof(acc->last_error), "Failed to start rclone mount with WinFsp.");
    config_save(cfg);
    return false;
}

bool unmount_account(AppConfig* cfg, int account_index) {
    if (account_index < 0 || account_index >= cfg->account_count) return false;
    DriveAccount* acc = &cfg->accounts[account_index];

    char drive_str[4] = { (char)toupper(acc->drive_letter), ':', '\0', '\0' };

    /* Terminate mount process if running */
    if (account_index < TAFIM_MAX_ACCOUNTS && g_hMountProcesses[account_index]) {
        TerminateProcess(g_hMountProcesses[account_index], 0);
        CloseHandle(g_hMountProcesses[account_index]);
        g_hMountProcesses[account_index] = NULL;
    } else if (acc->process_id != 0) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, acc->process_id);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
        }
    }
    acc->process_id = 0;

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
