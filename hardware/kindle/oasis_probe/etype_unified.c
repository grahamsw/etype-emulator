#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef char INT8;
typedef UINT8 BD_ADDR[6];

__attribute__((visibility("default"))) unsigned int g_lab126_log_mask = 0;

// Broadcom UUID structure (20 bytes)
typedef struct {
    UINT16 len;
    UINT8 pad[2];
    union {
        UINT16 uuid16;
        UINT32 uuid32;
        UINT8 uuid128[16];
    } uu;
} __attribute__((packed)) tBT_UUID;

// BSA MGT structures
typedef void (*tBSA_MGT_CBACK)(int event, void *p_data);
typedef struct {
    char server_path[100];
    tBSA_MGT_CBACK cback;
} tBSA_MGT_OPEN;

typedef struct {
    UINT8 pad[4];
} tBSA_MGT_CLOSE;

typedef struct {
    UINT8 pad[4];
} tBSA_BLE_ENABLE;

// Broadcom Discovery structures (32 bytes verified against bsa_server binary disassembly)
typedef void (*tBSA_DISC_CBACK)(int event, void *p_data);

typedef struct {
    UINT16 duration;        // Offset 0 (seconds, max 48)
    UINT8 pad[2];           // Offset 2..3
    tBSA_DISC_CBACK cback;  // Offset 4..7 (callback function)
    UINT32 p_cback_data;    // Offset 8..11 (user data / max_res)
    UINT8 sub_mode;         // Offset 12 (0 = normal search)
    BD_ADDR bd_addr;        // Offset 13..18 (target device address)
    UINT8 pad2;             // Offset 19
    UINT32 services;        // Offset 20..23 (tBTA_SERVICE_MASK: 0x40000000 = BTA_BLE_SERVICE_MASK)
    UINT8 mode;             // Offset 24 (tBTA_DM_INQ_MODE: 0x10=BLE General, 0x20=BLE Limited, 0x01=Classic)
    UINT8 is_ext;           // Offset 25
    UINT8 reserved[2];      // Offset 26..27
    UINT8 update;           // Offset 28 (<= 2)
    INT8 tx_power;          // Offset 29 (-70..+20)
    UINT8 pad3[2];          // Offset 30..31
} __attribute__((packed)) tBSA_DISC_START;

typedef struct {
    UINT8 pad[4];
} tBSA_DISC_ABORT;

// BSA BLE Client structures
typedef void (*tBSA_BLE_CL_CBACK)(int event, void *p_data);

typedef struct {
    tBT_UUID uuid;
    UINT8 client_if;
    UINT8 pad[3];
    tBSA_BLE_CL_CBACK p_cback;
} __attribute__((packed)) tBSA_BLE_CL_APP_REG;

// Wire-aligned 8-byte BLE connect struct
typedef struct {
    UINT8 client_if;
    BD_ADDR bd_addr;
    UINT8 direct;
} __attribute__((packed)) tBSA_BLE_CL_CONNECT;

typedef struct {
    UINT16 status;
    UINT16 conn_id;
    UINT8 client_if;
    BD_ADDR bd_addr;
    UINT8 pad;
} __attribute__((packed)) tBSA_BLE_CL_OPEN_MSG;

typedef struct {
    UINT16 conn_id;
    UINT8 pad[2];
    tBT_UUID uuid;
} __attribute__((packed)) tBSA_BLE_CL_SEARCH;

typedef struct {
    UINT8 client_if;
    BD_ADDR bd_addr;
    UINT8 pad;
    UINT8 char_data[56];
} __attribute__((packed)) tBSA_BLE_CL_NOTIFREG;

// libbsa function imports
extern int BSA_MgtOpenInit(tBSA_MGT_OPEN *p_req);
extern int BSA_MgtOpen(tBSA_MGT_OPEN *p_req);
extern int BSA_MgtCloseInit(tBSA_MGT_CLOSE *p_req);
extern int BSA_MgtClose(tBSA_MGT_CLOSE *p_req);

extern int BSA_BleEnableInit(tBSA_BLE_ENABLE *p_req);
extern int BSA_BleEnable(tBSA_BLE_ENABLE *p_req);

extern int BSA_DiscStartInit(tBSA_DISC_START *p_req);
extern int BSA_DiscStart(tBSA_DISC_START *p_req);
extern int BSA_DiscAbortInit(tBSA_DISC_ABORT *p_req);
extern int BSA_DiscAbort(tBSA_DISC_ABORT *p_req);

extern int BSA_BleClAppRegisterInit(tBSA_BLE_CL_APP_REG *p_req);
extern int BSA_BleClAppRegister(tBSA_BLE_CL_APP_REG *p_req);

extern int BSA_BleClConnectInit(tBSA_BLE_CL_CONNECT *p_req);
extern int BSA_BleClConnect(tBSA_BLE_CL_CONNECT *p_req);

extern int BSA_BleClSearchInit(tBSA_BLE_CL_SEARCH *p_req);
extern int BSA_BleClSearch(tBSA_BLE_CL_SEARCH *p_req);

extern int BSA_BleClNotifRegisterInit(tBSA_BLE_CL_NOTIFREG *p_req);
extern int BSA_BleClNotifRegister(tBSA_BLE_CL_NOTIFREG *p_req);

// Global State
static volatile int g_running = 1;
static volatile int g_connected = 0;
static volatile int g_connecting = 0;
static volatile int g_conn_id = 0;
static volatile int g_client_if = 0;
static volatile int g_keys_received = 0;
static BD_ADDR g_connected_mac = {0};

static char g_typed_line[256] = "";
static int g_typed_len = 0;

static void fbink_print(const char *msg, int y) {
    char cmd[512];
    // Native Amazon e-ink print (always works on every Kindle without dependencies!)
    int row = (y > 0) ? y : (25 + y);
    snprintf(cmd, sizeof(cmd), "/usr/sbin/eips 1 %d \"%s\" 2>/dev/null", row, msg);
    system(cmd);

    // Also run fbink if present
    snprintf(cmd, sizeof(cmd), "/mnt/us/libkh/bin/fbink -q -pm -y %d -- \"%s\" 2>/dev/null", y, msg);
    system(cmd);
}

static char hid_to_ascii(UINT8 mod, UINT8 code) {
    int shift = (mod & 0x22) != 0; // Left or Right Shift
    if (code >= 0x04 && code <= 0x1D) {
        char base = shift ? 'A' : 'a';
        return base + (code - 0x04);
    }
    if (code >= 0x1E && code <= 0x27) {
        const char *nums = "1234567890";
        const char *syms = "!@#$%^&*()";
        int idx = (code == 0x27) ? 9 : (code - 0x1E);
        return shift ? syms[idx] : nums[idx];
    }
    if (code == 0x28) return '\n'; // Enter
    if (code == 0x2C) return ' ';  // Space
    if (code == 0x2D) return shift ? '_' : '-';
    if (code == 0x2E) return shift ? '+' : '=';
    if (code == 0x2F) return shift ? '{' : '[';
    if (code == 0x30) return shift ? '}' : ']';
    if (code == 0x31) return shift ? '|' : '\\';
    if (code == 0x33) return shift ? ':' : ';';
    if (code == 0x34) return shift ? '"' : '\'';
    if (code == 0x36) return shift ? '<' : ',';
    if (code == 0x37) return shift ? '>' : '.';
    if (code == 0x38) return shift ? '?' : '/';
    return 0;
}

static void mgt_callback(int event, void *p_data) {
    // MGT events
}

static void disc_callback(int event, void *p_data);

static void ble_cl_callback(int event, void *p_data) {
    printf("[BLE_CL_CB] Event: %d\n", event);
    if (!p_data) return;

    UINT8 *raw = (UINT8*)p_data;
    printf("[BLE_CL_CB Raw %d] bytes: ", event);
    for (int i = 0; i < 16; i++) printf("%02x ", raw[i]);
    printf("\n");

    if (event == 1) { // BSA_BLE_CL_OPEN_EVT
        tBSA_BLE_CL_OPEN_MSG *msg = (tBSA_BLE_CL_OPEN_MSG*)p_data;
        printf("[BLE_CL_CB] >>> BSA_BLE_CL_OPEN_EVT: status=%d, conn_id=%d, client_if=%d <<<\n",
               msg->status, msg->conn_id, msg->client_if);

        if (msg->status == 0) {
            printf("\n************************************************\n");
            printf("  *** SUCCESS! KEYBOARD CONNECTED OVER BLE! ***\n");
            printf("************************************************\n\n");
            g_connected = 1;
            g_connecting = 0;
            g_conn_id = msg->conn_id;

            fbink_print("KEYBOARD CONNECTED (SOLID LED)!", -5);
            fbink_print("Configuring GATT HID link...", -4);

            // Discover GATT Services
            tBSA_BLE_CL_SEARCH search_req;
            BSA_BleClSearchInit(&search_req);
            search_req.conn_id = msg->conn_id;
            int src = BSA_BleClSearch(&search_req);
            printf("[BLE_CL_CB] BSA_BleClSearch returned: %d\n", src);
        } else {
            printf("[BLE_CL_CB] Connection failed with status=%d\n", msg->status);
            g_connecting = 0;
            char err[64];
            snprintf(err, sizeof(err), "Link retry (status=%d)... Scanning...", msg->status);
            fbink_print(err, -4);

            // Re-arm continuous discovery scan so we keep searching
            tBSA_DISC_START disc_start;
            BSA_DiscStartInit(&disc_start);
            disc_start.duration = 45;
            disc_start.cback = disc_callback;
            disc_start.services = 0x40000000; // BLE + BR/EDR
            disc_start.mode = 0x31;
            disc_start.update = 2;
            BSA_DiscStart(&disc_start);
        }
    } else if (event == 2 || event == 4) { // BSA_BLE_CL_CLOSE_EVT
        printf("[BLE_CL_CB] Disconnected (evt=%d)\n", event);
        g_connected = 0;
        fbink_print("Keyboard Disconnected", -4);
    } else if (event == 6) { // BSA_BLE_CL_SEARCH_CMPL_EVT
        printf("[BLE_CL_CB] >>> GATT Discovery Complete! Subscribing to HID... <<<\n");
        fbink_print("Typewriter Ready! Start typing now...", -3);

        tBSA_BLE_CL_NOTIFREG notif_req;
        BSA_BleClNotifRegisterInit(&notif_req);
        notif_req.client_if = g_client_if;
        memcpy(notif_req.bd_addr, g_connected_mac, 6);
        int nrc = BSA_BleClNotifRegister(&notif_req);
        printf("[BLE_CL_CB] BSA_BleClNotifRegister returned: %d\n", nrc);
    } else if (event == 7) { // BSA_BLE_CL_NOTIF_EVT (Keystroke report)
        g_keys_received++;
        UINT16 val_len = *(UINT16*)(raw + 84);
        UINT8 *val = raw + 86;

        if (val_len >= 3) {
            UINT8 mod = val[0];
            UINT8 code = val[2]; // primary key

            // Clean Exit shortcut: Esc (0x29) or Ctrl+Q (mod 0x01/0x10, code 0x14)
            if (code == 0x29 || (((mod & 0x11) != 0) && code == 0x14)) {
                printf("[KEY] Exit shortcut pressed (Esc / Ctrl+Q). Exiting E-Type...\n");
                fbink_print("Exiting E-Type session...", -2);
                g_running = 0;
                return;
            }

            if (code != 0) {
                char ch = hid_to_ascii(mod, code);
                printf("[KEY] HID=0x%02x Mod=0x%02x -> '%c'\n", code, mod, ch ? ch : '?');
                if (ch) {
                    if (ch == '\n') {
                        // Enter pressed: clear line
                        g_typed_len = 0;
                        g_typed_line[0] = '\0';
                        fbink_print("                                        ", 10);
                    } else if (g_typed_len < 38) {
                        g_typed_line[g_typed_len++] = ch;
                        g_typed_line[g_typed_len] = '\0';
                    }
                    char out[64];
                    snprintf(out, sizeof(out), "> %s_", g_typed_line);
                    fbink_print(out, 10);
                }
            }
        }
    }
    fflush(stdout);
}

static int g_devices_found = 0;

static void disc_callback(int event, void *p_data) {
    if (event == 1) { // BSA_DISC_CMPL_EVT
        printf("[DISC_CB] Scan cycle completed. Re-arming continuous scan...\n");
        if (!g_connected && !g_connecting && g_running) {
            tBSA_DISC_START disc_start;
            BSA_DiscStartInit(&disc_start);
            disc_start.duration = 45;
            disc_start.cback = disc_callback;
            disc_start.services = 0x40000000;
            disc_start.mode = 0x31;
            disc_start.update = 2;
            BSA_DiscStart(&disc_start);
        }
        return;
    }
    if (event != 0 || !p_data) return; // BSA_DISC_NEW_EVT
    if (g_connected || g_connecting) return;

    g_devices_found++;
    UINT8 *raw = (UINT8*)p_data;
    BD_ADDR *bda = (BD_ADDR*)raw;
    char mac_str[24];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             (*bda)[0], (*bda)[1], (*bda)[2], (*bda)[3], (*bda)[4], (*bda)[5]);

    // 1. Device Name from BSA name field (offset 9..257)
    char name[64] = "";
    if (raw[9] >= 0x20 && raw[9] <= 0x7E) {
        int len = 0;
        while (len < 63 && raw[9 + len] >= 0x20 && raw[9 + len] <= 0x7E) {
            name[len] = (char)raw[9 + len];
            len++;
        }
        name[len] = '\0';
    }
    // Fallback scan for string if offset 9 was empty
    if (name[0] == '\0') {
        for (int i = 9; i < 40; i++) {
            if (raw[i] >= 0x20 && raw[i] <= 0x7E && raw[i+1] >= 0x20 && raw[i+1] <= 0x7E) {
                int len = 0;
                while (len < 63 && (raw[i + len] >= 0x20 && raw[i + len] <= 0x7E)) {
                    name[len] = (char)raw[i + len];
                    len++;
                }
                name[len] = '\0';
                break;
            }
        }
    }
    // Trim trailing spaces
    int nlen = strlen(name);
    while (nlen > 0 && name[nlen - 1] == ' ') name[--nlen] = '\0';

    // 2. Class of Device (offset 6..8)
    UINT32 cod = raw[6] | (raw[7] << 8) | (raw[8] << 16);
    int is_cod_keyboard = ((raw[7] & 0x1F) == 0x05) && (raw[6] & 0x40);

    // 3. Parse EIR / BLE Advertisement Payload (offset 278..371)
    int has_hid_service = 0;
    int has_keyboard_appearance = 0;
    UINT16 appearance_val = 0;
    char eir_name[64] = "";

    UINT8 *eir = raw + 278;
    int idx = 0;
    while (idx < 90) {
        UINT8 ad_len = eir[idx];
        if (ad_len == 0 || idx + 1 + ad_len > 94) break;
        UINT8 ad_type = eir[idx + 1];
        UINT8 *ad_data = &eir[idx + 2];
        UINT8 data_len = ad_len - 1;

        if (ad_type == 0x08 || ad_type == 0x09) { // Shortened or Complete Local Name
            int elen = (data_len < 63) ? data_len : 63;
            memcpy(eir_name, ad_data, elen);
            eir_name[elen] = '\0';
        } else if (ad_type == 0x02 || ad_type == 0x03) { // 16-bit Service UUIDs
            for (int u = 0; u + 1 < data_len; u += 2) {
                UINT16 uuid = ad_data[u] | (ad_data[u+1] << 8);
                if (uuid == 0x1812) has_hid_service = 1; // HID Service
            }
        } else if (ad_type == 0x19 && data_len >= 2) { // Appearance
            appearance_val = ad_data[0] | (ad_data[1] << 8);
            if (appearance_val == 0x03C1 || (appearance_val >= 0x03C0 && appearance_val <= 0x03C4)) {
                has_keyboard_appearance = 1;
            }
        }
        idx += (1 + ad_len);
    }

    if (name[0] == '\0' && eir_name[0] != '\0') {
        strncpy(name, eir_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    if (name[0] == '\0') strcpy(name, "Unknown");

    printf("[DISCOVERY #%d] Heard: MAC=%s Name='%s' CoD=0x%06X HID=%d App=0x%04X\n",
           g_devices_found, mac_str, name, cod, has_hid_service, appearance_val);

    // Live ambient screen feedback so user visibly sees scanner working
    char ambient_info[128];
    snprintf(ambient_info, sizeof(ambient_info), "Scanning... Heard #%d: %s (%s)",
             g_devices_found, mac_str, name);
    fbink_print(ambient_info, 9 + (g_devices_found % 4));

    // Matching Criteria: Samsers 5.1 / BLE HID Peripheral / Keyboard
    int match = 0;
    const char *matched_by = "None";

    // Known Classic-only keyboards from earlier tests
    int is_old_classic_keyboard = (strcmp(mac_str, "13:07:10:00:05:19") == 0 ||
                                   strcmp(mac_str, "D5:24:53:14:5A:C7") == 0);

    if (is_old_classic_keyboard) {
        printf("[DISCOVERY] Notice: %s (%s) is old Classic-only keyboard. Skipping.\n",
               name, mac_str);
        match = 0;
    } else if (strcasestr(name, "Samsers") || strcasestr(eir_name, "Samsers") ||
               strcasestr(name, "KF08") || strcasestr(eir_name, "KF08")) {
        match = 1;
        matched_by = "Samsers KF08 keyboard string";
    } else if (has_hid_service) {
        match = 1;
        matched_by = "GATT HID Service UUID (0x1812)";
    } else if (has_keyboard_appearance) {
        match = 1;
        matched_by = "BLE Appearance (0x03C1 Keyboard)";
    } else if (strcasestr(name, "5.1") || strcasestr(eir_name, "5.1") ||
               strcasestr(name, "BT5") || strcasestr(eir_name, "BT5")) {
        match = 1;
        matched_by = "BT 5.1 keyboard string";
    } else if ((strcasestr(name, "Keyboard") || strcasestr(name, "Key") ||
                strcasestr(eir_name, "Keyboard") || strcasestr(eir_name, "Key")) && cod == 0) {
        match = 1;
        matched_by = "BLE Keyboard name string";
    }

    if (match && !g_connecting && !g_connected) {
        g_connecting = 1;
        memcpy(g_connected_mac, *bda, 6);

        printf("\n==================================================\n");
        printf("  *** TARGET KEYBOARD DETECTED OVER THE AIR! ***\n");
        printf("  MAC:  %s\n", mac_str);
        printf("  Name: %s\n", name);
        printf("  By:   %s\n", matched_by);
        printf("==================================================\n\n");

        char s1[128], s2[128];
        snprintf(s1, sizeof(s1), "FOUND: %s (%s)", name, mac_str);
        snprintf(s2, sizeof(s2), "Linking to Broadcom radio live...");
        fbink_print(s1, 6);
        fbink_print(s2, 7);

        // Save MAC address
        FILE *f = fopen("/mnt/us/extensions/oasis_probe/macally_mac.txt", "w");
        if (f) {
            fprintf(f, "%s\n", mac_str);
            fclose(f);
        }

        // 1. Stop Discovery Scan so radio focuses on connection
        printf("[CONNECT] Stopping discovery scan...\n");
        tBSA_DISC_ABORT disc_abort;
        BSA_DiscAbortInit(&disc_abort);
        BSA_DiscAbort(&disc_abort);

        // 2. Connect immediately while inquiry cache is hot!
        printf("[CONNECT] Dispatching live BSA_BleClConnect to %s...\n", mac_str);
        tBSA_BLE_CL_CONNECT ble_conn;
        BSA_BleClConnectInit(&ble_conn);
        ble_conn.client_if = g_client_if;
        memcpy(ble_conn.bd_addr, *bda, 6);
        ble_conn.direct = 1;

        int rc = BSA_BleClConnect(&ble_conn);
        printf("[CONNECT] BSA_BleClConnect returned: %d\n", rc);
    }
    fflush(stdout);
}

static void sig_handler(int sig) {
    printf("\n[SIG] Caught signal %d\n", sig);
    g_running = 0;
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    printf("====================================================\n");
    printf("  E-Type Live Unified Auto-Discovery & Typing Engine\n");
    printf("  Broadcom Native BCM4343W Bluetooth Subsystem      \n");
    printf("====================================================\n");
    fflush(stdout);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    fbink_print("E-Type Live Keyboard Engine", 2);
    fbink_print("---------------------------", 3);
    fbink_print("Connecting to Broadcom daemon...", 4);

    // 1. Open connection to BSA server
    tBSA_MGT_OPEN mgt_open;
    BSA_MgtOpenInit(&mgt_open);
    strncpy(mgt_open.server_path, "/var/local/zbluetooth/", sizeof(mgt_open.server_path) - 1);
    mgt_open.cback = mgt_callback;

    int rc = -1;
    for (int retry = 0; retry < 10 && rc != 0 && g_running; retry++) {
        rc = BSA_MgtOpen(&mgt_open);
        printf("[1/4] BSA_MgtOpen attempt %d: rc=%d\n", retry + 1, rc);
        if (rc != 0) usleep(500000);
    }
    if (rc != 0) {
        printf("[ERROR] Failed to connect to BSA server.\n");
        fbink_print("ERROR: Cannot connect to bsa_server", 5);
        return 1;
    }

    // 2. Enable BLE Subsystem
    printf("[2/4] Enabling BLE Subsystem in Broadcom Stack...\n");
    tBSA_BLE_ENABLE ble_enable;
    BSA_BleEnableInit(&ble_enable);
    rc = BSA_BleEnable(&ble_enable);
    printf("[2/4] BSA_BleEnable returned: %d\n", rc);

    // 3. Pre-register BLE Client Application
    printf("[3/4] Pre-registering BLE Client Application...\n");
    tBSA_BLE_CL_APP_REG app_reg;
    BSA_BleClAppRegisterInit(&app_reg);
    app_reg.uuid.len = 16;
    for (int i = 0; i < 16; i++) {
        app_reg.uuid.uu.uuid128[i] = (UINT8)(0x50 + i);
    }
    app_reg.p_cback = ble_cl_callback;
    rc = BSA_BleClAppRegister(&app_reg);
    g_client_if = app_reg.client_if;
    printf("[3/4] BSA_BleClAppRegister returned: %d, client_if=%d\n", rc, g_client_if);

    // 4. Start Live Discovery Scan
    printf("[4/4] Starting Live Over-the-Air Discovery Scan...\n");
    fbink_print("Searching for keyboard... (LED blinking)", 5);

    tBSA_DISC_START disc_start;
    BSA_DiscStartInit(&disc_start);
    disc_start.duration = 45;
    disc_start.cback = disc_callback;
    disc_start.services = 0x40000000; // BTA_BLE_SERVICE_MASK
    disc_start.mode = 0x31;           // Dual Mode: BLE + Classic
    disc_start.update = 2;

    rc = BSA_DiscStart(&disc_start);
    printf("[4/4] BSA_DiscStart returned: %d\n", rc);

    // 5. Active Loop (Typing Session)
    int session_ticks = 900; // 90 seconds (extends automatically while typing)
    while (g_running && session_ticks > 0) {
        usleep(100000);
        session_ticks--;

        if (session_ticks % 50 == 0) {
            printf("[SESSION] %ds remaining | connected=%d | keys=%d\n",
                   session_ticks / 10, g_connected, g_keys_received);
            fflush(stdout);
        }

        // Once connected, keep session alive for deep writing (1 hour = 36000 ticks)
        if (g_connected && session_ticks < 18000) {
            session_ticks = 36000;
        } else if (g_keys_received > 0 && session_ticks < 300) {
            session_ticks = 600; // Extend by 60 seconds of typing time
        }
    }

    printf("\n[SUMMARY] Session ended. connected=%d, keys=%d\n", g_connected, g_keys_received);
    if (g_connected) {
        fbink_print("Typing session complete.", -2);
    }

    printf("[CLEANUP] Closing MGT connection...\n");
    tBSA_MGT_CLOSE mgt_close;
    BSA_MgtCloseInit(&mgt_close);
    BSA_MgtClose(&mgt_close);

    return 0;
}
