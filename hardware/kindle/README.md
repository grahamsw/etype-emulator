# 📖 E-Type Hardware: Kindle Oasis Standalone Typewriter

This directory contains the firmware, userspace C daemons, and KUAL extensions for turning a jailbroken **Amazon Kindle Oasis 9th Gen (`KOA2`, firmware `5.16.2.1.1`, codename `juno`)** into a dedicated, distraction-free **E-Type E-Ink Typewriter**.

Zero hardware modifications. No dongles. No ESP32 bridge. Pure standalone Oasis hardware pairing directly with a Bluetooth keyboard and rendering keystrokes to `/dev/fb0`.

---

## 🏗️ Architecture & Broadcom Bluetooth Subsystem

### 1. Hardware Stack
* **Device**: Kindle Oasis 9th Generation (2017, Model `CW96BW`, Board `juno`)
* **SoC**: NXP/Freescale i.MX7 Dual (2× ARM Cortex-A7 @ 1.0 GHz, Neon/VFPv4)
* **Display**: 7.0" E-Ink Carta HD (1264 × 1680 pixels, 300 PPI, 16-level grayscale)
* **Wireless Chip**: Broadcom BCM4343W (Wi-Fi 802.11bgn + Bluetooth 4.2 / BLE) connected via UART `/dev/ttymxc2`
* **OS / Kernel**: Kindle OS 5.16.2.1.1 (Linux Kernel 4.1.15-lab126, GLIBC 2.4 / 2.17)

### 2. The Broadcom BSA Protocol Architecture
Unlike standard Linux distributions that use BlueZ (`bluetoothd` + `/dev/uhid`), Amazon Kindles run a proprietary **Broadcom BSA (Bluetooth System Architecture)** stack:

```
  +-------------------------------------------------------------+
  |              E-Type Typewriter Engine (C Binary)            |
  |   (Auto-Discovery, GATT Client, HID Report Parser, E-Ink)   |
  +------------------------------+------------------------------+
                                 |
                     UIPC IPC Socket Communication
                                 |
                                 v
  +-------------------------------------------------------------+
  |               Broadcom bsa_server Daemon                    |
  |              (/usr/bin/bsa_server on Oasis)                 |
  +------------------------------+------------------------------+
                                 |
                         HCI H4 Serial Protocol
                                 |
                                 v
  +-------------------------------------------------------------+
  |             Broadcom BCM4343W Bluetooth Baseband            |
  |                     (/dev/ttymxc2 UART)                     |
  +-------------------------------------------------------------+
```

### 3. Classic Bluetooth vs. BLE (HOGP) Discovery Findings
Disassembly of Amazon's `/usr/bin/bsa_server` binary revealed why older keyboards fail:
* **`bta_hh` (Classic Bluetooth HID Host)**: **Omitted by Amazon**. Amazon stripped Classic HID from `bsa_server` because Kindles only needed Bluetooth for VoiceView A2DP screen-reader audio. Devices broadcasting Classic Class of Device (`CoD = 0x402500`, e.g. Macally BTMINIZKEY or BT 3.0 keyboards) cannot form L2CAP HID channels.
* **`bta_gattc` (BLE GATT Client)**: **Fully compiled and functional**. Modern **Bluetooth Low Energy (BLE / Bluetooth 4.x / 5.x / HOGP)** keyboards transmit keystrokes via standard GATT Service `0x1812` and Report Characteristic `0x2A4D`. `etype_unified` connects to BLE keyboards with `status = 0`, locks the pairing LED solid, and receives live keystroke notifications.

---

## 📁 Directory Structure

```
hardware/kindle/
├── oasis_probe/                  <-- Staged KUAL extension (mirrors /mnt/us/extensions/oasis_probe)
│   ├── menu.json                 <-- KUAL touch menu configuration
│   ├── run_zen.sh                <-- Fullscreen Zen Mode launcher (pauses lab126_gui)
│   ├── run_discovery.sh          <-- Standard discovery & typing launcher
│   ├── kill_ads.sh               <-- Permanently locks /var/local/adunits
│   ├── restore_ads.sh            <-- Restores ad directory
│   ├── show_art.sh               <-- Displays 1264x1680 vintage typewriter screensaver
│   ├── screensaver_typewriter.png<-- 1264x1680 e-ink dithered illustration
│   ├── screensaver_typewriter.svg<-- Master vector artwork
│   ├── fakereg.sh                <-- Silences "Device Not Registered" popup
│   ├── unfakereg.sh              <-- Restores unregistered state
│   ├── bt_connect.sh             <-- Bluetooth connect helper
│   ├── bt_scan.sh                <-- Bluetooth scan helper
│   ├── probe.sh                  <-- Deep hardware inspection probe
│   ├── etype_unified             <-- Compiled live typing engine binary
│   └── etype_unified.c           <-- C source for live typing engine
├── bt_libs/                      <-- Extracted Oasis Bluetooth binaries and shared libraries
│   ├── bsa_server                <-- Amazon Broadcom server daemon
│   ├── btd                       <-- Amazon BlueLotus Bluetooth manager
│   ├── libbsa.so.1.0             <-- Broadcom client library
│   └── libbthal.so.1.0           <-- Amazon Bluetooth HAL library
├── reports/                      <-- Comprehensive hardware and Bluetooth inspection reports
│   ├── oasis_report.txt          <-- Full CPU, memory, kernel modules, devices probe
│   └── bt_report.txt             <-- Bluetooth daemons, upstart configs, LIPC probe
├── README.md                     <-- This document
└── CONTINUATION_KINDLE.md        <-- Pick-up prompt for future development
```

---

## 🚀 KUAL Menu Items & Capabilities

| Menu Item | Action | Effect |
| :--- | :--- | :--- |
| **`Launch E-Type Zen Mode (Fullscreen)`** | `run_zen.sh` | **The pure typewriter**: Pauses Amazon's `lab126_gui`, blanks the screen, dedicates 100% CPU/RAM to typing. Press **`Esc`** or **`Ctrl + Q`** to exit and instantly restore Kindle OS. |
| **`Start E-Type Typewriter (Standard)`** | `run_discovery.sh` | Runs the typewriter engine alongside the standard Kindle OS interface for testing. |
| **`Nuke Lockscreen Ads & Recommendations`** | `kill_ads.sh` | Purges `/var/local/adunits` and sets file permissions to `chmod 000` (`chattr +i`). Permanently removes lockscreen advertisements. |
| **`Display Vintage Typewriter Sleep Screen`** | `show_art.sh` | Flashes the screen and draws the custom 1264 × 1680 vintage typewriter manifesto art with hardware dithering. |
| **`Silence 'Cloud Not Available' Popup`** | `fakereg.sh` | Activates FakeReg to silence registration popups on offline/unregistered Kindles. |

---

## 🛠️ Cross-Compilation Instructions

Because Kindle Oasis 9th Gen runs a Cortex-A7 processor with GLIBC 2.4/2.17, binaries are cross-compiled using the **Zig C compiler** (`zig cc`):

```bash
/tmp/zig/zig cc -target arm-linux-gnueabi.2.17 -mcpu=cortex_a7 \
    -rdynamic -Wl,-export-dynamic -Wl,-z,lazy \
    -Wl,-z,max-page-size=4096 -Wl,-z,common-page-size=4096 \
    hardware/kindle/oasis_probe/etype_unified.c \
    -Lhardware/kindle/bt_libs -lbsa \
    -o hardware/kindle/oasis_probe/etype_unified
```

### Deploying to Physical Kindle:
With Kindle connected via USB:
```bash
cp -r hardware/kindle/oasis_probe/* /Volumes/Kindle/extensions/oasis_probe/
sync
```

---

## ⌨️ Keyboard Compatibility Matrix

| Keyboard Model | Protocol | Status | Why |
| :--- | :--- | :--- | :--- |
| **Samsers 5.1 Foldable / Mini** | **BLE 5.1 (HOGP)** | 🚀 **Ready to Pair** | Uses BLE Service `0x1812` & Appearance `0x03C1`. Direct `status = 0` connection. |
| **Apple Magic Keyboard** | **BLE 4.2 / 5.0** | 🚀 **Supported** | Standard BLE HOGP peripheral. Connects cleanly. |
| **Logitech K380 / MX Keys / Pebble** | **BLE 4.0+ (HOGP)** | 🚀 **Supported** | Standard BLE HID profile over GATT. |
| **Macally BTMINIZKEY** | **Bluetooth 3.0 Classic** | ❌ **Not Supported** | Uses Classic L2CAP HID (`CoD = 0x402500`). Amazon stripped `bta_hh` from `bsa_server`. |
| **Generic 'Bluetooth Keyboard' (13:07:...)** | **Bluetooth 2.1 Classic** | ❌ **Not Supported** | Classic BR/EDR only. Throws `status = 133` on BLE connection attempt. |
