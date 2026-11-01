# 🚀 E-Type Continuation Plan: ESP32-S3 Standalone Typewriter
**GitHub Issue**: [Issue #62: ESP32-S3 Typewriter: Enclosure design, battery fuel gauge & hardware power switch](https://github.com/grahamsw/etype-emulator/issues/62)

> **How to resume in a new chat:**
> Open a new chat session, point the agent here, and paste:
> *"We are continuing work on the ESP32-S3 hardware typewriter. Please read `hardware/esp32/CONTINUATION_ESP32.md` and `hardware/esp32/README.md`. Let's pick up where we left off."*

---

## 📌 Executive Status

| Component | Status | Notes |
| :--- | :--- | :--- |
| **Core Firmware** | ✅ **v1.6 Ready** | [`hardware/esp32/05_interactive_typewriter/05_interactive_typewriter.ino`](file:///Users/graha/Documents/dev/etype-emulator/hardware/esp32/05_interactive_typewriter/05_interactive_typewriter.ino) |
| **Display** | ✅ **Verified** | Waveshare 4.2" E-Paper (400×300) with fast partial refresh character/line typing |
| **Keyboard Input** | ✅ **Verified** | Native ESP32-S3 USB Host (GPIO 19/20) for wired USB keyboards |
| **Draft Persistence** | ✅ **Verified** | LittleFS local storage with auto-save and multi-draft browsing |
| **Cloud Sync** | ✅ **Verified** | Background Google Drive Apps Script sync & SoftAP Wi-Fi Web Portal |
| **Battery Indicator** | ⏳ **Code Ready** | Implemented in v1.6 via GPIO 1 ADC voltage divider (`getBatteryPercentage()`); needs physical resistors wired |
| **Hardware Power Switch** | ⏳ **Planned** | Needs physical toggle/rocker switch inline between TP4056 `OUT+` and MH-CD42 `BAT` |
| **Enclosure / Casing** | ⏳ **Active Discussion** | Tracked in **[GitHub Issue #56](https://github.com/graha/etype-emulator/issues/56)** |

---

## 🎯 Next Steps & Open Workstreams

### 1. Enclosure & Physical Packaging (GitHub Issue #56)
* **Battery Form Factor**:
  * Currently using an 18650 holder or flat Li-ion cell with separate TP4056 + MH-CD42 modules.
  * *Option A (Prototype Chassis)*: Keep the screw terminal breakout board and 18650 cell. Leads to a thicker enclosure (~32mm–38mm).
  * *Option B (Slim Portable Chassis)*: Move to a flat pouch LiPo (2000–3000mAh) and direct-soldered perfboard or custom carrier PCB. Reduces overall unit thickness to under 20mm.
* **Ergonomics & Desk Angle**:
  * 7° to 10° fixed incline for optimal screen contrast under desk lighting.
  * Bezel recess with soft foam gasket to protect the delicate Waveshare glass substrate.
* **Cutouts Required**:
  1. USB-C port cutout for TP4056 charging.
  2. USB-A female or USB-C keyboard socket cutout.
  3. Rocker / toggle switch cutout on side or rear.
  4. Recessed button access hole for BOOT / RESET.

### 2. Battery Fuel Gauge Hardware Wiring
* Wire two 100kΩ (1% metal film) resistors:
  ```
  Battery (+) ---> [100kΩ] ---> Screw Terminal GPIO 1 ---> [100kΩ] ---> GND
  ```
* Test battery percentage readout on screen (`[BAT 85%]`) across full charge and discharge cycles.

### 3. Physical Power Cutoff Switch
* Solder an SPST rocker or toggle switch inline on the positive battery line:
  * Place between **TP4056 `OUT+`** and **MH-CD42 `BAT`**.
  * Gives **0.0 µA standby drain** when switched OFF, while still allowing the battery to charge safely when USB-C is plugged into the TP4056.

### 4. Software & UI Refinements
* Cross-pollinate typographic enhancements with the Kindle version (margin styling, word count indicators, flow sprint timer alerts).

---

## 📂 Key Files & References

* **Main Firmware**: [`hardware/esp32/05_interactive_typewriter/05_interactive_typewriter.ino`](file:///Users/graha/Documents/dev/etype-emulator/hardware/esp32/05_interactive_typewriter/05_interactive_typewriter.ino)
* **Wiring & Pinouts**: [`hardware/esp32/README.md`](file:///Users/graha/Documents/dev/etype-emulator/hardware/esp32/README.md)
* **Circuit Schematic**: [`hardware/esp32/circuit_diagram.png`](file:///Users/graha/Documents/dev/etype-emulator/hardware/esp32/circuit_diagram.png) and [`circuit_diagram.svg`](file:///Users/graha/Documents/dev/etype-emulator/hardware/esp32/circuit_diagram.svg)
* **GitHub Issue**: [Issue #56: Enclosure design](https://github.com/graha/etype-emulator/issues/56)
