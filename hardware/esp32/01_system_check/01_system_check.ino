/*
 * E-Type Hardware: Step 1 - System & Memory Check
 * Hardware: ESP32-S3-WROOM-1 (N16R8: 16MB Flash, 8MB Octal PSRAM)
 * 
 * Arduino IDE Settings:
 *   - Board: ESP32S3 Dev Module
 *   - USB CDC On Boot: Enabled
 *   - Flash Size: 16MB (128Mb)
 *   - PSRAM: OPI PSRAM
 *   - Upload Speed: 921600
 */

#include <Arduino.h>
#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  delay(2000); // Wait for native USB CDC serial to attach

  Serial.println("\n==================================================");
  Serial.println("     ⌨️  E-TYPE HARDWARE: ESP32-S3 SYSTEM CHECK    ");
  Serial.println("==================================================");

  // 1. Silicon Info
  Serial.printf("Chip Model:        %s (Revision %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("CPU Cores:         %d @ %d MHz\n", ESP.getChipCores(), ESP.getCpuFreqMHz());

  // 2. Flash Memory Verification
  uint32_t flashSize = ESP.getFlashChipSize() / (1024 * 1024);
  Serial.printf("Flash Size:        %u MB %s\n", flashSize, (flashSize >= 16) ? "✅ (N16 Detected)" : "⚠️ (Check Flash Size setting)");

  // 3. PSRAM Verification
  if (psramInit()) {
    uint32_t psramSize = ESP.getPsramSize() / (1024 * 1024);
    uint32_t freePsram = ESP.getFreePsram() / (1024 * 1024);
    Serial.printf("PSRAM Status:      ACTIVE ✅ (%u MB total, %u MB free)\n", psramSize, freePsram);
  } else {
    Serial.println("PSRAM Status:      DISABLED ❌ (Set Tools -> PSRAM: OPI PSRAM)");
  }

  // 4. Internal SRAM
  Serial.printf("Internal SRAM:     %u KB free\n", ESP.getFreeHeap() / 1024);

  // 5. LittleFS Storage Test (For draft auto-save)
  Serial.println("--------------------------------------------------");
  Serial.print("Testing LittleFS Flash Storage: ");
  if (!LittleFS.begin(true)) {
    Serial.println("FAILED ❌ (Could not mount LittleFS)");
  } else {
    Serial.printf("MOUNTED ✅ (%u KB total, %u KB used)\n", 
      (unsigned int)(LittleFS.totalBytes() / 1024), 
      (unsigned int)(LittleFS.usedBytes() / 1024));

    // Test write and read
    File testFile = LittleFS.open("/etype_test.txt", "w");
    if (testFile) {
      testFile.print("E-Type persistence test successful.");
      testFile.close();
      Serial.println("  -> Draft write test: PASSED ✅");
    }

    File readFile = LittleFS.open("/etype_test.txt", "r");
    if (readFile) {
      String content = readFile.readString();
      readFile.close();
      Serial.printf("  -> Draft read test:  \"%s\" ✅\n", content.c_str());
      LittleFS.remove("/etype_test.txt");
    }
  }

  Serial.println("==================================================");
  Serial.println("  Hardware check complete. ESP32-S3 ready!        ");
  Serial.println("==================================================\n");
}

void loop() {
  delay(1000);
}
