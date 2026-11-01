/*
 * E-Type Hardware: Step 2 - Waveshare 4.2" E-Paper Display Test
 * Hardware: ESP32-S3-WROOM-1 (N16R8) + Waveshare 4.2" B&W E-Paper (400x300)
 * 
 * Required Arduino Libraries (Install via Library Manager):
 *   1. "GxEPD2" by Jean-Marc Zingg
 *   2. "Adafruit GFX Library" by Adafruit
 * 
 * Safe Pin Mapping (N16R8 Octal PSRAM Safe):
 *   - Waveshare BUSY -> GPIO 7
 *   - Waveshare RST  -> GPIO 8
 *   - Waveshare DC   -> GPIO 9
 *   - Waveshare CS   -> GPIO 10
 *   - Waveshare DIN  -> GPIO 11 (SPI MOSI)
 *   - Waveshare CLK  -> GPIO 12 (SPI SCK)
 *   - Waveshare GND  -> GND
 *   - Waveshare VCC  -> 3.3V (⚠️ Never connect to 5V!)
 */

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

// -------------------------------------------------------------------------
// Screen Model Selection: Waveshare 4.2" V2 (GDEY042T81 / SSD1683)
// -------------------------------------------------------------------------
// Active: Waveshare 4.2" V2 B&W (GDEY042T81 / SSD1683)
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(/*CS=*/ 10, /*DC=*/ 9, /*RST=*/ 8, /*BUSY=*/ 7)
);

// Fallback: Legacy Waveshare 4.2" V1 (UC8176 / GDEW042T2)
// GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT> display(
//     GxEPD2_420(/*CS=*/ 10, /*DC=*/ 9, /*RST=*/ 8, /*BUSY=*/ 7)
// );

const int PIN_SPI_SCK  = 12;
const int PIN_SPI_MOSI = 11;
const int PIN_SPI_MISO = -1;  // E-paper is write-only SPI
const int PIN_SPI_CS   = 10;

void drawHeader() {
  display.drawRect(5, 5, display.width() - 10, display.height() - 10, GxEPD_BLACK);
  display.drawRect(7, 7, display.width() - 14, display.height() - 14, GxEPD_BLACK);

  display.setFont(&FreeMonoBold12pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(20, 40);
  display.print("E-TYPE TYPEWRITER");

  display.drawLine(20, 50, display.width() - 20, 50, GxEPD_BLACK);

  display.setFont(&FreeMono9pt7b);
  display.setCursor(20, 75);
  display.print("Hardware: ESP32-S3 + 4.2\" E-Paper");
  display.setCursor(20, 100);
  display.print("Resolution: 400 x 300 pixels");
  display.setCursor(20, 125);
  display.print("Status: Display Initialized OK!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Waveshare 4.2\" E-Paper Test Starting ---");

  // Initialize custom SPI bus on safe pins
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);

  // Initialize display (115200 baud for debug, initial reset, 2ms pulse, SPI pullup)
  display.init(115200, true, 2, false);
  display.setRotation(0); // 0 or 2 for landscape, 1 or 3 for portrait

  Serial.println("Performing Full Screen Refresh (clearing screen to white)...");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawHeader();
  } while (display.nextPage());

  Serial.println("Full refresh complete! Testing partial refresh...");
  delay(2000);

  // Partial Refresh Demo: Simulate real-time typing of lines
  const char* demoLines[] = {
    "Write forward. Never look back.",
    "No backspace. No distractions.",
    "First drafts are for discovery.",
    "Ready for USB keyboard..."
  };

  int startY = 165;
  for (int i = 0; i < 4; i++) {
    Serial.printf("Typing demo line %d via fast partial refresh...\n", i + 1);
    
    // Fast partial window update (only refreshes the updated line)
    display.setPartialWindow(20, startY + (i * 28) - 16, display.width() - 40, 24);
    display.firstPage();
    do {
      display.setFont(&FreeMono9pt7b);
      display.setTextColor(GxEPD_BLACK);
      display.setCursor(20, startY + (i * 28));
      display.print("> ");
      display.print(demoLines[i]);
    } while (display.nextPage());

    delay(400); // Small pause between lines
  }

  Serial.println("\nDisplay test finished successfully! Screen will hold this image with ZERO power.");
  display.powerOff();
}

void loop() {
  // Static e-paper holds image indefinitely without power
  delay(10000);
}
