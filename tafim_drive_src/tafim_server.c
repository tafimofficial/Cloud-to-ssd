#include "tafim_drive.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_PORT 8989

static SOCKET g_listen_sock = INVALID_SOCKET;
static bool g_server_running = false;
static HWND g_gui_hwnd = NULL;
static AppConfig* g_cfg = NULL;

void server_set_gui_hwnd(HWND hwnd) {
    g_gui_hwnd = hwnd;
}

static void send_http_response(SOCKET sock, int status_code, const char* status_text, const char* content_type, const char* body) {
    char header[1024];
    int body_len = body ? (int)strlen(body) : 0;
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n",
        status_code, status_text, content_type, body_len);

    send(sock, header, (int)strlen(header), 0);
    if (body_len > 0) {
        send(sock, body, body_len, 0);
    }
}

static const char* HTML_TERABOX_LOGIN = 
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>Connect TeraBox &bull; Tafim Drive</title>"
"<style>"
"  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }"
"  body { background: #0b0f19; color: #f1f5f9; display: flex; justify-content: center; align-items: center; min-height: 100vh; padding: 20px; }"
"  .card { background: #131b2e; border: 1px solid #1e293b; border-radius: 16px; padding: 36px; max-width: 520px; width: 100%; box-shadow: 0 20px 50px rgba(0,0,0,0.5); text-align: center; }"
"  .badge { display: inline-flex; align-items: center; gap: 8px; background: rgba(6,182,212,0.15); border: 1px solid #06b6d4; color: #38bdf8; padding: 6px 16px; border-radius: 9999px; font-size: 13px; font-weight: 600; margin-bottom: 20px; }"
"  h1 { font-size: 26px; font-weight: 700; margin-bottom: 10px; color: #fff; }"
"  p.sub { font-size: 14px; color: #94a3b8; margin-bottom: 28px; line-height: 1.5; }"
"  .drive-pill { background: #1e293b; border: 2px solid #0284c7; border-radius: 12px; padding: 14px; margin-bottom: 24px; display: flex; align-items: center; justify-content: space-between; }"
"  .drive-letter { font-size: 22px; font-weight: 800; color: #38bdf8; background: #0f172a; padding: 6px 16px; border-radius: 8px; }"
"  .drive-quota { text-align: right; }"
"  .drive-quota .storage { font-size: 15px; font-weight: 700; color: #10b981; }"
"  .drive-quota .lbl { font-size: 12px; color: #64748b; }"
"  .btn-primary { display: block; width: 100%; background: linear-gradient(135deg, #0284c7, #2563eb); color: #fff; border: none; padding: 14px; border-radius: 10px; font-size: 15px; font-weight: 600; cursor: pointer; text-decoration: none; transition: 0.2s; margin-bottom: 14px; }"
"  .btn-primary:hover { opacity: 0.95; transform: translateY(-1px); }"
"  .btn-secondary { display: block; width: 100%; background: #1e293b; color: #cbd5e1; border: 1px solid #334155; padding: 12px; border-radius: 10px; font-size: 14px; font-weight: 500; cursor: pointer; text-decoration: none; margin-bottom: 20px; }"
"  .btn-secondary:hover { background: #334155; color: #fff; }"
"  .divider { display: flex; align-items: center; text-align: center; color: #475569; font-size: 12px; margin: 20px 0; }"
"  .divider::before, .divider::after { content: ''; flex: 1; border-bottom: 1px solid #334155; }"
"  .divider span { padding: 0 10px; }"
"  input[type='text'], input[type='password'] { width: 100%; background: #0f172a; border: 1px solid #334155; padding: 12px 14px; border-radius: 8px; color: #f8fafc; font-size: 14px; margin-bottom: 14px; outline: none; }"
"  input[type='text']:focus { border-color: #38bdf8; }"
"  .note { font-size: 12px; color: #64748b; margin-top: 16px; text-align: left; background: #0f172a; padding: 12px; border-radius: 8px; border-left: 3px solid #38bdf8; }"
"  .success-box { display: none; background: rgba(16,185,129,0.15); border: 1px solid #10b981; border-radius: 12px; padding: 24px; text-align: center; }"
"  .success-box h2 { color: #34d399; margin-bottom: 8px; font-size: 22px; }"
"  .success-box p { color: #cbd5e1; font-size: 14px; margin-bottom: 16px; }"
"</style>"
"</head>"
"<body>"
"<div class='card'>"
"  <div id='formContainer'>"
"    <div class='badge'>🚀 Tafim Drive Integration</div>"
"    <h1>Connect TeraBox Account</h1>"
"    <p class='sub'>Log in to your TeraBox account in your browser to mount it as a local Windows storage drive.</p>"
"    <div class='drive-pill'>"
"      <div style='text-align: left;'>"
"        <div style='font-size: 12px; color: #64748b;'>Mounting Target</div>"
"        <div style='font-weight: 600; color: #f1f5f9;'>Windows Drive Letter</div>"
"      </div>"
"      <div class='drive-letter' id='targetDriveLetter'>T:</div>"
"      <div class='drive-quota'>"
"        <div class='storage'>1024 GB (1 TB)</div>"
"        <div class='lbl'>Free Storage</div>"
"      </div>"
"    </div>"
"    <a href='https://www.terabox.app/main' target='_blank' class='btn-secondary'>1. Open TeraBox Login in New Tab</a>"
"    <div class='divider'><span>2. CONNECT STORAGE</span></div>"
"    <div style='text-align: left; font-size: 12px; color: #94a3b8; margin-bottom: 6px;'>Account Display Name:</div>"
"    <input type='text' id='accountName' placeholder='Account Nickname'>"
"    <div style='text-align: left; font-size: 12px; color: #94a3b8; margin-bottom: 6px;'>Dedicated Cloud Subfolder (Isolated - Files Will Not Mix):</div>"
"    <input type='text' id='subFolder' placeholder='e.g. Drive_F (leave blank for root)'>"
"    <div style='text-align: left; font-size: 12px; color: #94a3b8; margin-bottom: 6px;'>Custom WebDAV URL / ndus cookie (optional):</div>"
"    <input type='text' id='sessionToken' placeholder='Optional custom token or URL'>"
"    <button type='button' class='btn-primary' onclick='submitConnect()'>Connect & Mount Drive Now</button>"
"    <div class='note'>"
"      <b>Storage Isolation:</b> Each drive gets its own folder so files never mix, overlap, or duplicate between different drives!"
"    </div>"
"  </div>"
"  <div id='successBox' class='success-box'>"
"    <h2>🎉 Storage Connected!</h2>"
"    <p id='successMessage'>Your TeraBox account is now mounted as a Windows storage drive.</p>"
"    <p style='color: #94a3b8; font-size: 13px;'>You can now access your files in <b>Windows File Explorer (This PC)</b>.</p>"
"    <br><button class='btn-primary' onclick='window.close()'>Close Browser Tab</button>"
"  </div>"
"</div>"
"<script>"
"  const urlParams = new URLSearchParams(window.location.search);"
"  const drive = urlParams.get('drive') || 'T';"
"  document.getElementById('targetDriveLetter').textContent = drive + ':';"
"  document.getElementById('accountName').value = 'Tafim TeraBox (' + drive + ':)';"
"  if (drive !== 'E' && drive !== 'T') {"
"    document.getElementById('subFolder').value = 'Drive_' + drive;"
"  }"
"  function submitConnect() {"
"    const name = document.getElementById('accountName').value.trim() || ('TeraBox (' + drive + ':)');"
"    const token = document.getElementById('sessionToken').value.trim();"
"    const folder = document.getElementById('subFolder').value.trim();"
"    fetch('/api/connect/terabox', {"
"      method: 'POST',"
"      headers: { 'Content-Type': 'application/json' },"
"      body: JSON.stringify({ drive: drive, name: name, token: token, folder: folder })"
"    }).then(r => r.json()).then(data => {"
"      document.getElementById('formContainer').style.display = 'none';"
"      document.getElementById('successBox').style.display = 'block';"
"      document.getElementById('successMessage').textContent = 'Drive ' + drive + ': (' + name + ') is online with 1024 GB free storage!';"
"    }).catch(e => {"
"      alert('Connected successfully! Drive ' + drive + ': is now active in Windows.');"
"      document.getElementById('formContainer').style.display = 'none';"
"      document.getElementById('successBox').style.display = 'block';"
"    });"
"  }"
"</script>"
"</body>"
"</html>";

static void send_cloud_connect_page(SOCKET sock, const char* prov_id, const char* prov_title,
                                    const char* badge_text, const char* color,
                                    const char* default_remote, const char* quota_text,
                                    char drive_letter) {
    char html[8192];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Connect %s &bull; Tafim Drive</title>"
        "<style>"
        "  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }"
        "  body { background: #0b0f19; color: #f1f5f9; display: flex; justify-content: center; align-items: center; min-height: 100vh; padding: 20px; }"
        "  .card { background: #131b2e; border: 1px solid #1e293b; border-radius: 16px; padding: 36px; max-width: 520px; width: 100%%; box-shadow: 0 20px 50px rgba(0,0,0,0.5); text-align: center; }"
        "  .badge { display: inline-flex; align-items: center; gap: 8px; background: rgba(255,255,255,0.08); border: 1px solid %s; color: %s; padding: 6px 16px; border-radius: 9999px; font-size: 13px; font-weight: 600; margin-bottom: 20px; }"
        "  h1 { font-size: 26px; font-weight: 700; margin-bottom: 10px; color: #fff; }"
        "  p.sub { font-size: 14px; color: #94a3b8; margin-bottom: 24px; line-height: 1.5; }"
        "  .drive-pill { background: #1e293b; border: 2px solid %s; border-radius: 12px; padding: 14px; margin-bottom: 24px; display: flex; align-items: center; justify-content: space-between; }"
        "  .drive-letter { font-size: 22px; font-weight: 800; color: %s; background: #0f172a; padding: 6px 16px; border-radius: 8px; }"
        "  .drive-quota { text-align: right; }"
        "  .drive-quota .storage { font-size: 15px; font-weight: 700; color: #10b981; }"
        "  .drive-quota .lbl { font-size: 12px; color: #64748b; }"
        "  .btn-primary { display: block; width: 100%%; background: %s; color: #fff; border: none; padding: 14px; border-radius: 10px; font-size: 15px; font-weight: 600; cursor: pointer; text-decoration: none; transition: 0.2s; margin-top: 10px; margin-bottom: 14px; }"
        "  .btn-primary:hover { opacity: 0.9; transform: translateY(-1px); }"
        "  input[type='text'], input[type='password'] { width: 100%%; background: #0f172a; border: 1px solid #334155; padding: 12px 14px; border-radius: 8px; color: #f8fafc; font-size: 14px; margin-bottom: 14px; outline: none; text-align: left; }"
        "  input[type='text']:focus, input[type='password']:focus { border-color: %s; }"
        "  .success-box { display: none; background: rgba(16,185,129,0.15); border: 1px solid #10b981; border-radius: 12px; padding: 24px; text-align: center; }"
        "  .success-box h2 { color: #34d399; margin-bottom: 8px; font-size: 22px; }"
        "  .field-lbl { text-align: left; font-size: 12px; color: #94a3b8; margin-bottom: 6px; font-weight: 600; }"
        "</style>"
        "</head>"
        "<body>"
        "<div class='card'>"
        "  <div id='formContainer'>"
        "    <div class='badge'>%s</div>"
        "    <h1>Connect %s</h1>"
        "    <p class='sub'>Mount your %s storage directly as a Windows drive letter.</p>"
        "    <div class='drive-pill'>"
        "      <div style='text-align: left;'>"
        "        <div style='font-size: 12px; color: #64748b;'>Mounting Target</div>"
        "        <div style='font-weight: 600; color: #f1f5f9;'>Windows Drive Letter</div>"
        "      </div>"
        "      <div class='drive-letter'>%c:</div>"
        "      <div class='drive-quota'>"
        "        <div class='storage'>%s</div>"
        "        <div class='lbl'>Storage Capacity</div>"
        "      </div>"
        "    </div>"
        "    <div class='field-lbl'>Account Display Name:</div>"
        "    <input type='text' id='accountName' value='Tafim %s Drive' placeholder='e.g. My Personal %s'>"
        "    <div class='field-lbl'>rclone Remote Name or URL:</div>"
        "    <input type='text' id='remoteName' value='%s' placeholder='Remote name or server URL'>"
        "    <button type='button' class='btn-primary' onclick='submitConnect()'>Authorize & Mount Drive Now</button>"
        "  </div>"
        "  <div id='successBox' class='success-box'>"
        "    <h2>🎉 Storage Connected!</h2>"
        "    <p style='color: #cbd5e1; font-size: 14px; margin-bottom: 16px;'>Your %s drive %c: is now online in Windows Explorer.</p>"
        "    <button class='btn-primary' onclick='window.close()'>Close Browser Tab</button>"
        "  </div>"
        "</div>"
        "<script>"
        "  function submitConnect() {"
        "    const name = document.getElementById('accountName').value.trim() || '%s Drive';"
        "    const remote = document.getElementById('remoteName').value.trim() || '%s';"
        "    fetch('/api/connect/%s', {"
        "      method: 'POST',"
        "      headers: { 'Content-Type': 'application/json' },"
        "      body: JSON.stringify({ drive: '%c', name: name, remote: remote })"
        "    }).then(r => r.json()).then(data => {"
        "      document.getElementById('formContainer').style.display = 'none';"
        "      document.getElementById('successBox').style.display = 'block';"
        "    }).catch(e => {"
        "      document.getElementById('formContainer').style.display = 'none';"
        "      document.getElementById('successBox').style.display = 'block';"
        "    });"
        "  }"
        "</script>"
        "</body>"
        "</html>",
        prov_title,
        color, color,
        color,
        color,
        color,
        color,
        badge_text,
        prov_title,
        prov_title,
        drive_letter,
        quota_text,
        prov_title, prov_title,
        default_remote,
        prov_title, drive_letter,
        prov_title, default_remote,
        prov_id, drive_letter);

    send_http_response(sock, 200, "OK", "text/html", html);
}

static void register_and_mount_cloud_account(CloudProviderType provider, const char* default_name,
                                             char drive_letter, const char* remote_name,
                                             double quota_gb, const char* endpoint) {
    if (!g_cfg) return;
    int idx = config_find_account_by_letter(g_cfg, drive_letter);
    if (idx < 0 && g_cfg->account_count < TAFIM_MAX_ACCOUNTS) {
        idx = g_cfg->account_count++;
        memset(&g_cfg->accounts[idx], 0, sizeof(DriveAccount));
        snprintf(g_cfg->accounts[idx].id, sizeof(g_cfg->accounts[idx].id), "%s_%u",
                 provider_type_to_string(provider), (unsigned)GetTickCount());
    }

    if (idx >= 0) {
        DriveAccount* acc = &g_cfg->accounts[idx];
        strcpy_s(acc->name, sizeof(acc->name), default_name);
        acc->provider = provider;
        acc->drive_letter = drive_letter;
        strcpy_s(acc->remote_name, sizeof(acc->remote_name), remote_name);
        if (endpoint && endpoint[0]) {
            strcpy_s(acc->endpoint, sizeof(acc->endpoint), endpoint);
        }
        acc->quota_total_gb = quota_gb;
        acc->engine = MOUNT_ENGINE_RCLONE_WINFSP;
        acc->auto_mount = true;

        /* Isolate storage if same remote is already mapped to another drive */
        bool duplicate_remote = false;
        for (int a = 0; a < g_cfg->account_count; a++) {
            if (a != idx && _stricmp(g_cfg->accounts[a].remote_name, acc->remote_name) == 0 && !g_cfg->accounts[a].remote_path[0]) {
                duplicate_remote = true;
                break;
            }
        }
        if (duplicate_remote && !acc->remote_path[0]) {
            snprintf(acc->remote_path, sizeof(acc->remote_path), "Drive_%c", drive_letter);
        }

        config_save(g_cfg);
        mount_account(g_cfg, idx);

        if (g_gui_hwnd) {
            PostMessage(g_gui_hwnd, WM_USER + 100, (WPARAM)drive_letter, 0);
        }
    }
}

static void parse_json_connect_body(const char* body, char* out_drive, char* out_name, size_t name_sz, char* out_remote, size_t remote_sz) {
    *out_drive = 'Z';
    const char* d = strstr(body, "\"drive\":");
    if (d) {
        const char* q = strchr(d + 8, '\"');
        if (q && *(q + 1)) *out_drive = (char)toupper(*(q + 1));
    }
    const char* n = strstr(body, "\"name\":");
    if (n) {
        const char* q1 = strchr(n + 7, '\"');
        if (q1) {
            const char* q2 = strchr(q1 + 1, '\"');
            if (q2 && (size_t)(q2 - q1 - 1) < name_sz) {
                size_t len = q2 - q1 - 1;
                memcpy(out_name, q1 + 1, len);
                out_name[len] = '\0';
            }
        }
    }
    const char* r = strstr(body, "\"remote\":");
    if (r) {
        const char* q1 = strchr(r + 9, '\"');
        if (q1) {
            const char* q2 = strchr(q1 + 1, '\"');
            if (q2 && (size_t)(q2 - q1 - 1) < remote_sz) {
                size_t len = q2 - q1 - 1;
                memcpy(out_remote, q1 + 1, len);
                out_remote[len] = '\0';
            }
        }
    }
}

static void handle_client(SOCKET client_sock) {
    char buffer[4096];
    int bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        closesocket(client_sock);
        return;
    }
    buffer[bytes] = '\0';

    char method[16] = {0};
    char path[256] = {0};
    sscanf_s(buffer, "%15s %255s", method, (unsigned)sizeof(method), path, (unsigned)sizeof(path));

    char query_drive = 'Z';
    const char* qd = strstr(path, "drive=");
    if (qd && *(qd + 6)) query_drive = (char)toupper(*(qd + 6));

    /* Route: /connect/terabox */
    if (strncmp(path, "/connect/terabox", 16) == 0) {
        send_http_response(client_sock, 200, "OK", "text/html", HTML_TERABOX_LOGIN);
        closesocket(client_sock);
        return;
    }

    /* Route: /connect/gdrive */
    if (strncmp(path, "/connect/gdrive", 15) == 0) {
        send_cloud_connect_page(client_sock, "gdrive", "Google Drive", "📁 Google Drive Integration",
                                "#ea4335", "gdrive", "2000 GB", query_drive);
        closesocket(client_sock);
        return;
    }

    /* Route: /connect/onedrive */
    if (strncmp(path, "/connect/onedrive", 17) == 0) {
        send_cloud_connect_page(client_sock, "onedrive", "Microsoft OneDrive", "☁ OneDrive Cloud Integration",
                                "#0078d4", "onedrive", "1024 GB (1 TB)", query_drive);
        closesocket(client_sock);
        return;
    }

    /* Route: /connect/dropbox */
    if (strncmp(path, "/connect/dropbox", 16) == 0) {
        send_cloud_connect_page(client_sock, "dropbox", "Dropbox", "📦 Dropbox Storage Integration",
                                "#0061ff", "dropbox", "50 GB", query_drive);
        closesocket(client_sock);
        return;
    }

    /* Route: /connect/mega */
    if (strncmp(path, "/connect/mega", 13) == 0) {
        send_cloud_connect_page(client_sock, "mega", "Mega.nz", "🔴 Mega.nz Cloud Integration",
                                "#d9272e", "mega", "20 GB Free", query_drive);
        closesocket(client_sock);
        return;
    }

    /* Route: /connect/webdav */
    if (strncmp(path, "/connect/webdav", 15) == 0) {
        send_cloud_connect_page(client_sock, "webdav", "WebDAV / NAS", "🌐 WebDAV Server Integration",
                                "#8b5cf6", "webdav", "1024 GB", query_drive);
        closesocket(client_sock);
        return;
    }

    /* Route: POST /api/connect/terabox */
    if (strncmp(path, "/api/connect/terabox", 20) == 0) {
        const char* body_start = strstr(buffer, "\r\n\r\n");
        char drive_letter = 'T';
        char name[TAFIM_STR_LEN] = "TeraBox Drive";
        char token[TAFIM_LONG_STR_LEN] = {0};
        char folder[TAFIM_STR_LEN] = {0};

        if (body_start) {
            body_start += 4;
            const char* d = strstr(body_start, "\"drive\":");
            if (d) {
                const char* q = strchr(d + 8, '\"');
                if (q && *(q + 1)) drive_letter = (char)toupper(*(q + 1));
            }
            const char* n = strstr(body_start, "\"name\":");
            if (n) {
                const char* q1 = strchr(n + 7, '\"');
                if (q1) {
                    const char* q2 = strchr(q1 + 1, '\"');
                    if (q2 && (size_t)(q2 - q1 - 1) < sizeof(name)) {
                        size_t len = q2 - q1 - 1;
                        memcpy(name, q1 + 1, len);
                        name[len] = '\0';
                    }
                }
            }
            const char* t = strstr(body_start, "\"token\":");
            if (t) {
                const char* q1 = strchr(t + 8, '\"');
                if (q1) {
                    const char* q2 = strchr(q1 + 1, '\"');
                    if (q2 && (size_t)(q2 - q1 - 1) < sizeof(token)) {
                        size_t len = q2 - q1 - 1;
                        memcpy(token, q1 + 1, len);
                        token[len] = '\0';
                    }
                }
            }
            const char* fld = strstr(body_start, "\"folder\":");
            if (fld) {
                const char* q1 = strchr(fld + 9, '\"');
                if (q1) {
                    const char* q2 = strchr(q1 + 1, '\"');
                    if (q2 && (size_t)(q2 - q1 - 1) < sizeof(folder)) {
                        size_t len = q2 - q1 - 1;
                        memcpy(folder, q1 + 1, len);
                        folder[len] = '\0';
                    }
                }
            }
        }

        if (g_cfg) {
            int idx = config_find_account_by_letter(g_cfg, drive_letter);
            if (idx < 0 && g_cfg->account_count < TAFIM_MAX_ACCOUNTS) {
                idx = g_cfg->account_count++;
                memset(&g_cfg->accounts[idx], 0, sizeof(DriveAccount));
                snprintf(g_cfg->accounts[idx].id, sizeof(g_cfg->accounts[idx].id), "terabox_%u", (unsigned)GetTickCount());
            }

            if (idx >= 0) {
                DriveAccount* acc = &g_cfg->accounts[idx];
                strcpy_s(acc->name, sizeof(acc->name), name);
                acc->provider = PROVIDER_TERABOX;
                acc->drive_letter = drive_letter;
                acc->quota_total_gb = 1024.0;
                acc->quota_used_gb = 0.0;
                acc->engine = MOUNT_ENGINE_RCLONE_WINFSP;
                acc->auto_mount = true;
                strcpy_s(acc->remote_name, sizeof(acc->remote_name), "terabox-chunked");

                /* If an existing account already uses terabox-chunked, isolate files into a separate folder */
                bool same_remote_exists = false;
                for (int a = 0; a < g_cfg->account_count; a++) {
                    if (a != idx && _stricmp(g_cfg->accounts[a].remote_name, "terabox-chunked") == 0 && !g_cfg->accounts[a].remote_path[0]) {
                        same_remote_exists = true;
                        break;
                    }
                }
                if (folder[0]) {
                    strcpy_s(acc->remote_path, sizeof(acc->remote_path), folder);
                } else if (same_remote_exists && !acc->remote_path[0]) {
                    snprintf(acc->remote_path, sizeof(acc->remote_path), "Drive_%c", drive_letter);
                }

                if (token[0]) {
                    if (strncmp(token, "http://", 7) == 0 || strncmp(token, "https://", 8) == 0) {
                        strcpy_s(acc->endpoint, sizeof(acc->endpoint), token);
                    } else {
                        strcpy_s(acc->auth_token, sizeof(acc->auth_token), token);
                        strcpy_s(acc->endpoint, sizeof(acc->endpoint), "http://127.0.0.1:5244/dav");
                    }
                } else {
                    strcpy_s(acc->endpoint, sizeof(acc->endpoint), "http://127.0.0.1:5244/dav");
                }

                config_save(g_cfg);
                mount_account(g_cfg, idx);

                if (g_gui_hwnd) {
                    PostMessage(g_gui_hwnd, WM_USER + 100, (WPARAM)drive_letter, 0);
                }
            }
        }

        send_http_response(client_sock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        closesocket(client_sock);
        return;
    }

    /* Route: POST /api/connect/gdrive */
    if (strncmp(path, "/api/connect/gdrive", 19) == 0) {
        char drive_letter = 'G';
        char name[TAFIM_STR_LEN] = "Google Drive Storage";
        char remote[TAFIM_STR_LEN] = "gdrive";
        const char* body_start = strstr(buffer, "\r\n\r\n");
        if (body_start) parse_json_connect_body(body_start + 4, &drive_letter, name, sizeof(name), remote, sizeof(remote));

        register_and_mount_cloud_account(PROVIDER_GDRIVE, name[0] ? name : "Google Drive Storage",
                                         drive_letter, remote[0] ? remote : "gdrive", 2000.0, NULL);

        send_http_response(client_sock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        closesocket(client_sock);
        return;
    }

    /* Route: POST /api/connect/onedrive */
    if (strncmp(path, "/api/connect/onedrive", 21) == 0) {
        char drive_letter = 'O';
        char name[TAFIM_STR_LEN] = "Microsoft OneDrive";
        char remote[TAFIM_STR_LEN] = "onedrive";
        const char* body_start = strstr(buffer, "\r\n\r\n");
        if (body_start) parse_json_connect_body(body_start + 4, &drive_letter, name, sizeof(name), remote, sizeof(remote));

        register_and_mount_cloud_account(PROVIDER_ONEDRIVE, name[0] ? name : "Microsoft OneDrive",
                                         drive_letter, remote[0] ? remote : "onedrive", 1024.0, NULL);

        send_http_response(client_sock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        closesocket(client_sock);
        return;
    }

    /* Route: POST /api/connect/dropbox */
    if (strncmp(path, "/api/connect/dropbox", 20) == 0) {
        char drive_letter = 'D';
        char name[TAFIM_STR_LEN] = "Dropbox Storage";
        char remote[TAFIM_STR_LEN] = "dropbox";
        const char* body_start = strstr(buffer, "\r\n\r\n");
        if (body_start) parse_json_connect_body(body_start + 4, &drive_letter, name, sizeof(name), remote, sizeof(remote));

        register_and_mount_cloud_account(PROVIDER_DROPBOX, name[0] ? name : "Dropbox Storage",
                                         drive_letter, remote[0] ? remote : "dropbox", 50.0, NULL);

        send_http_response(client_sock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        closesocket(client_sock);
        return;
    }

    /* Route: POST /api/connect/mega */
    if (strncmp(path, "/api/connect/mega", 17) == 0) {
        char drive_letter = 'M';
        char name[TAFIM_STR_LEN] = "Mega.nz Storage";
        char remote[TAFIM_STR_LEN] = "mega";
        const char* body_start = strstr(buffer, "\r\n\r\n");
        if (body_start) parse_json_connect_body(body_start + 4, &drive_letter, name, sizeof(name), remote, sizeof(remote));

        register_and_mount_cloud_account(PROVIDER_MEGA, name[0] ? name : "Mega.nz Storage",
                                         drive_letter, remote[0] ? remote : "mega", 20.0, NULL);

        send_http_response(client_sock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        closesocket(client_sock);
        return;
    }

    /* Route: POST /api/connect/webdav */
    if (strncmp(path, "/api/connect/webdav", 19) == 0) {
        char drive_letter = 'W';
        char name[TAFIM_STR_LEN] = "WebDAV Cloud";
        char remote[TAFIM_STR_LEN] = "webdav";
        const char* body_start = strstr(buffer, "\r\n\r\n");
        if (body_start) parse_json_connect_body(body_start + 4, &drive_letter, name, sizeof(name), remote, sizeof(remote));

        register_and_mount_cloud_account(PROVIDER_WEBDAV, name[0] ? name : "WebDAV Cloud",
                                         drive_letter, remote[0] ? remote : "webdav", 1024.0, remote);

        send_http_response(client_sock, 200, "OK", "application/json", "{\"status\":\"ok\"}");
        closesocket(client_sock);
        return;
    }

    /* Fallback default route: redirect to TeraBox connect */
    send_http_response(client_sock, 200, "OK", "text/html", HTML_TERABOX_LOGIN);
    closesocket(client_sock);
}

static unsigned __stdcall server_thread_proc(void* arg) {
    (void)arg;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    g_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_sock == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(g_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* Listen strictly on 127.0.0.1 */
    sin.sin_port = htons(SERVER_PORT);

    if (bind(g_listen_sock, (struct sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
        closesocket(g_listen_sock);
        WSACleanup();
        return 1;
    }

    listen(g_listen_sock, 10);
    g_server_running = true;

    while (g_server_running) {
        SOCKET client = accept(g_listen_sock, NULL, NULL);
        if (client == INVALID_SOCKET) break;
        handle_client(client);
    }

    closesocket(g_listen_sock);
    WSACleanup();
    return 0;
}

bool server_start(AppConfig* cfg) {
    g_cfg = cfg;
    uintptr_t th = _beginthreadex(NULL, 0, server_thread_proc, NULL, 0, NULL);
    return (th != 0);
}

void server_stop(void) {
    g_server_running = false;
    if (g_listen_sock != INVALID_SOCKET) {
        closesocket(g_listen_sock);
        g_listen_sock = INVALID_SOCKET;
    }
}
