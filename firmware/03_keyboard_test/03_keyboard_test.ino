/*
 * E-Type Hardware: Step 3 - Native USB Host Keyboard Test
 * Hardware: ESP32-S3-WROOM-1 (N16R8) + Wired USB Keyboard
 * 
 * Required Arduino Library (Install via Library Manager):
 *   "EspUsbHost" by tanakamasayuki
 * 
 * Hardware Connections:
 *   - The wired USB keyboard plugs directly into the ESP32-S3 USB-C port
 *     (connected internally to GPIO 19 = D- and GPIO 20 = D+).
 *   - The ESP32 terminal base must be powered via its 5V/VIN & GND terminals
 *     (from your PowerBoost 1000C or 5V power supply) so the keyboard receives 5V VBUS.
 * 
 * What This Sketch Does:
 *   - Initializes the ESP32-S3 USB Host stack.
 *   - Detects when the keyboard is plugged in or unplugged.
 *   - Echoes keystrokes live to the Serial Monitor (115200 baud).
 *   - Displays key codes, ASCII characters, and modifier keys (Shift, Ctrl, Alt).
 */

#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n==================================================");
  Serial.println("  ⌨️  E-TYPE HARDWARE: USB HOST KEYBOARD TEST      ");
  Serial.println("==================================================");
  Serial.println("Plug your wired USB keyboard into the ESP32-S3 port.");
  Serial.println("Ensure 5V is supplied to the terminal base.\n");

  // Standard US QWERTY keyboard layout
  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_US);

  // Callback when a USB device is attached
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
    Serial.println("\n[USB] >>> Keyboard Connected! <<<");
    Serial.printf("      Vendor ID:  0x%04X\n", device.vendor_id);
    Serial.printf("      Product ID: 0x%04X\n", device.product_id);
    Serial.println("Start typing! Keystrokes will echo below:\n");
  });

  // Callback when device is detached
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
    Serial.println("\n[USB] <<< Keyboard Disconnected! >>>\n");
  });

  // Keystroke callback
  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    // Only process on key press (ignore key release events)
    if (!event.pressed) return;

    // Handle Enter / Return
    if (event.ascii == '\r' || event.ascii == '\n') {
      Serial.println(" [ENTER]");
      return;
    }

    // Handle Backspace (Warning: forward-only typewriter!)
    if (event.ascii == 0x08 || event.keycode == 0x2A) {
      Serial.print(" [BACKSPACE - Blocked in Hemingway Mode]");
      return;
    }

    // Handle Tab
    if (event.ascii == '\t') {
      Serial.print("    "); // 4-space tab
      return;
    }

    // Handle printable characters (letters, numbers, symbols, space)
    if (event.ascii >= 0x20 && event.ascii <= 0x7E) {
      Serial.print((char)event.ascii);
    } else {
      // Special function keys or navigation keys
      Serial.printf(" [Keycode: 0x%02X, Mod: 0x%02X]", event.keycode, event.modifiers);
    }
  });

  // Start the USB Host controller task
  if (!usb.begin()) {
    Serial.printf("[ERROR] USB Host initialization failed: %s\n", usb.lastErrorName());
  } else {
    Serial.println("[USB] USB Host initialized successfully. Waiting for keyboard...");
  }
}

void loop() {
  // FreeRTOS background task handles USB packets automatically
  delay(10);
}
