#include "tafim_drive.h"
#include <shlobj.h>

const char* provider_type_to_string(CloudProviderType type) {
    switch (type) {
        case PROVIDER_TERABOX:  return "TeraBox";
        case PROVIDER_GDRIVE:   return "Google Drive";
        case PROVIDER_ONEDRIVE: return "OneDrive";
        case PROVIDER_DROPBOX:  return "Dropbox";
        case PROVIDER_MEGA:     return "Mega.nz";
        case PROVIDER_WEBDAV:   return "WebDAV";
        case PROVIDER_S3:       return "S3 Storage";
        case PROVIDER_LOCAL:    return "Local Folder";
        default:                return "Unknown";
    }
}

CloudProviderType provider_string_to_type(const char* str) {
    if (!str) return PROVIDER_TERABOX;
    if (_stricmp(str, "TeraBox") == 0 || _stricmp(str, "terabox") == 0) return PROVIDER_TERABOX;
    if (_stricmp(str, "Google Drive") == 0 || _stricmp(str, "gdrive") == 0) return PROVIDER_GDRIVE;
    if (_stricmp(str, "OneDrive") == 0 || _stricmp(str, "onedrive") == 0) return PROVIDER_ONEDRIVE;
    if (_stricmp(str, "Dropbox") == 0 || _stricmp(str, "dropbox") == 0) return PROVIDER_DROPBOX;
    if (_stricmp(str, "Mega") == 0 || _stricmp(str, "mega") == 0 || _stricmp(str, "Mega.nz") == 0) return PROVIDER_MEGA;
    if (_stricmp(str, "WebDAV") == 0 || _stricmp(str, "webdav") == 0) return PROVIDER_WEBDAV;
    if (_stricmp(str, "S3") == 0 || _stricmp(str, "s3") == 0) return PROVIDER_S3;
    return PROVIDER_LOCAL;
}

bool file_exists(const char* path) {
    if (!path || !*path) return false;
    DWORD attrib = GetFileAttributesA(path);
    return (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
}

void config_get_file_path(char* out_path, size_t max_len) {
    char exe_dir[MAX_PATH];
    if (GetModuleFileNameA(NULL, exe_dir, MAX_PATH)) {
        char* last_slash = strrchr(exe_dir, '\\');
        if (last_slash) {
            *last_slash = '\0';
            snprintf(out_path, max_len, "%s\\tafim_drives.json", exe_dir);
            return;
        }
    }
    strncpy_s(out_path, max_len, "tafim_drives.json", _TRUNCATE);
}

static void escape_json_str(const char* in, char* out, size_t out_max) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < out_max; i++) {
        if (in[i] == '"' || in[i] == '\\') {
            out[j++] = '\\';
            out[j++] = in[i];
        } else if (in[i] == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (in[i] == '\r') {
            /* skip */
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
}

void config_init_defaults(AppConfig* cfg) {
    memset(cfg, 0, sizeof(AppConfig));
    cfg->auto_mount_on_startup = true;

    const char* potential_rclone[] = {
        "rclone.exe",
        "tools\\rclone\\rclone.exe",
        "tools\\rclone.exe"
    };

    cfg->rclone_path[0] = '\0';
    for (int i = 0; i < (int)(sizeof(potential_rclone) / sizeof(potential_rclone[0])); i++) {
        if (file_exists(potential_rclone[i])) {
            GetFullPathNameA(potential_rclone[i], MAX_PATH, cfg->rclone_path, NULL);
            break;
        }
    }
    if (!cfg->rclone_path[0]) {
        strncpy_s(cfg->rclone_path, MAX_PATH, "rclone.exe", _TRUNCATE);
    }

    /* Set default cache dir in local appdata or temp */
    char local_appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata))) {
        snprintf(cfg->cache_dir, MAX_PATH, "%s\\TafimDrive\\Cache", local_appdata);
    } else {
        strncpy_s(cfg->cache_dir, MAX_PATH, "C:\\TafimCache", _TRUNCATE);
    }
    cfg->cache_max_gb = 50;

    /* Preset 1: TeraBox Account 1 (Drive T:) */
    DriveAccount* t1 = &cfg->accounts[cfg->account_count++];
    strcpy_s(t1->id, sizeof(t1->id), "terabox_1");
    strcpy_s(t1->name, sizeof(t1->name), "Tafim Main TeraBox (1024 GB)");
    t1->provider = PROVIDER_TERABOX;
    t1->drive_letter = 'T';
    strcpy_s(t1->remote_name, sizeof(t1->remote_name), "terabox_main");
    strcpy_s(t1->endpoint, sizeof(t1->endpoint), "http://127.0.0.1:5244/dav");
    t1->auto_mount = false;
    t1->read_only = false;
    t1->engine = MOUNT_ENGINE_RCLONE_WINFSP;
    t1->quota_total_gb = 1024.0;
    t1->quota_used_gb = 0.0;
    t1->state = MOUNT_STATE_DISCONNECTED;

    /* Preset 2: TeraBox Account 2 (Drive U:) */
    DriveAccount* t2 = &cfg->accounts[cfg->account_count++];
    strcpy_s(t2->id, sizeof(t2->id), "terabox_2");
    strcpy_s(t2->name, sizeof(t2->name), "Tafim Second TeraBox (1024 GB)");
    t2->provider = PROVIDER_TERABOX;
    t2->drive_letter = 'U';
    strcpy_s(t2->remote_name, sizeof(t2->remote_name), "terabox_second");
    strcpy_s(t2->endpoint, sizeof(t2->endpoint), "http://127.0.0.1:5245/dav");
    t2->auto_mount = false;
    t2->read_only = false;
    t2->engine = MOUNT_ENGINE_RCLONE_WINFSP;
    t2->quota_total_gb = 1024.0;
    t2->quota_used_gb = 0.0;
    t2->state = MOUNT_STATE_DISCONNECTED;

    /* Preset 3: Google Drive (Drive G:) */
    DriveAccount* g1 = &cfg->accounts[cfg->account_count++];
    strcpy_s(g1->id, sizeof(g1->id), "gdrive_1");
    strcpy_s(g1->name, sizeof(g1->name), "Google Drive Storage");
    g1->provider = PROVIDER_GDRIVE;
    g1->drive_letter = 'G';
    strcpy_s(g1->remote_name, sizeof(g1->remote_name), "gdrive");
    g1->auto_mount = false;
    g1->read_only = false;
    g1->engine = MOUNT_ENGINE_RCLONE_WINFSP;
    g1->quota_total_gb = 2000.0;
    g1->quota_used_gb = 0.0;
    g1->state = MOUNT_STATE_DISCONNECTED;
}

bool config_save(const AppConfig* cfg) {
    char path[MAX_PATH];
    config_get_file_path(path, sizeof(path));

    FILE* fp = NULL;
    if (fopen_s(&fp, path, "w") != 0 || !fp) {
        return false;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": \"%s\",\n", TAFIM_APP_VERSION);
    
    char esc_rclone[MAX_PATH * 2];
    escape_json_str(cfg->rclone_path, esc_rclone, sizeof(esc_rclone));
    fprintf(fp, "  \"rclone_path\": \"%s\",\n", esc_rclone);

    char esc_cache[MAX_PATH * 2];
    escape_json_str(cfg->cache_dir, esc_cache, sizeof(esc_cache));
    fprintf(fp, "  \"cache_dir\": \"%s\",\n", esc_cache);
    fprintf(fp, "  \"cache_max_gb\": %d,\n", cfg->cache_max_gb > 0 ? cfg->cache_max_gb : 50);

    fprintf(fp, "  \"auto_mount_on_startup\": %s,\n", cfg->auto_mount_on_startup ? "true" : "false");
    fprintf(fp, "  \"accounts\": [\n");

    for (int i = 0; i < cfg->account_count; i++) {
        const DriveAccount* acc = &cfg->accounts[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"id\": \"%s\",\n", acc->id);
        
        char buf[TAFIM_LONG_STR_LEN * 2];
        escape_json_str(acc->name, buf, sizeof(buf));
        fprintf(fp, "      \"name\": \"%s\",\n", buf);
        
        fprintf(fp, "      \"provider\": \"%s\",\n", provider_type_to_string(acc->provider));
        fprintf(fp, "      \"drive_letter\": \"%c\",\n", acc->drive_letter);
        
        escape_json_str(acc->remote_name, buf, sizeof(buf));
        fprintf(fp, "      \"remote_name\": \"%s\",\n", buf);

        escape_json_str(acc->remote_path, buf, sizeof(buf));
        fprintf(fp, "      \"remote_path\": \"%s\",\n", buf);

        escape_json_str(acc->endpoint, buf, sizeof(buf));
        fprintf(fp, "      \"endpoint\": \"%s\",\n", buf);

        escape_json_str(acc->auth_token, buf, sizeof(buf));
        fprintf(fp, "      \"auth_token\": \"%s\",\n", buf);

        escape_json_str(acc->username, buf, sizeof(buf));
        fprintf(fp, "      \"username\": \"%s\",\n", buf);

        escape_json_str(acc->password, buf, sizeof(buf));
        fprintf(fp, "      \"password\": \"%s\",\n", buf);

        fprintf(fp, "      \"auto_mount\": %s,\n", acc->auto_mount ? "true" : "false");
        fprintf(fp, "      \"read_only\": %s,\n", acc->read_only ? "true" : "false");
        fprintf(fp, "      \"engine\": %d,\n", (int)acc->engine);
        fprintf(fp, "      \"quota_total_gb\": %.2f,\n", acc->quota_total_gb);
        fprintf(fp, "      \"quota_used_gb\": %.2f\n", acc->quota_used_gb);
        fprintf(fp, "    }%s\n", (i + 1 < cfg->account_count) ? "," : "");
    }

    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    return true;
}

static char* extract_json_string(const char* json, const char* key, char* out, size_t max_len) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return NULL;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return NULL;
    p++; /* Skip colon */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '\"') return NULL;
    p++;
    size_t idx = 0;
    while (*p && *p != '\"' && idx + 1 < max_len) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            if (*p == 'n') out[idx++] = '\n';
            else out[idx++] = *p;
        } else {
            out[idx++] = *p;
        }
        p++;
    }
    out[idx] = '\0';
    return out;
}

static bool extract_json_bool(const char* json, const char* key, bool default_val) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return default_val;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return default_val;
    p++; /* Skip colon */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (_strnicmp(p, "true", 4) == 0) return true;
    if (_strnicmp(p, "false", 5) == 0) return false;
    return default_val;
}

static double extract_json_double(const char* json, const char* key, double default_val) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return default_val;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return default_val;
    p++; /* Skip colon */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return atof(p);
}

static int extract_json_int(const char* json, const char* key, int default_val) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return default_val;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return default_val;
    p++; /* Skip colon */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    int val = atoi(p);
    return val > 0 ? val : default_val;
}

bool config_load(AppConfig* cfg) {
    char path[MAX_PATH];
    config_get_file_path(path, sizeof(path));

    FILE* fp = NULL;
    if (fopen_s(&fp, path, "r") != 0 || !fp) {
        config_init_defaults(cfg);
        config_save(cfg);
        return true;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0 || sz > 10 * 1024 * 1024) {
        fclose(fp);
        config_init_defaults(cfg);
        return true;
    }

    char* content = (char*)malloc(sz + 1);
    if (!content) {
        fclose(fp);
        config_init_defaults(cfg);
        return false;
    }

    fread(content, 1, sz, fp);
    content[sz] = '\0';
    fclose(fp);

    memset(cfg, 0, sizeof(AppConfig));
    extract_json_string(content, "rclone_path", cfg->rclone_path, sizeof(cfg->rclone_path));
    extract_json_string(content, "cache_dir", cfg->cache_dir, sizeof(cfg->cache_dir));
    cfg->cache_max_gb = extract_json_int(content, "cache_max_gb", 50);
    cfg->auto_mount_on_startup = extract_json_bool(content, "auto_mount_on_startup", true);

    if (!cfg->rclone_path[0]) {
        strcpy_s(cfg->rclone_path, sizeof(cfg->rclone_path), "rclone.exe");
    }

    /* Parse accounts block */
    const char* acc_start = strstr(content, "\"accounts\"");
    if (acc_start) {
        const char* p = strchr(acc_start, '[');
        if (p) {
            p++;
            while (*p && cfg->account_count < TAFIM_MAX_ACCOUNTS) {
                const char* obj_start = strchr(p, '{');
                if (!obj_start) break;
                const char* obj_end = strchr(obj_start, '}');
                if (!obj_end) break;

                size_t obj_len = obj_end - obj_start + 1;
                char* obj_str = (char*)malloc(obj_len + 1);
                if (obj_str) {
                    memcpy(obj_str, obj_start, obj_len);
                    obj_str[obj_len] = '\0';

                    DriveAccount acc;
                    memset(&acc, 0, sizeof(DriveAccount));

                    extract_json_string(obj_str, "id", acc.id, sizeof(acc.id));
                    extract_json_string(obj_str, "name", acc.name, sizeof(acc.name));
                    
                    char prov_str[64] = {0};
                    extract_json_string(obj_str, "provider", prov_str, sizeof(prov_str));
                    acc.provider = provider_string_to_type(prov_str);

                    char drive_letter_str[8] = {0};
                    extract_json_string(obj_str, "drive_letter", drive_letter_str, sizeof(drive_letter_str));
                    acc.drive_letter = drive_letter_str[0] ? (char)toupper(drive_letter_str[0]) : 'T';

                    extract_json_string(obj_str, "remote_name", acc.remote_name, sizeof(acc.remote_name));
                    extract_json_string(obj_str, "remote_path", acc.remote_path, sizeof(acc.remote_path));
                    extract_json_string(obj_str, "endpoint", acc.endpoint, sizeof(acc.endpoint));
                    extract_json_string(obj_str, "auth_token", acc.auth_token, sizeof(acc.auth_token));
                    extract_json_string(obj_str, "username", acc.username, sizeof(acc.username));
                    extract_json_string(obj_str, "password", acc.password, sizeof(acc.password));

                    acc.auto_mount = extract_json_bool(obj_str, "auto_mount", false);
                    acc.read_only = extract_json_bool(obj_str, "read_only", false);
                    acc.engine = (MountEngine)(int)extract_json_double(obj_str, "engine", 0);
                    acc.quota_total_gb = extract_json_double(obj_str, "quota_total_gb", 1024.0);
                    acc.quota_used_gb = extract_json_double(obj_str, "quota_used_gb", 0.0);
                    acc.state = MOUNT_STATE_DISCONNECTED;

                    free(obj_str);

                    if (acc.id[0] && acc.name[0] && cfg->account_count < TAFIM_MAX_ACCOUNTS) {
                        cfg->accounts[cfg->account_count++] = acc;
                    }
                }
                p = obj_end + 1;
            }
        }
    }

    free(content);

    /* Ensure duplicate drives on the same cloud backend have their own isolated folder so files never mix */
    for (int i = 0; i < cfg->account_count; i++) {
        for (int j = i + 1; j < cfg->account_count; j++) {
            if (cfg->accounts[i].remote_name[0] &&
                _stricmp(cfg->accounts[i].remote_name, cfg->accounts[j].remote_name) == 0) {
                if (!cfg->accounts[i].remote_path[0] && !cfg->accounts[j].remote_path[0]) {
                    snprintf(cfg->accounts[j].remote_path, sizeof(cfg->accounts[j].remote_path), "Drive_%c", cfg->accounts[j].drive_letter);
                }
            }
        }
    }

    if (cfg->account_count == 0) {
        config_init_defaults(cfg);
        config_save(cfg);
    }

    return true;
}

int config_find_account_by_letter(const AppConfig* cfg, char drive_letter) {
    char needle = (char)toupper(drive_letter);
    for (int i = 0; i < cfg->account_count; i++) {
        if (toupper(cfg->accounts[i].drive_letter) == needle) {
            return i;
        }
    }
    return -1;
}

int config_find_account_by_id(const AppConfig* cfg, const char* id) {
    if (!id || !*id) return -1;
    for (int i = 0; i < cfg->account_count; i++) {
        if (strcmp(cfg->accounts[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}
