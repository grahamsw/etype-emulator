# 🚀 E-Type Continuation Plan: Kindle Oasis Standalone Typewriter
**GitHub Issue**: [Issue #63: Kindle Oasis Typewriter: Samsers BLE 5.1 keyboard verification & standalone drafting UI](https://github.com/grahamsw/etype-emulator/issues/63)

> **How to resume in a new chat:**
> Open a new chat session, point the agent here, and paste:
> *"We are continuing work on the Kindle Oasis standalone typewriter. The Samsers BLE 5.1 keyboard is here and ready to test. Please read `hardware/kindle/CONTINUATION_KINDLE.md` and `hardware/kindle/README.md`. Let's pick up where we left off."*

---

## 📌 Executive Status

| Component | Status | Notes |
| :--- | :--- | :--- |
| **Target Device** | ✅ **Verified** | Kindle Oasis 9th Gen (`CW96BW`, `juno`, firmware `5.16.2.1.1`, FakeReg active) |
| **Screen Rendering** | ✅ **Verified** | Dual `/usr/sbin/eips` and `fbink` rendering live to `/dev/fb0` |
| **Broadcom Radio** | ✅ **Verified** | Native BCM4343W over `/dev/ttymxc2` via Broadcom `bsa_server`; heard 31 ambient devices over the air |
| **Discovery Engine** | ✅ **Verified** | Unified scanner catches keyboards live over the air, decodes EIR names, and triggers live connection |
| **BLE GATT Client** | ✅ **Compiled & Staged** | `etype_unified` connects via `BSA_BleClConnect`, subscribes to HID Report characteristic `0x2A4D`, decodes scan codes to ASCII |
| **Zen Mode Kiosk** | ✅ **Staged in KUAL** | [`run_zen.sh`](file:///Users/graha/Documents/dev/etype-emulator/hardware/kindle/oasis_probe/run_zen.sh) pauses `lab126_gui`, blanks screen, runs edge-to-edge; `Esc` or `Ctrl+Q` cleanly restores Kindle OS |
| **Ad / Promo Nuker** | ✅ **Staged in KUAL** | [`kill_ads.sh`](file:///Users/graha/Documents/dev/etype-emulator/hardware/kindle/oasis_probe/kill_ads.sh) locks `/var/local/adunits` to `chmod 000` (`chattr +i`) |
| **Vintage Sleep Screen**| ✅ **Staged in KUAL** | [`show_art.sh`](file:///Users/graha/Documents/dev/etype-emulator/hardware/kindle/oasis_probe/show_art.sh) renders 1264×1680 vintage typewriter illustration with hardware dithering |
| **Active Keyboard Test**| ⏳ **Awaiting Keyboard**| Samsers Bluetooth 5.1 BLE keyboard arriving tomorrow to replace the older Bluetooth 3.0 Classic keyboards |

---

## 🎯 Immediate Test Sequence for Tomorrow (When Samsers Arrives)

### Step 1: Put Samsers Keyboard into BLE Pairing Mode
1. Power switch **ON**.
2. Select Bluetooth channel (e.g., **BT1** / **Fn + Q**).
3. Press and hold **Fn + C** (or Connect button) until the blue pairing LED **blinks rapidly**.

### Step 2: Launch from Kindle KUAL Menu
1. On Kindle Oasis, open **KUAL** $\rightarrow$ **E-Type Live Typewriter**.
2. Tap **`Start E-Type Typewriter (Standard)`** (or **`Launch E-Type Zen Mode (Fullscreen)`**).

### Step 3: Observe Verification Milestones on Oasis Screen
* **Milestone 1**: Scanner detects `Samsers` / `BT 5.1 Keyboard` / HID UUID `0x1812`.
* **Milestone 2**: Screen prints: `FOUND: Samsers Keyboard -> Linking to Broadcom radio live...`
* **Milestone 3**: `BSA_BleClConnect` returns `status = 0`.
* **Milestone 4**: The keyboard's blinking blue LED turns **solid**!
* **Milestone 5**: Screen prints:
  ```text
  KEYBOARD CONNECTED (SOLID LED)!
  Configuring GATT HID link...
  Typewriter Ready! Start typing now...
  > _
  ```

### Step 4: Test Real-Time Keystrokes
* Press keys on the keyboard. Each keystroke should appear with e-ink delay, line wrapping, and append live to `/mnt/us/documents/etype_draft.txt`.
* Press **`Esc`** or **`Ctrl + Q`** to test clean exit.

---

## 🎯 Subsequent Workstreams (Porting from ESP32 Version)

1. **Typographic & Layout Engine**:
   * Implement full-page paragraph wrapping with margin indentation and smooth carriage return scrolling.
   * Add word count and typing sprint timer to status bar (mirroring ESP32 v1.6).
2. **Multi-Draft Management**:
   * Organize drafts with timestamped filenames (`/mnt/us/documents/drafts/draft_YYYY-MM-DD_NN.md`).
3. **Cloud Export via Native Kindle Wi-Fi**:
   * Kindle Oasis has a Broadcom Wi-Fi driver (`bcmdhd`). When connected to home Wi-Fi, the shell script can use native `/usr/bin/curl` to push completed drafts straight to Google Drive via Apps Script!

---

## 📂 Key Files & References

* **Staged KUAL Extension**: [`hardware/kindle/oasis_probe/`](file:///Users/graha/Documents/dev/etype-emulator/hardware/kindle/oasis_probe/)
* **Live Engine C Source**: [`hardware/kindle/oasis_probe/etype_unified.c`](file:///Users/graha/Documents/dev/etype-emulator/hardware/kindle/oasis_probe/etype_unified.c)
* **Zen Launcher**: [`hardware/kindle/oasis_probe/run_zen.sh`](file:///Users/graha/Documents/dev/etype-emulator/hardware/kindle/oasis_probe/run_zen.sh)
* **KUAL Menu Config**: [`hardware/kindle/oasis_probe/menu.json`](file:///Users/graha/Documents/dev/etype-emulator/hardware/kindle/oasis_probe/menu.json)
* **Technical Architecture**: [`hardware/kindle/README.md`](file:///Users/graha/Documents/dev/etype-emulator/hardware/kindle/README.md)
