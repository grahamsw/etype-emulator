# ⌨️ E-Type Hardware: ESP32-S3 Firmware & Test Suite

This directory contains the firmware and hardware verification suite for the standalone **E-Type E-Ink Typewriter**, powered by an **ESP32-S3 N16R8** (16MB Flash, 8MB Octal PSRAM), a **Waveshare 4.2" E-Paper Display**, and an **Adafruit PowerBoost 1000C**.

---

## 📁 Sketches in this Directory

| Sketch | Purpose | Status |
| :--- | :--- | :--- |
| [`01_system_check/`](./01_system_check/01_system_check.ino) | Verifies chip rev, dual 240MHz cores, 16MB Flash, 8MB Octal PSRAM, and LittleFS file persistence. | ✅ Tested & Verified |
| [`02_display_test/`](./02_display_test/02_display_test.ino) | Waveshare 4.2" SPI test: full screen clear, typewriter title card, and fast partial refresh line typing. | ⏳ Ready for cables |
| [`03_keyboard_test/`](./03_keyboard_test/03_keyboard_test.ino) | Native USB Host keyboard test: detects plug/unplug and echoes keystrokes live to Serial Monitor. | ⏳ Ready for keyboard |

---

## ⚙️ Arduino IDE Setup

### 1. Board Settings (`Tools` Menu)
Set these options under the **Tools** menu when compiling for the ESP32-S3 N16R8:

* **Board**: `ESP32S3 Dev Module`
* **Port**: `/dev/cu.usbmodem101` (or your Mac's USB port)
* **USB CDC On Boot**: **`Enabled`** *(Routes `Serial.print()` to the USB-C cable)*
* **Flash Size**: **`16MB (128Mb)`**
* **PSRAM**: **`OPI PSRAM`** *(Enables 8MB Octal PSRAM)*
* **Erase All Flash Before Sketch Upload**: **`Disabled`** *(Keep disabled for 3-second uploads)*

---

### 2. Required Libraries (Install via Arduino Library Manager)
Press **`Cmd + Shift + I`** in Arduino IDE and install:

1. **`GxEPD2`** by *Jean-Marc Zingg* (select "Install All" for Adafruit GFX dependencies).
2. **`EspUsbHost`** by *tanakamasayuki* (USB Host keyboard driver).

---

## 📌 Wiring Reference

> [!WARNING]
> **Octal PSRAM Hazard**: Do **NOT** connect any wires to **GPIO 33 through GPIO 37** on the N16R8 module.

### Waveshare 4.2" E-Paper to Terminal Base
| Waveshare Pin | Wire Color (Std) | Breakout Pin | Role |
| :--- | :--- | :--- | :--- |
| **VCC** | Red | **`3.3V`** | **Must be 3.3V (Never connect to 5V!)** |
| **GND** | Black | **`GND`** | Ground |
| **DIN** | Blue | **`GPIO 11`** | SPI MOSI (Data) |
| **CLK** | Yellow | **`GPIO 12`** | SPI SCK (Clock) |
| **CS** | Orange | **`GPIO 10`** | Chip Select |
| **DC** | Green | **`GPIO 9`** | Data / Command |
| **RST** | White | **`GPIO 8`** | Screen Reset |
| **BUSY** | Purple | **`GPIO 7`** | Busy Status Line |

### PowerBoost 1000C to Terminal Base
| PowerBoost Pin | Breakout Pin | Role |
| :--- | :--- | :--- |
| **+5V** | **`5V` (or `VIN`)** | Clean 5.2V rail for ESP32 and USB keyboard |
| **GND** | **`GND`** | System ground |
| **LBO** | **`GPIO 6`** | Low battery signal (< 3.2V) triggers safe draft save |
| **EN** | Toggle Switch to **`GND`** | Hardware on/off switch |
| **JST Port** | 3.7V Battery | 2500–3400mAh LiPo or 18650 holder |
