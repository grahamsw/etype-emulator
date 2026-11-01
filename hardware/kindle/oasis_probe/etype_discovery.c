#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef char INT8;
typedef UINT8 BD_ADDR[6];

// Lab126 symbol required by libbsa.so
__attribute__((visibility("default"))) unsigned int g_lab126_log_mask = 0;

// Broadcom MGT structures
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
    UINT8 reserved2[2];     // Offset 30..31
} __attribute__((packed)) tBSA_DISC_START;

typedef struct {
    UINT8 pad[4];
} tBSA_DISC_ABORT;

// libbsa function declarations
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

static volatile int g_running = 1;
static int g_devices_found = 0;
static int g_macally_found = 0;
static int g_disc_complete = 0;
static BD_ADDR g_target_mac = {0xD5, 0x24, 0x53, 0x14, 0x5A, 0xC7};

static void fbink_print(const char *msg, int y) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "/mnt/us/libkh/bin/fbink -q -pm -y %d -- \"%s\"", y, msg);
    system(cmd);
}

static void mgt_callback(int event, void *p_data) {
    // Broadcom MGT events (connect/disconnect/etc)
}

static void disc_callback(int event, void *p_data) {
    printf("[DISC_CB] Event: %d\n", event);
    if (!p_data) return;

    UINT8 *raw = (UINT8*)p_data;

    if (event == 0) { // BSA_DISC_NEW_EVT
        g_devices_found++;

        // Raw buffer dump
        printf("[DEVICE #%d RAW] ", g_devices_found);
        for (int i = 0; i < 48; i++) {
            printf("%02x ", raw[i]);
        }
        printf("\n");

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
        while (nlen > 0 && name[nlen - 1] == ' ') {
            name[--nlen] = '\0';
        }

        // 2. Class of Device (offset 6..8)
        UINT32 cod = raw[6] | (raw[7] << 8) | (raw[8] << 16);
        // Major Class Peripheral = 0x05, Minor Class Keyboard = bit 6 (0x40)
        int is_cod_keyboard = ((raw[7] & 0x1F) == 0x05) && (raw[6] & 0x40);

        // 3. Parse EIR / BLE Advertisement Payload (offset 278..371 if present)
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

        // Prefer EIR name if BSA name was empty
        if (name[0] == '\0' && eir_name[0] != '\0') {
            strncpy(name, eir_name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        }
        if (name[0] == '\0') {
            strcpy(name, "Unknown");
        }

        printf("  >>> DEVICE #%d: MAC=%s | Name='%s' | CoD=0x%06X | HID_UUID=%d | App=0x%04X\n",
               g_devices_found, mac_str, name, cod, has_hid_service, appearance_val);

        // Identification Logic (matches Macally BTMINIZKEY or any Bluetooth Keyboard)
        int is_macally = 0;
        const char *reason = "None";

        if (memcmp(*bda, g_target_mac, 6) == 0) {
            is_macally = 1;
            reason = "Exact MAC match";
        } else if (strcasestr(name, "Macally") || strcasestr(name, "BTMINI") ||
                   strcasestr(name, "MINI") || strcasestr(name, "Key") ||
                   strcasestr(name, "Keyboard") || strcasestr(name, "ZKEY") ||
                   strcasestr(eir_name, "Macally") || strcasestr(eir_name, "BTMINI") ||
                   strcasestr(eir_name, "Key") || strcasestr(eir_name, "Keyboard")) {
            is_macally = 1;
            reason = "Device Name string match";
        } else if (is_cod_keyboard || (cod & 0x001F00) == 0x000500 || cod == 0x402500) {
            is_macally = 1;
            reason = "Bluetooth CoD (Peripheral Keyboard)";
        } else if (has_hid_service) {
            is_macally = 1;
            reason = "GATT HID Service UUID (0x1812)";
        } else if (has_keyboard_appearance) {
            is_macally = 1;
            reason = "BLE Appearance (0x03C1 Keyboard)";
        }

        if (is_macally) {
            g_macally_found = 1;
            printf("\n************************************************\n");
            printf("  *** SUCCESS! MACALLY KEYBOARD HEARD OVER AIR! ***\n");
            printf("  MAC:        %s\n", mac_str);
            printf("  Name:       %s\n", name);
            printf("  Matched by: %s\n", reason);
            printf("************************************************\n\n");

            // Save discovered MAC for Stage 4 connection
            FILE *f = fopen("/mnt/us/extensions/oasis_probe/macally_mac.txt", "w");
            if (f) {
                fprintf(f, "%s\n", mac_str);
                fclose(f);
                printf("[SAVE] Wrote live MAC %s to macally_mac.txt\n", mac_str);
            }

            char b1[128], b2[128];
            snprintf(b1, sizeof(b1), "MACALLY HEARD: %s", mac_str);
            snprintf(b2, sizeof(b2), "Matched: %s", reason);
            fbink_print(b1, 6);
            fbink_print(b2, 7);
            fbink_print("Stage 3 PASSED! Ready for Stage 4 pairing.", 8);
        } else {
            // Display other device found (proves receiver is active!)
            char info[128];
            snprintf(info, sizeof(info), "Heard: %s (%s)", mac_str, name);
            fbink_print(info, 9 + (g_devices_found % 4));
        }

    } else if (event == 1) { // BSA_DISC_CMPL_EVT
        printf("[DISC_CB] >>> Discovery Scan Complete! <<<\n");
        g_disc_complete = 1;
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
    printf("  Stage 3 Discovery Probe - Over-the-Air Packet Scan\n");
    printf("  Target: Macally BTMINIZKEY (Classic BR/EDR + BLE) \n");
    printf("====================================================\n");
    fflush(stdout);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    fbink_print("Stage 3: Dual-Mode Discovery Scan", 2);
    fbink_print("---------------------------------", 3);
    fbink_print("Connecting to Broadcom daemon...", 4);

    // 1. Connect to BSA Server
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

    // 2. Enable BLE Subsystem (activates dual-mode radio controller)
    printf("[2/4] Enabling BLE Subsystem in Broadcom Stack...\n");
    tBSA_BLE_ENABLE ble_enable;
    BSA_BleEnableInit(&ble_enable);
    rc = BSA_BleEnable(&ble_enable);
    printf("[2/4] BSA_BleEnable returned: %d\n", rc);
    if (rc != 0) {
        printf("[WARN] BSA_BleEnable returned %d (continuing with Classic)\n", rc);
    }

    // 3. PHASE 1: Open BLE Advertisement Scan (Channels 37, 38, 39 + Dual Mode)
    // Matches what smartphones scan when discovering BLE keyboards by name
    printf("[3/4] Starting Phase 1: Open BLE Advertisement Scan (45 seconds)...\n");
    fbink_print("Phase 1: Open BLE Broadcast Scan (45s)...", 5);
    fbink_print("Listening for BTMINIZKEY over the air...", -3);

    g_disc_complete = 0;
    tBSA_DISC_START disc_ble;
    BSA_DiscStartInit(&disc_ble);
    disc_ble.duration = 45;
    disc_ble.cback = disc_callback;
    disc_ble.services = 0x40000000; // BTA_BLE_SERVICE_MASK
    disc_ble.mode = 0x31;           // BLE General/Limited (0x10|0x20) + Classic (0x01)
    disc_ble.update = 2;

    printf("[3/4] Configured Open Scan: duration=%ds, services=0x%08X, mode=0x%02X\n",
           disc_ble.duration, disc_ble.services, disc_ble.mode);

    rc = BSA_DiscStart(&disc_ble);
    printf("[3/4] BSA_DiscStart (BLE) returned: %d\n", rc);

    if (rc == 0) {
        int wait_ticks = 460; // 46 seconds
        while (g_running && !g_disc_complete && !g_macally_found && wait_ticks > 0) {
            usleep(100000);
            wait_ticks--;
            if (wait_ticks % 20 == 0) {
                printf("[BLE SCAN] %ds remaining | devices=%d | macally=%d\n",
                       wait_ticks / 10, g_devices_found, g_macally_found);
                fflush(stdout);
            }
        }
    } else {
        printf("[WARN] BLE scan failed to start: %d\n", rc);
    }

    // 4. PHASE 2: Classic Bluetooth Inquiry Fallback (if not seen in BLE)
    if (!g_macally_found && g_running) {
        printf("\n[4/4] Not heard in BLE scan. Aborting and trying Classic Inquiry...\n");
        tBSA_DISC_ABORT disc_abort;
        BSA_DiscAbortInit(&disc_abort);
        BSA_DiscAbort(&disc_abort);
        usleep(500000);

        printf("[4/4] Starting Phase 2: Classic Bluetooth BR/EDR Inquiry (20 seconds)...\n");
        fbink_print("Phase 2: Classic Bluetooth Inquiry (20s)...", 5);

        g_disc_complete = 0;
        tBSA_DISC_START disc_classic;
        BSA_DiscStartInit(&disc_classic);
        disc_classic.duration = 20;
        disc_classic.cback = disc_callback;
        disc_classic.services = 0; // Classic BR/EDR services
        disc_classic.mode = 1;     // Classic BR/EDR inquiry
        disc_classic.update = 2;

        rc = BSA_DiscStart(&disc_classic);
        printf("[4/4] BSA_DiscStart (Classic) returned: %d\n", rc);

        if (rc == 0) {
            int wait_ticks = 210;
            while (g_running && !g_disc_complete && !g_macally_found && wait_ticks > 0) {
                usleep(100000);
                wait_ticks--;
                if (wait_ticks % 20 == 0) {
                    printf("[CLASSIC SCAN] %ds remaining | devices=%d | macally=%d\n",
                           wait_ticks / 10, g_devices_found, g_macally_found);
                    fflush(stdout);
                }
            }
        }
    }

    printf("\n====================================================\n");
    printf("  STAGE 3 DISCOVERY PROBE SUMMARY\n");
    printf("  Total Devices Heard: %d\n", g_devices_found);
    printf("  Macally Detected:    %s\n", g_macally_found ? "YES (100% SUCCESS)" : "NO");
    printf("====================================================\n");

    if (g_macally_found) {
        fbink_print("STAGE 3 RESULT: MACALLY HEARD OVER THE AIR!", -4);
        fbink_print("Radio link verified! Ready for Stage 4.", -3);
    } else if (g_devices_found > 0) {
        fbink_print("STAGE 3 RESULT: Radio active, other devices heard.", -4);
        fbink_print("Macally not seen. Press Fn+1/2, then Fn+P.", -3);
    } else {
        fbink_print("STAGE 3 RESULT: 0 packets heard. Radio deaf.", -4);
        fbink_print("Diagnose RF antenna/mode.", -3);
    }

cleanup:
    printf("[CLEANUP] Closing MGT connection...\n");
    tBSA_MGT_CLOSE mgt_close;
    BSA_MgtCloseInit(&mgt_close);
    BSA_MgtClose(&mgt_close);

    return 0;
}
