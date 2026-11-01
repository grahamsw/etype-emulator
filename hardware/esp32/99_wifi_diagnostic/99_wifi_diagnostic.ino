/*
 * E-Type Hardware Diagnostic: Standalone Wi-Fi & NVS Test
 * Hardware: ESP32-S3-WROOM-1 (N16R8)
 * 
 * Purpose:
 *   Isolated, zero-dependency diagnostic to verify:
 *   1. Hardware reset reason (was the previous reboot a Brownout, Panic crash, or normal?)
 *   2. Non-Volatile Storage (NVS) flash partition integrity (auto-repairs corruption)
 *   3. 2.4GHz RF Radio Receiver (performs Wi-Fi scan to detect nearby networks)
 *   4. 2.4GHz RF Radio Transmitter & SoftAP (broadcasts 'EType-Drafts' at 192.168.4.1)
 *   5. Web Server HTTP stack (serves diagnostic webpage)
 * 
 * Requirements:
 *   - NO KEYBOARD REQUIRED (all tests execute automatically on boot)
 *   - Run over native USB connection directly to your computer
 *   - Monitor output at 115200 baud
 * 
 * Arduino IDE Settings:
 *   - Board: ESP32S3 Dev Module
 *   - USB CDC On Boot: Enabled
 *   - Flash Size: 16MB (128Mb)
 *   - PSRAM: OPI PSRAM
 *   - Upload Speed: 921600
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <nvs_flash.h>
#include <esp_system.h>

WebServer server(80);

const char* getResetReasonStr(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "Power-on (Normal boot / power applied)";
    case ESP_RST_EXT:       return "External pin reset (RST button pressed)";
    case ESP_RST_SW:        return "Software reset (ESP.restart)";
    case ESP_RST_PANIC:     return "CRASH / PANIC (Software exception)";
    case ESP_RST_INT_WDT:   return "Interrupt Watchdog";
    case ESP_RST_TASK_WDT:  return "Task Watchdog";
    case ESP_RST_WDT:       return "Other Watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (Voltage dropped below 2.8V!)";
    case ESP_RST_SDIO:      return "SDIO reset";
    default:                return "Unknown";
  }
}

void setup() {
  Serial.begin(115200);
  
  // Wait up to 3 seconds for Native USB CDC Serial Monitor connection
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 3000)) {
    delay(10);
  }
  delay(500);

  Serial.println("\n\n==================================================");
  Serial.println("     📡 E-TYPE ISOLATED WI-FI DIAGNOSTIC TOOL     ");
  Serial.println("==================================================");

  // 1. Boot & Silicon Diagnostic
  Serial.println("\n--- [1] SILICON & RESET DIAGNOSTIC ---");
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("Reset Reason:      %s (Code: %d)\n", getResetReasonStr(resetReason), (int)resetReason);
  Serial.printf("Chip Model:        %s (Revision %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("CPU Cores:         %d @ %d MHz\n", ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("Flash Chip Size:   %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("Internal Heap:     %u KB free\n", ESP.getFreeHeap() / 1024);
  
  if (psramInit()) {
    Serial.printf("Octal PSRAM:       %u MB total, %u MB free ✅\n", 
                  ESP.getPsramSize() / (1024 * 1024), 
                  ESP.getFreePsram() / (1024 * 1024));
  } else {
    Serial.println("Octal PSRAM:       Not initialized / Disabled ⚠️");
  }

  // 2. Non-Volatile Storage (NVS) Health Check & Auto-Repair
  Serial.println("\n--- [2] NVS FLASH PARTITION INTEGRITY ---");
  esp_err_t nvsErr = nvs_flash_init();
  if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("⚠️  NVS partition is corrupt or full! Erasing corrupted partition...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvsErr = nvs_flash_init();
  }
  if (nvsErr == ESP_OK) {
    Serial.println("✅ NVS Flash Initialized Cleanly.");
  } else {
    Serial.printf("❌ NVS Flash Error: 0x%04X (May cause Wi-Fi stack panic if persistent)\n", nvsErr);
  }

  // 3. Wi-Fi Stack Initialization (Volatile RAM Mode)
  Serial.println("\n--- [3] 2.4GHz RF RADIO TEST (STATION SCAN) ---");
  // Ensure Wi-Fi does NOT write credentials to flash
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(100);

  WiFi.mode(WIFI_STA);
  delay(100);

  Serial.println("Scanning 2.4GHz Wi-Fi channels for nearby access points...");
  int scanCount = WiFi.scanNetworks(false, true); // synchronous scan, show hidden
  if (scanCount < 0) {
    Serial.printf("❌ Wi-Fi Scan FAILED (Error code: %d)\n", scanCount);
    Serial.println("   -> This indicates an issue with the 2.4GHz RF transceiver or radio calibration.");
  } else if (scanCount == 0) {
    Serial.println("⚠️  Scan finished: 0 networks found. (Check antenna connection)");
  } else {
    Serial.printf("✅ RF Receiver Functional! Found %d networks nearby:\n", scanCount);
    for (int i = 0; i < scanCount && i < 6; i++) {
      Serial.printf("   [%d] %-20s  RSSI: %3d dBm  Channel: %2d  Auth: %s\n",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    WiFi.channel(i),
                    (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted");
    }
  }
  WiFi.scanDelete();

  // 4. Access Point (SoftAP) Mode Test
  Serial.println("\n--- [4] SOFT AP BROADCAST TEST ---");
  Serial.println("Configuring SoftAP 'EType-Drafts'...");
  WiFi.mode(WIFI_AP);
  delay(100);

  IPAddress localIp(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(localIp, gateway, subnet);

  bool apStarted = WiFi.softAP("EType-Drafts", nullptr, 1, 0, 4);
  if (apStarted) {
    Serial.println("✅ SoftAP 'EType-Drafts' is BROADCASTING!");
    Serial.printf("   SSID:      EType-Drafts (Open / No Password)\n");
    Serial.printf("   IP:        http://%s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("   MAC:       %s\n", WiFi.softAPmacAddress().c_str());
  } else {
    Serial.println("❌ Failed to start SoftAP.");
  }

  // 5. Minimal Web Server Test
  Serial.println("\n--- [5] EMBEDDED WEB SERVER TEST ---");
  server.on("/", []() {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>E-Type Diagnostic</title>";
    html += "<style>";
    html += "body { font-family: -apple-system, monospace; background: #f5f0e8; color: #2a2a2a; margin: 0; padding: 20px; }";
    html += ".card { background: white; max-width: 480px; margin: 20px auto; padding: 24px; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.08); }";
    html += "h2 { margin-top: 0; font-size: 1.3rem; }";
    html += ".badge { display: inline-block; background: #2e7d32; color: white; padding: 4px 10px; border-radius: 12px; font-size: 0.8rem; font-weight: bold; }";
    html += "ul { list-style: none; padding-left: 0; }";
    html += "li { padding: 6px 0; border-bottom: 1px solid #eee; font-size: 0.95rem; }";
    html += ".btn { display: inline-block; margin-top: 15px; padding: 10px 16px; background: #2a2a2a; color: white; text-decoration: none; border-radius: 6px; }";
    html += "</style></head><body>";
    html += "<div class='card'>";
    html += "<h2>⌨️ E-Type Wi-Fi Diagnostic</h2>";
    html += "<p><span class='badge'>WI-FI & WEB SERVER WORKING!</span></p>";
    html += "<ul>";
    html += "<li><b>Chip:</b> " + String(ESP.getChipModel()) + " (Rev " + String(ESP.getChipRevision()) + ")</li>";
    html += "<li><b>Uptime:</b> " + String(millis() / 1000) + " seconds</li>";
    html += "<li><b>Free Heap:</b> " + String(ESP.getFreeHeap() / 1024) + " KB</li>";
    html += "<li><b>Free PSRAM:</b> " + String(ESP.getFreePsram() / 1024) + " KB</li>";
    html += "</ul>";
    html += "<p>If you see this page, the ESP32 Wi-Fi radio, TCP/IP stack, and HTTP server are 100% operational!</p>";
    html += "<a href='/' class='btn'>Refresh Stats</a>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
    Serial.printf("[HTTP] Client served diagnostic page! (Uptime: %lus)\n", millis() / 1000);
  });

  server.begin();
  Serial.println("✅ Web Server started on port 80.");
  Serial.println("==================================================");
  Serial.println("READY: Connect your phone or Mac to 'EType-Drafts'");
  Serial.println("and navigate to http://192.168.4.1 in your browser.");
  Serial.println("==================================================\n");
}

void loop() {
  server.handleClient();

  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 3000) {
    lastHeartbeat = millis();
    int stationCount = WiFi.softAPgetStationNum();
    Serial.printf("[HEARTBEAT %4lus] Connected Stations: %d | Free Heap: %3u KB | Free PSRAM: %3u KB\n",
                  millis() / 1000,
                  stationCount,
                  ESP.getFreeHeap() / 1024,
                  ESP.getFreePsram() / 1024);
  }
}
