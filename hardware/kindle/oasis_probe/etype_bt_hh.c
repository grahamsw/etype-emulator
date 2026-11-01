#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;

typedef UINT8 BD_ADDR[6];

// Amazon Lab126 symbol required by libbsa.so
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

// BSA BLE structures
typedef struct {
    UINT8 pad[4];
} tBSA_BLE_ENABLE;

typedef void (*tBSA_BLE_CL_CBACK)(int event, void *p_data);

// Total size: 28 bytes
// Offset 0..19: uuid (20 bytes)
// Offset 20: client_if (1 byte)
// Offset 21..23: pad (3 bytes)
// Offset 24..27: p_cback (4 bytes at 0x18)
typedef struct {
    tBT_UUID uuid;
    UINT8 client_if;
    UINT8 pad[3];
    tBSA_BLE_CL_CBACK p_cback;
} __attribute__((packed)) tBSA_BLE_CL_APP_REG;

// Total size: 8 bytes
// Broadcom BSA BLE Connect structure:
// Offset 0: client_if (1 byte)
// Offset 1..6: bd_addr (6 bytes)
// Offset 7: direct (1 byte)
typedef struct {
    UINT8 client_if;
    BD_ADDR bd_addr;
    UINT8 direct;
} __attribute__((packed)) tBSA_BLE_CL_CONNECT;

// Total size: 12 bytes
typedef struct {
    UINT16 status;
    UINT16 conn_id;
    UINT8 client_if;
    BD_ADDR bd_addr;
    UINT8 pad;
} __attribute__((packed)) tBSA_BLE_CL_OPEN_MSG;

// Total size: 24 bytes
typedef struct {
    UINT16 conn_id;
    UINT8 pad[2];
    tBT_UUID uuid;
} __attribute__((packed)) tBSA_BLE_CL_SEARCH;

// Total size: 64 bytes
typedef struct {
    UINT8 client_if;
    BD_ADDR bd_addr;
    UINT8 pad;
    UINT8 char_data[56];
} __attribute__((packed)) tBSA_BLE_CL_NOTIFREG;

// Security structures
typedef struct {
    BD_ADDR bd_addr;
} __attribute__((packed)) tBSA_SEC_BOND;

typedef struct {
    BD_ADDR bd_addr;
    UINT8 accept;
    UINT8 pad;
} __attribute__((packed)) tBSA_SEC_SP_CFM_REPLY;

// Function declarations from libbsa
extern int BSA_MgtOpenInit(tBSA_MGT_OPEN *p_req);
extern int BSA_MgtOpen(tBSA_MGT_OPEN *p_req);

typedef struct {
    UINT8 pad[4];
} tBSA_MGT_CLOSE;
extern int BSA_MgtCloseInit(tBSA_MGT_CLOSE *p_req);
extern int BSA_MgtClose(tBSA_MGT_CLOSE *p_req);

extern int BSA_BleEnableInit(tBSA_BLE_ENABLE *p_req);
extern int BSA_BleEnable(tBSA_BLE_ENABLE *p_req);

extern int BSA_BleClAppRegisterInit(tBSA_BLE_CL_APP_REG *p_req);
extern int BSA_BleClAppRegister(tBSA_BLE_CL_APP_REG *p_req);

extern int BSA_BleClConnectInit(tBSA_BLE_CL_CONNECT *p_req);
extern int BSA_BleClConnect(tBSA_BLE_CL_CONNECT *p_req);

extern int BSA_BleClSearchInit(tBSA_BLE_CL_SEARCH *p_req);
extern int BSA_BleClSearch(tBSA_BLE_CL_SEARCH *p_req);

extern int BSA_BleClNotifRegisterInit(tBSA_BLE_CL_NOTIFREG *p_req);
extern int BSA_BleClNotifRegister(tBSA_BLE_CL_NOTIFREG *p_req);

extern int BSA_SecBondInit(tBSA_SEC_BOND *p_req);
extern int BSA_SecBond(tBSA_SEC_BOND *p_req);

extern int BSA_SecSpCfmReplyInit(tBSA_SEC_SP_CFM_REPLY *p_req);
extern int BSA_SecSpCfmReply(tBSA_SEC_SP_CFM_REPLY *p_req);

static volatile int g_running = 1;
static volatile int g_connected = 0;
static volatile UINT16 g_conn_id = 0;
static volatile UINT8 g_client_if = 0;
static int g_keys_received = 0;
static BD_ADDR g_target_mac = {0xD5, 0x24, 0x53, 0x14, 0x5A, 0xC7};

static void fbink_print(const char *msg, int y) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "/mnt/us/libkh/bin/fbink -q -pm -y %d \"%s\"", y, msg);
    system(cmd);
}

static int parse_mac(const char *str, BD_ADDR mac) {
    int val = 0;
    int count = 0;
    int chars = 0;
    for (int i = 0; str[i] && count < 6; i++) {
        char c = str[i];
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit >= 0) {
            val = (val << 4) | digit;
            chars++;
            if (chars == 2) {
                mac[count++] = (UINT8)val;
                val = 0;
                chars = 0;
            }
        }
    }
    return (count == 6) ? 0 : -1;
}

// Convert USB HID keycode to ASCII character
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

void mgt_callback(int event, void *p_data) {
    printf("[MGT_CB] Event: %d (p_data=%p)\n", event, p_data);
    if (p_data) {
        UINT8 *b = (UINT8*)p_data;
        printf("[MGT_CB] Data[0]: %02x\n", b[0]);
        // Auto-accept simple pairing confirm request if received
        if (event == 18 || event == 19 || event == 20) {
            printf("[MGT_CB] Auto-accepting pairing confirmation request...\n");
            tBSA_SEC_SP_CFM_REPLY cfm_reply;
            BSA_SecSpCfmReplyInit(&cfm_reply);
            memcpy(cfm_reply.bd_addr, g_target_mac, 6);
            cfm_reply.accept = 1;
            BSA_SecSpCfmReply(&cfm_reply);
        }
    }
    fflush(stdout);
}

void ble_cl_callback(int event, void *p_data) {
    printf("[BLE_CL_CB] Event: %d (p_data=%p)\n", event, p_data);
    if (!p_data) {
        fflush(stdout);
        return;
    }

    UINT8 *raw = (UINT8*)p_data;
    printf("[BLE_CL_CB Raw %d] bytes: ", event);
    for (int i = 0; i < 24; i++) {
        printf("%02x ", raw[i]);
    }
    printf("\n");

    if (event == 1) { // BSA_BLE_CL_OPEN_EVT
        tBSA_BLE_CL_OPEN_MSG *msg = (tBSA_BLE_CL_OPEN_MSG*)p_data;
        printf("[BLE_CL_CB] >>> BSA_BLE_CL_OPEN_EVT: status=%d, conn_id=%d, client_if=%d, bd_addr=%02X:%02X:%02X:%02X:%02X:%02X <<<\n",
               msg->status, msg->conn_id, msg->client_if,
               msg->bd_addr[0], msg->bd_addr[1], msg->bd_addr[2],
               msg->bd_addr[3], msg->bd_addr[4], msg->bd_addr[5]);

        if (msg->status == 0) {
            printf("[BLE_CL_CB] *** SUCCESS! MACALLY KEYBOARD CONNECTED VIA BLE! ***\n");
            g_connected = 1;
            g_conn_id = msg->conn_id;

            fbink_print("MACALLY CONNECTED (BLE)!", -4);
            fbink_print("Discovering HID characteristics...", -3);

            // Trigger GATT service discovery
            tBSA_BLE_CL_SEARCH search_req;
            BSA_BleClSearchInit(&search_req);
            search_req.conn_id = msg->conn_id;
            int src = BSA_BleClSearch(&search_req);
            printf("[BLE_CL_CB] BSA_BleClSearch returned: %d\n", src);
        } else {
            printf("[BLE_CL_CB] Connection failed with status=%d\n", msg->status);
        }
    } else if (event == 2 || event == 4) { // BSA_BLE_CL_CLOSE_EVT
        printf("[BLE_CL_CB] >>> BSA_BLE_CL_CLOSE_EVT (disconnected, evt=%d) <<<\n", event);
        g_connected = 0;
        fbink_print("Keyboard Disconnected", -4);
    } else if (event == 5) { // BSA_BLE_CL_SEARCH_RES_EVT
        printf("[BLE_CL_CB] >>> GATT Service Discovered <<<\n");
    } else if (event == 6) { // BSA_BLE_CL_SEARCH_CMPL_EVT
        printf("[BLE_CL_CB] >>> GATT Service Search Complete! <<<\n");
        fbink_print("Ready! Type on keyboard now...", -3);

        // Register notification on client_if
        tBSA_BLE_CL_NOTIFREG notif_req;
        BSA_BleClNotifRegisterInit(&notif_req);
        notif_req.client_if = g_client_if;
        memcpy(notif_req.bd_addr, g_target_mac, 6);
        int nrc = BSA_BleClNotifRegister(&notif_req);
        printf("[BLE_CL_CB] BSA_BleClNotifRegister returned: %d\n", nrc);
    } else if (event == 7) { // BSA_BLE_CL_NOTIF_EVT
        g_keys_received++;
        UINT16 val_len = *(UINT16*)(raw + 84);
        UINT8 *val = raw + 86;

        printf("[BLE_KEY #%d] NOTIF len=%d:", g_keys_received, val_len);
        for (int i = 0; i < (val_len > 16 ? 16 : val_len); i++) {
            printf(" %02x", val[i]);
        }
        printf("\n");

        if (val_len >= 3) {
            UINT8 mod = val[0];
            UINT8 code = val[2]; // primary key
            if (code != 0) {
                char ch = hid_to_ascii(mod, code);
                printf("[KEYSTROKE] HID=%02x Mod=%02x -> ASCII '%c' (0x%02x)\n", code, mod, ch ? ch : '?', ch);
                if (ch) {
                    char display_buf[64];
                    snprintf(display_buf, sizeof(display_buf), "KEY: '%c' (total=%d)", ch, g_keys_received);
                    fbink_print(display_buf, -2);
                }
            }
        }
    }
    fflush(stdout);
}

void sig_handler(int sig) {
    printf("\n[SIG] Caught signal %d\n", sig);
    g_running = 0;
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    write(1, "[BOOT] Broadcom Native BLE Keyboard Engine Starting...\n", 55);
    printf("==========================================\n");
    printf("  E-Type Broadcom Native BLE Connector    \n");
    printf("  Macally BTMINIZKEY (D5:24:53:14:5A:C7)  \n");
    printf("==========================================\n");
    fflush(stdout);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Check if macally_mac.txt exists from Stage 3 discovery
    FILE *mf = fopen("/mnt/us/extensions/oasis_probe/macally_mac.txt", "r");
    if (mf) {
        char mac_line[64];
        if (fgets(mac_line, sizeof(mac_line), mf)) {
            if (parse_mac(mac_line, g_target_mac) == 0) {
                printf("[CONFIG] Loaded live target MAC from macally_mac.txt: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       g_target_mac[0], g_target_mac[1], g_target_mac[2],
                       g_target_mac[3], g_target_mac[4], g_target_mac[5]);
            }
        }
        fclose(mf);
    }

    // 1. Connect to BSA Server
    printf("[1/5] Connecting to BSA Server at /var/local/zbluetooth/...\n");
    fbink_print("Connecting to BSA Server...", -5);
    tBSA_MGT_OPEN mgt_open;
    BSA_MgtOpenInit(&mgt_open);
    strncpy(mgt_open.server_path, "/var/local/zbluetooth/", sizeof(mgt_open.server_path) - 1);
    mgt_open.cback = mgt_callback;

    int rc = -1;
    for (int retry = 0; retry < 10 && rc != 0 && g_running; retry++) {
        rc = BSA_MgtOpen(&mgt_open);
        printf("[1/5] BSA_MgtOpen attempt %d: rc=%d\n", retry + 1, rc);
        if (rc != 0) usleep(500000);
    }
    if (rc != 0) {
        printf("[ERROR] Failed to connect to BSA server.\n");
        return 1;
    }

    // 2. Enable BLE Subsystem in Broadcom Server
    printf("[2/5] Enabling BLE Subsystem in Broadcom Stack...\n");
    fbink_print("Enabling BLE Subsystem...", -4);
    tBSA_BLE_ENABLE ble_enable;
    BSA_BleEnableInit(&ble_enable);
    rc = BSA_BleEnable(&ble_enable);
    printf("[2/5] BSA_BleEnable returned: %d\n", rc);

    // 3. Initiate Security Bonding with Macally Keyboard
    printf("[3/5] Initiating Security Bonding with %02X:%02X:%02X:%02X:%02X:%02X...\n",
           g_target_mac[0], g_target_mac[1], g_target_mac[2],
           g_target_mac[3], g_target_mac[4], g_target_mac[5]);
    tBSA_SEC_BOND sec_bond;
    BSA_SecBondInit(&sec_bond);
    memcpy(sec_bond.bd_addr, g_target_mac, 6);
    rc = BSA_SecBond(&sec_bond);
    printf("[3/5] BSA_SecBond returned: %d\n", rc);

    // 4. Register BLE Client Application
    printf("[4/5] Registering BLE Client Application...\n");
    tBSA_BLE_CL_APP_REG app_reg;
    BSA_BleClAppRegisterInit(&app_reg);
    app_reg.uuid.len = 16;
    for (int i = 0; i < 16; i++) {
        app_reg.uuid.uu.uuid128[i] = (UINT8)(0xA0 + i);
    }
    app_reg.p_cback = ble_cl_callback;
    rc = BSA_BleClAppRegister(&app_reg);
    g_client_if = app_reg.client_if;
    printf("[4/5] BSA_BleClAppRegister returned: %d, client_if=%d\n", rc, g_client_if);

    // 5. Connect directly to Macally keyboard via BLE GATT
    printf("[5/5] Connecting to Macally keyboard (BLE GATT direct)...\n");
    fbink_print("Connecting BLE GATT...", -4);
    tBSA_BLE_CL_CONNECT ble_conn;
    BSA_BleClConnectInit(&ble_conn);
    ble_conn.client_if = g_client_if;
    memcpy(ble_conn.bd_addr, g_target_mac, 6);
    ble_conn.direct = 1;

    printf("[5/5] ble_conn raw bytes (len=%zu): ", sizeof(ble_conn));
    for (size_t i = 0; i < sizeof(ble_conn); i++) {
        printf("%02x ", ((UINT8*)&ble_conn)[i]);
    }
    printf("\n");

    rc = BSA_BleClConnect(&ble_conn);
    printf("[5/5] BSA_BleClConnect returned: %d\n", rc);

    // 6. Active Connection & Keystroke Listening Loop (45 seconds)
    printf("\n>>> ACTIVE LISTENING LOOP (45 seconds) <<<\n");
    fbink_print("Pairing... (LED blinking -> solid)", -3);
    int listen_cycles = 450; // 45 seconds
    while (g_running && listen_cycles > 0) {
        usleep(100000);
        listen_cycles--;

        if (listen_cycles % 30 == 0) {
            printf("[STATUS] %ds remaining | connected=%d | keys=%d\n",
                   listen_cycles / 10, g_connected, g_keys_received);
            fflush(stdout);
        }

        // Auto-reconnect if not connected yet and peripheral is entering advertising
        if (!g_connected && (listen_cycles == 350 || listen_cycles == 250 || listen_cycles == 150)) {
            printf("[RETRY] Re-issuing BSA_BleClConnect (remaining=%ds)...\n", listen_cycles / 10);
            BSA_BleClConnect(&ble_conn);
        }

        // If keys are flowing, keep extending listening time!
        if (g_keys_received > 0 && listen_cycles < 100) {
            listen_cycles = 150; // extend by 15s while typing
        }
    }

    printf("\n[SUMMARY] connected=%d, keys_received=%d\n", g_connected, g_keys_received);
    if (g_connected) {
        fbink_print("MACALLY CONNECTED!", -3);
    } else {
        fbink_print("Not connected. See log.", -3);
    }

    printf("[CLEANUP] Closing MGT connection...\n");
    tBSA_MGT_CLOSE mgt_close;
    BSA_MgtCloseInit(&mgt_close);
    BSA_MgtClose(&mgt_close);

    return 0;
}
