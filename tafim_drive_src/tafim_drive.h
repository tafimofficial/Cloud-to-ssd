#ifndef TAFIM_DRIVE_H
#define TAFIM_DRIVE_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <shellapi.h>
#include <winnetwk.h>

#define TAFIM_APP_NAME "Tafim Drive"
#define TAFIM_APP_VERSION "1.0.0"
#define TAFIM_MAX_ACCOUNTS 32
#define TAFIM_STR_LEN 256
#define TAFIM_LONG_STR_LEN 1024

typedef enum {
    PROVIDER_TERABOX = 0,
    PROVIDER_GDRIVE,
    PROVIDER_ONEDRIVE,
    PROVIDER_DROPBOX,
    PROVIDER_MEGA,
    PROVIDER_WEBDAV,
    PROVIDER_S3,
    PROVIDER_LOCAL
} CloudProviderType;

typedef enum {
    MOUNT_STATE_DISCONNECTED = 0,
    MOUNT_STATE_MOUNTED,
    MOUNT_STATE_ERROR,
    MOUNT_STATE_STARTING
} MountState;

typedef enum {
    MOUNT_ENGINE_RCLONE_WINFSP = 0,
    MOUNT_ENGINE_NETWORK_DRIVE,
    MOUNT_ENGINE_VIRTUAL_DEVICE
} MountEngine;

typedef struct {
    char id[64];
    char name[TAFIM_STR_LEN];
    CloudProviderType provider;
    char drive_letter;          /* e.g. 'T', 'U', 'G' */
    char remote_name[TAFIM_STR_LEN];
    char remote_path[TAFIM_STR_LEN];
    char endpoint[TAFIM_STR_LEN];  /* WebDAV URL or API endpoint */
    char auth_token[TAFIM_LONG_STR_LEN]; /* TeraBox ndus cookie / auth token */
    char username[TAFIM_STR_LEN];
    char password[TAFIM_STR_LEN];
    bool auto_mount;
    bool read_only;
    MountEngine engine;
    
    /* Runtime status */
    MountState state;
    DWORD process_id;
    double quota_total_gb;
    double quota_used_gb;
    char last_error[TAFIM_STR_LEN];
} DriveAccount;

typedef struct {
    DriveAccount accounts[TAFIM_MAX_ACCOUNTS];
    int account_count;
    char rclone_path[MAX_PATH];
    char cache_dir[MAX_PATH];
    int cache_max_gb;
    bool auto_mount_on_startup;
} AppConfig;

/* Function prototypes */

/* Config functions */
void config_get_file_path(char* out_path, size_t max_len);
bool config_load(AppConfig* cfg);
bool config_save(const AppConfig* cfg);
void config_init_defaults(AppConfig* cfg);
int config_find_account_by_letter(const AppConfig* cfg, char drive_letter);
int config_find_account_by_id(const AppConfig* cfg, const char* id);

/* TeraBox functions */
bool terabox_test_connection(const char* token_or_cookie, double* out_total_gb, double* out_used_gb, char* out_user);
bool terabox_configure_rclone_remote(const char* rclone_exe, const char* remote_name, const char* endpoint, const char* user, const char* pass);

/* Mount functions */
bool mount_is_letter_in_use(char drive_letter);
void mount_get_available_letters(char* out_letters, int max_count);
bool mount_account(AppConfig* cfg, int account_index);
bool unmount_account(AppConfig* cfg, int account_index);
int mount_all_auto(AppConfig* cfg);
int unmount_all(AppConfig* cfg);
void mount_update_all_states(AppConfig* cfg);
bool mount_open_in_explorer(char drive_letter);

/* UI & Terminal functions */
void ui_clear_screen(void);
void ui_show_banner(void);
void ui_show_status_table(const AppConfig* cfg);
void ui_menu_loop(AppConfig* cfg);
void ui_wizard_add_account(AppConfig* cfg);
void ui_wizard_remove_account(AppConfig* cfg);
void ui_wizard_test_terabox(AppConfig* cfg);

/* Server & Browser Login functions */
bool server_start(AppConfig* cfg);
void server_stop(void);
void server_set_gui_hwnd(HWND hwnd);

/* GUI functions */
int gui_run(AppConfig* cfg, HINSTANCE hInstance, int nCmdShow);

/* Helpers */
const char* provider_type_to_string(CloudProviderType type);
CloudProviderType provider_string_to_type(const char* str);
void format_bytes(double bytes, char* out_str, size_t max_len);
bool file_exists(const char* path);

#endif /* TAFIM_DRIVE_H */
