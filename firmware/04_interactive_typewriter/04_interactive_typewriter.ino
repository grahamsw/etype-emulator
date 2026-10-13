/*
 * E-Type Hardware: Step 4 - Interactive USB Keyboard to E-Paper Typewriter
 * Hardware: ESP32-S3-WROOM-1 (N16R8) + Waveshare 4.2" E-Paper (400x300) + USB Keyboard
 * 
 * Required Arduino Libraries (Install via Library Manager):
 *   1. "GxEPD2" by Jean-Marc Zingg (Install All dependencies)
 *   2. "EspUsbHost" by tanakamasayuki
 *   3. "Adafruit GFX Library" by Adafruit
 * 
 * Hardware Connections (Screw Terminal Base):
 *   - Waveshare BUSY -> GPIO 7
 *   - Waveshare RST  -> GPIO 8
 *   - Waveshare DC   -> GPIO 9
 *   - Waveshare CS   -> GPIO 10
 *   - Waveshare DIN  -> GPIO 11 (SPI MOSI)
 *   - Waveshare CLK  -> GPIO 12 (SPI SCK)
 *   - Waveshare GND  -> GND
 *   - Waveshare VCC  -> 3.3V (⚠️ NEVER connect to 5V!)
 *   - USB Keyboard D- -> GPIO 19
 *   - USB Keyboard D+ -> GPIO 20
 *   - USB Keyboard 5V -> 5V (from MH-CD42 / PowerBoost / USB)
 *   - USB Keyboard GND -> GND
 * 
 * What This Sketch Does:
 *   - Real-time live typing: Characters typed on the USB keyboard appear on the 4.2" E-Paper!
 *   - Thread-Safe Architecture: Uses a FreeRTOS keystroke queue so slow E-Paper refreshes
 *     never block the USB Host driver or drop typed keys.
 *   - Fast Partial Refresh: Only redraws the active line being typed (~300ms update).
 *   - F5 Key: Triggers a full-screen refresh to clear accumulated ghosting on demand.
 */

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <EspUsbHost.h>

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
// Display Driver Selection: Waveshare 4.2" V2 (GDEY042T81 / SSD1683)
// -------------------------------------------------------------------------
// Active: Waveshare 4.2" V2 B&W (SSD1683 / GDEY042T81)
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(/*CS=*/ 10, /*DC=*/ 9, /*RST=*/ 8, /*BUSY=*/ 7)
);

// Fallback: Legacy Waveshare 4.2" V1 (UC8176)
// GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT> display(
//     GxEPD2_420(/*CS=*/ 10, /*DC=*/ 9, /*RST=*/ 8, /*BUSY=*/ 7)
// );

const int PIN_SPI_SCK  = 12;
const int PIN_SPI_MOSI = 11;
const int PIN_SPI_MISO = -1;  // E-paper is write-only SPI
const int PIN_SPI_CS   = 10;

// -------------------------------------------------------------------------
// USB Host & Thread-Safe Keystroke Queue
// -------------------------------------------------------------------------
EspUsbHost usb;
QueueHandle_t keyQueue = NULL;

struct KeyMessage {
  uint8_t ascii;
  uint8_t keycode;
  uint8_t modifiers;
};

// -------------------------------------------------------------------------
// Typewriter State & Buffer
// -------------------------------------------------------------------------
#define MAX_COLS 32
#define MAX_ROWS 8
#define LINE_HEIGHT 24
#define START_Y 75
#define MARGIN_X 20

char pageBuffer[MAX_ROWS][MAX_COLS + 1];
int cursorRow = 0;
int cursorCol = 0;
bool keyboardConnected = false;

// -------------------------------------------------------------------------
// Graphics Functions
// -------------------------------------------------------------------------
void drawHeader(const char* statusMsg) {
  display.drawRect(5, 5, display.width() - 10, display.height() - 10, GxEPD_BLACK);
  display.drawRect(7, 7, display.width() - 14, display.height() - 14, GxEPD_BLACK);

  display.setFont(&FreeMonoBold12pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(MARGIN_X, 35);
  display.print("E-TYPE TYPEWRITER");

  display.drawLine(MARGIN_X, 45, display.width() - MARGIN_X, 45, GxEPD_BLACK);

  display.setFont(&FreeMono9pt7b);
  display.setCursor(MARGIN_X, 60);
  display.print(statusMsg);
}

void redrawEntirePage() {
  Serial.println("[Display] Redrawing full page...");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawHeader(keyboardConnected ? "USB Keyboard Ready | F5: Clear Ghosting" : "Waiting for USB Keyboard...");

    display.setFont(&FreeMono9pt7b);
    display.setTextColor(GxEPD_BLACK);

    for (int r = 0; r <= cursorRow; r++) {
      int y = START_Y + 25 + (r * LINE_HEIGHT);
      display.setCursor(MARGIN_X, y);
      display.print(pageBuffer[r]);
      if (r == cursorRow) {
        display.print("_"); // Draw active cursor
      }
    }
  } while (display.nextPage());
}

void renderCurrentLinePartial() {
  int y = START_Y + 25 + (cursorRow * LINE_HEIGHT);
  // Box coordinates for partial update: x, y, width, height
  int windowY = y - 16;
  int windowH = LINE_HEIGHT;

  display.setPartialWindow(MARGIN_X, windowY, display.width() - (MARGIN_X * 2), windowH);
  display.firstPage();
  do {
    display.fillRect(MARGIN_X, windowY, display.width() - (MARGIN_X * 2), windowH, GxEPD_WHITE);
    display.setFont(&FreeMono9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(MARGIN_X, y);
    display.print(pageBuffer[cursorRow]);
    display.print("_"); // Blinking cursor placeholder
  } while (display.nextPage());
}

// -------------------------------------------------------------------------
// Setup
// -------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n==================================================");
  Serial.println("  ⌨️  E-TYPE: STEP 4 - LIVE TYPEWRITER PROTOTYPE   ");
  Serial.println("==================================================");

  // Initialize line buffers
  for (int r = 0; r < MAX_ROWS; r++) {
    memset(pageBuffer[r], 0, sizeof(pageBuffer[r]));
  }

  // Create queue for up to 32 keystrokes
  keyQueue = xQueueCreate(32, sizeof(KeyMessage));

  // Initialize SPI bus for E-Paper
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);

  // Initialize E-Paper Display
  display.init(115200, true, 2, false);
  display.setRotation(0);

  // Draw initial blank page
  redrawEntirePage();

  // Initialize USB Host keyboard driver
  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_US);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
    Serial.println("[USB] Keyboard connected!");
    keyboardConnected = true;
    KeyMessage msg = { 0, 0xFF, 0 }; // Special reconnect flag
    xQueueSend(keyQueue, &msg, 0);
  });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
    Serial.println("[USB] Keyboard disconnected!");
    keyboardConnected = false;
    KeyMessage msg = { 0, 0xFE, 0 }; // Special disconnect flag
    xQueueSend(keyQueue, &msg, 0);
  });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (!event.pressed) return; // Only process key press down

    KeyMessage msg;
    msg.ascii = event.ascii;
    msg.keycode = event.keycode;
    msg.modifiers = event.modifiers;

    // Send key event to processing task/loop
    xQueueSend(keyQueue, &msg, 0);
  });

  if (!usb.begin()) {
    Serial.printf("[ERROR] USB Host init failed: %s\n", usb.lastErrorName());
  } else {
    Serial.println("[USB] Host stack running. Start typing!");
  }
}

// -------------------------------------------------------------------------
// Main Loop: Handles Keystrokes & Screen Updates
// -------------------------------------------------------------------------
void loop() {
  KeyMessage key;

  // Block until a key arrives from the queue (timeout 100ms)
  if (xQueueReceive(keyQueue, &key, pdMS_TO_TICKS(100)) == pdTRUE) {

    // Check for Connect / Disconnect events
    if (key.keycode == 0xFF || key.keycode == 0xFE) {
      redrawEntirePage();
      return;
    }

    // F5 Key (Keycode 0x3E): Force full refresh to clear ghosting
    if (key.keycode == 0x3E) {
      Serial.println("[Key] F5 pressed: Clearing screen ghosting...");
      redrawEntirePage();
      return;
    }

    // Handle Enter / Return key
    if (key.ascii == '\r' || key.ascii == '\n') {
      Serial.println("[Key] ENTER");
      // Remove trailing cursor on old line
      renderCurrentLinePartial();
      
      cursorRow++;
      cursorCol = 0;
      if (cursorRow >= MAX_ROWS) {
        // Page full: for this prototype, clear and start top of page
        Serial.println("[Typewriter] Page full! Resetting to top...");
        for (int r = 0; r < MAX_ROWS; r++) {
          memset(pageBuffer[r], 0, sizeof(pageBuffer[r]));
        }
        cursorRow = 0;
        cursorCol = 0;
        redrawEntirePage();
      } else {
        renderCurrentLinePartial();
      }
      return;
    }

    // Handle Backspace (0x08 or keycode 0x2A)
    if (key.ascii == 0x08 || key.keycode == 0x2A) {
      if (cursorCol > 0) {
        cursorCol--;
        pageBuffer[cursorRow][cursorCol] = '\0';
        Serial.println("[Key] BACKSPACE");
        renderCurrentLinePartial();
      }
      return;
    }

    // Handle Tab (convert to 4 spaces)
    if (key.ascii == '\t') {
      for (int i = 0; i < 4 && cursorCol < MAX_COLS; i++) {
        pageBuffer[cursorRow][cursorCol++] = ' ';
      }
      pageBuffer[cursorRow][cursorCol] = '\0';
      renderCurrentLinePartial();
      return;
    }

    // Handle Printable Characters (ASCII 32 to 126)
    if (key.ascii >= 0x20 && key.ascii <= 0x7E) {
      Serial.printf("[Key] '%c'\n", (char)key.ascii);

      // Auto-wrap if line exceeds column width
      if (cursorCol >= MAX_COLS) {
        cursorRow++;
        cursorCol = 0;
        if (cursorRow >= MAX_ROWS) {
          for (int r = 0; r < MAX_ROWS; r++) {
            memset(pageBuffer[r], 0, sizeof(pageBuffer[r]));
          }
          cursorRow = 0;
          cursorCol = 0;
          redrawEntirePage();
        }
      }

      pageBuffer[cursorRow][cursorCol++] = (char)key.ascii;
      pageBuffer[cursorRow][cursorCol] = '\0';

      // Push partial update to the E-Paper display
      renderCurrentLinePartial();
    }
  }
}
