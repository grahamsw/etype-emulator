/*
 * E-Type Hardware: Step 4 - Live Interactive E-Ink Typewriter
 * Hardware: ESP32-S3 (N16R8) + Waveshare 4.2" E-Paper (400x300) + USB Keyboard
 * 
 * Features:
 *   - Clean, text-first canvas inspired by the E-Type web emulator.
 *   - 11 lines of text with 33 columns for maximum writing area.
 *   - Intelligent word wrap: words are never split across lines.
 *   - 2/3 Screen Scrolling: When line 11 fills, scrolls 7 lines up, keeping 4 lines of
 *     context at top so the writer never loses orientation.
 *   - Invisible Strikethrough: Striking [Ctrl+Backspace] or [Alt+Backspace] draws a
 *     physical strike line through the preceding word on the e-ink screen (s̶o̶t̶r̶m̶y̶)
 *     without typing any clutter characters, and exports as Markdown '~~word~~'.
 *   - Three Writing Modes:
 *       1. Forward-Only (Default): Backspace locked, pure momentum drafting.
 *       2. Flow (15m Sprint / 25m Pomodoro): Timed writing sprints with live status bar countdown.
 *       3. Edit Mode: Backspace, Delete, and Arrow keys enabled for revisions.
 *   - LittleFS Flash Storage: Auto-saves drafts under /draft_001.txt, etc.
 *   - Control Mode ([ESC] key):
 *       [N] New Draft
 *       [O] Open Draft
 *       [R] Rename Active Draft
 *       [E] Export Files & Web OTA Firmware Flasher (http://192.168.4.1)
 *       [G] Sync Active Draft to Google Drive
 *       [M] Toggle Writing Mode (Forward / Flow 15m / Flow 25m / Edit)
 *       [ESC] Return to Writing
 *   - Web OTA: Wirelessly update firmware (.bin) via browser—never unscrew keyboard wires again!
 *   - Google Drive Sync: Direct wireless upload to Google Drive folder 'E-Type Drafts'.
 */

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <EspUsbHost.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// -------------------------------------------------------------------------
// Display Driver: Waveshare 4.2" V2 B&W (SSD1683 / GDEY042T81)
// -------------------------------------------------------------------------
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(/*CS=*/ 10, /*DC=*/ 9, /*RST=*/ 8, /*BUSY=*/ 7)
);

const int PIN_SPI_SCK  = 12;
const int PIN_SPI_MOSI = 11;
const int PIN_SPI_MISO = -1;
const int PIN_SPI_CS   = 10;

// -------------------------------------------------------------------------
// Screen Geometry & Layout Tokens
// -------------------------------------------------------------------------
#define MAX_COLS            33   // Monospace characters per line (~11px per char)
#define MAX_ROWS            11   // Visible lines of text on 4.2" screen
#define SCROLL_KEEP_LINES   4    // Lines of context retained at top on scroll
#define SCROLL_LINES        7    // (MAX_ROWS - SCROLL_KEEP_LINES) lines to scroll up
#define LINE_HEIGHT         22   // Comfortable line height
#define TEXT_START_Y        32   // Top margin: first line baseline
#define MARGIN_X            16   // Left & right margins (368px writable width)
#define STATUS_LINE_Y       272  // Dividing line above status bar
#define STATUS_TEXT_Y       290  // Baseline of bottom status text

// -------------------------------------------------------------------------
// Writing Modes & Sprint System
// -------------------------------------------------------------------------
enum WritingMode {
  MODE_FORWARD_ONLY = 0,
  MODE_FLOW_15      = 1,
  MODE_FLOW_25      = 2,
  MODE_EDIT         = 3
};

WritingMode currentWritingMode = MODE_FORWARD_ONLY;

bool flowSprintActive = false;
unsigned long flowSprintStartTime = 0;
unsigned long flowSprintDurationMs = 15 * 60 * 1000;
int flowSprintStartWords = 0;

enum AppMode {
  MODE_TYPING,
  MODE_CONTROL_MENU,
  MODE_FILE_SELECT,
  MODE_RENAME,
  MODE_WIFI_EXPORT
};

AppMode currentMode = MODE_TYPING;

char pageBuffer[MAX_ROWS][MAX_COLS + 1];
bool struckChar[MAX_ROWS][MAX_COLS + 1];
int cursorRow = 0;
int cursorCol = 0;
bool keyboardConnected = false;

// Complete draft text buffer (holds all text in active document)
String currentDraftText = "";
char activeDraft[32] = "/draft_001.txt";
int totalDraftWords = 0;
bool draftDirty = false;
unsigned long lastTypeTime = 0;

const char* statusNotice = nullptr;
unsigned long statusNoticeTime = 0;

// Rename input buffer
char renameBuffer[32];
int renameCursor = 0;

// Home Wi-Fi & Google Sync Config
String wifiSSID = "";
String wifiPass = "";
String googleScriptUrl = "";

// Draft file info structure for File Picker & Web Server
struct DraftInfo {
  char name[32];
  size_t size;
  int words;
};

#define MAX_DRAFTS 16
DraftInfo draftList[MAX_DRAFTS];
int draftCount = 0;

// -------------------------------------------------------------------------
// Web Server (On-Demand WiFi Export & Web OTA)
// -------------------------------------------------------------------------
WebServer server(80);
bool wifiActive = false;

// -------------------------------------------------------------------------
// USB Host & FreeRTOS Keystroke Queue
// -------------------------------------------------------------------------
EspUsbHost usb;
QueueHandle_t keyQueue = NULL;

struct KeyMessage {
  uint8_t ascii;
  uint8_t keycode;
  uint8_t modifiers;
};

// -------------------------------------------------------------------------
// Forward Declarations
// -------------------------------------------------------------------------
void saveCurrentDraft();
void createNewDraft();
void loadDraft(const char* filename);
void scanDraftFiles();
void loadConfig();
void saveConfig(const String &ssid, const String &pass, const String &url);
void syncToGoogleDrive();
void startWiFiExport();
void stopWiFiExport();
void redrawEntirePage();
void renderLinePartial(int row);
void renderStatusBarPartial();
void drawControlMenu();
void drawFileSelectMenu();
void drawRenameScreen();
void drawWiFiExportScreen();
void scrollTwoThirds(const char* wrappedWord = nullptr);
String convertToMarkdown(const String &raw);
int countWords(const String &text);

// -------------------------------------------------------------------------
// Markdown Conversion & Word Count Utilities
// -------------------------------------------------------------------------
String convertToMarkdown(const String &raw) {
  String result = "";
  result.reserve(raw.length() + 256);

  int start = 0;
  while (start < (int)raw.length()) {
    int end = raw.indexOf('\n', start);
    if (end == -1) end = raw.length();

    String line = raw.substring(start, end);

    // Replace any legacy 'xxxxx' or 'XXXXX' tokens with strikethrough if present
    int xPos;
    while ((xPos = line.indexOf("xxxxx")) != -1 || (xPos = line.indexOf("XXXXX")) != -1) {
      int wEnd = xPos;
      while (wEnd > 0 && (line[wEnd - 1] == ' ' || line[wEnd - 1] == '\t')) wEnd--;

      int wStart = wEnd;
      while (wStart > 0 && line[wStart - 1] != ' ' && line[wStart - 1] != '\t') wStart--;

      if (wEnd > wStart) {
        String before = line.substring(0, wStart);
        String word = line.substring(wStart, wEnd);
        String after = line.substring(xPos + 5);
        if (after.startsWith(" ")) after = after.substring(1);

        if (!word.startsWith("~~")) {
          word = "~~" + word + "~~";
        }
        line = before + word + (after.length() > 0 ? (" " + after) : "");
      } else {
        line = line.substring(0, xPos) + line.substring(xPos + 5);
      }
    }

    result += line;
    if (end < (int)raw.length()) result += "\n";
    start = end + 1;
  }
  return result;
}

int countWords(const String &raw) {
  String md = convertToMarkdown(raw);
  int count = 0;
  bool inWord = false;
  bool inStrike = false;

  for (unsigned int i = 0; i < md.length(); i++) {
    if (i + 1 < md.length() && md[i] == '~' && md[i + 1] == '~') {
      inStrike = !inStrike;
      i++; // Skip second ~
      continue;
    }
    char ch = md[i];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      inWord = false;
    } else if (!inWord) {
      inWord = true;
      if (!inStrike) {
        count++; // Only count words that are NOT struck out!
      }
    }
  }
  return count;
}

// -------------------------------------------------------------------------
// Configuration Storage (Home Wi-Fi & Google Sync)
// -------------------------------------------------------------------------
void loadConfig() {
  if (LittleFS.exists("/config.txt")) {
    File f = LittleFS.open("/config.txt", "r");
    if (f) {
      wifiSSID = f.readStringUntil('\n');
      wifiSSID.trim();
      wifiPass = f.readStringUntil('\n');
      wifiPass.trim();
      googleScriptUrl = f.readStringUntil('\n');
      googleScriptUrl.trim();
      f.close();
      Serial.println("[Config] Loaded Home Wi-Fi & Google Script URL.");
    }
  }
}

void saveConfig(const String &ssid, const String &pass, const String &url) {
  File f = LittleFS.open("/config.txt", "w");
  if (f) {
    f.println(ssid);
    f.println(pass);
    f.println(url);
    f.close();
    wifiSSID = ssid;
    wifiPass = pass;
    googleScriptUrl = url;
    Serial.println("[Config] Saved Wi-Fi & Google Sync settings.");
  }
}

// -------------------------------------------------------------------------
// LittleFS Storage Subsystem
// -------------------------------------------------------------------------
void initStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("[LittleFS] Mount failed! Formatting...");
  } else {
    Serial.println("[LittleFS] Mounted successfully.");
  }

  loadConfig();
  scanDraftFiles();

  if (draftCount == 0) {
    createNewDraft();
  } else {
    loadDraft(draftList[0].name);
  }
}

void scanDraftFiles() {
  draftCount = 0;
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    Serial.println("[LittleFS] Failed to open root directory.");
    return;
  }

  File file = root.openNextFile();
  while (file && draftCount < MAX_DRAFTS) {
    String fname = file.name();
    if (!fname.startsWith("/")) fname = "/" + fname;

    if (fname.endsWith(".txt") || fname.endsWith(".md")) {
      if (fname != "/config.txt") {
        strncpy(draftList[draftCount].name, fname.c_str(), sizeof(draftList[draftCount].name));
        draftList[draftCount].size = file.size();

        String content = file.readString();
        draftList[draftCount].words = countWords(content);
        draftCount++;
      }
    }
    file = root.openNextFile();
  }
  root.close();

  // Sort drafts alphabetically
  for (int i = 0; i < draftCount - 1; i++) {
    for (int j = i + 1; j < draftCount; j++) {
      if (strcmp(draftList[i].name, draftList[j].name) > 0) {
        DraftInfo tmp = draftList[i];
        draftList[i] = draftList[j];
        draftList[j] = tmp;
      }
    }
  }
}

void saveCurrentDraft() {
  if (strlen(activeDraft) == 0) return;

  File f = LittleFS.open(activeDraft, "w");
  if (!f) {
    Serial.printf("[LittleFS] Error opening %s for write\n", activeDraft);
    return;
  }
  f.print(currentDraftText);
  f.close();
  draftDirty = false;
  Serial.printf("[LittleFS] Saved %s (%d bytes, %d words)\n", activeDraft, currentDraftText.length(), totalDraftWords);
}

void populatePageBufferFromText(const String &text) {
  for (int r = 0; r < MAX_ROWS; r++) {
    memset(pageBuffer[r], 0, sizeof(pageBuffer[r]));
    memset(struckChar[r], 0, sizeof(struckChar[r]));
  }
  cursorRow = 0;
  cursorCol = 0;

  if (text.length() == 0) return;

  char tempLine[MAX_COLS + 1];
  int tempLen = 0;
  memset(tempLine, 0, sizeof(tempLine));

  char allLines[512][MAX_COLS + 1];
  int totalLines = 0;

  for (unsigned int i = 0; i < text.length() && totalLines < 510; i++) {
    char ch = text[i];
    if (ch == '\r') continue;

    if (ch == '\n') {
      tempLine[tempLen] = '\0';
      strncpy(allLines[totalLines++], tempLine, MAX_COLS + 1);
      tempLen = 0;
      memset(tempLine, 0, sizeof(tempLine));
      continue;
    }

    if (tempLen >= MAX_COLS) {
      int lastSpace = -1;
      for (int c = tempLen - 1; c >= 0; c--) {
        if (tempLine[c] == ' ') { lastSpace = c; break; }
      }
      if (lastSpace > 0 && lastSpace > (tempLen - 12)) {
        char wrapped[MAX_COLS + 1];
        strcpy(wrapped, &tempLine[lastSpace + 1]);
        tempLine[lastSpace] = '\0';
        strncpy(allLines[totalLines++], tempLine, MAX_COLS + 1);

        strcpy(tempLine, wrapped);
        tempLen = strlen(wrapped);
      } else {
        tempLine[tempLen] = '\0';
        strncpy(allLines[totalLines++], tempLine, MAX_COLS + 1);
        tempLen = 0;
        memset(tempLine, 0, sizeof(tempLine));
      }
    }
    tempLine[tempLen++] = ch;
  }

  tempLine[tempLen] = '\0';
  strncpy(allLines[totalLines++], tempLine, MAX_COLS + 1);

  int startLine = (totalLines > MAX_ROWS) ? (totalLines - MAX_ROWS) : 0;
  cursorRow = 0;
  for (int i = startLine; i < totalLines; i++) {
    strncpy(pageBuffer[cursorRow], allLines[i], MAX_COLS + 1);
    if (i == totalLines - 1) {
      cursorCol = strlen(pageBuffer[cursorRow]);
    } else {
      cursorRow++;
    }
  }
}

void loadDraft(const char* filename) {
  strncpy(activeDraft, filename, sizeof(activeDraft));
  File f = LittleFS.open(activeDraft, "r");
  if (!f) {
    Serial.printf("[LittleFS] Could not open %s\n", activeDraft);
    currentDraftText = "";
  } else {
    currentDraftText = f.readString();
    f.close();
  }

  totalDraftWords = countWords(currentDraftText);
  populatePageBufferFromText(currentDraftText);
  draftDirty = false;
  Serial.printf("[LittleFS] Loaded %s: %d words\n", activeDraft, totalDraftWords);
}

void createNewDraft() {
  saveCurrentDraft();

  scanDraftFiles();
  int nextNum = 1;
  while (true) {
    char candidate[32];
    snprintf(candidate, sizeof(candidate), "/draft_%03d.txt", nextNum);
    bool exists = false;
    for (int i = 0; i < draftCount; i++) {
      if (strcmp(draftList[i].name, candidate) == 0) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      strncpy(activeDraft, candidate, sizeof(activeDraft));
      break;
    }
    nextNum++;
  }

  currentDraftText = "";
  totalDraftWords = 0;
  draftDirty = false;

  for (int r = 0; r < MAX_ROWS; r++) {
    memset(pageBuffer[r], 0, sizeof(pageBuffer[r]));
    memset(struckChar[r], 0, sizeof(struckChar[r]));
  }
  cursorRow = 0;
  cursorCol = 0;

  saveCurrentDraft();
  Serial.printf("[LittleFS] Created fresh new draft: %s\n", activeDraft);
}

// -------------------------------------------------------------------------
// 2/3 Screen Scrolling Architecture
// -------------------------------------------------------------------------
void scrollTwoThirds(const char* wrappedWord) {
  Serial.println("[Typewriter] Scrolling 2/3 screen (retaining 4 lines)...");

  saveCurrentDraft();

  // Shift rows 7..10 into rows 0..3 (4 lines of context kept at top)
  for (int r = 0; r < SCROLL_KEEP_LINES; r++) {
    strncpy(pageBuffer[r], pageBuffer[r + SCROLL_LINES], MAX_COLS + 1);
    memcpy(struckChar[r], struckChar[r + SCROLL_LINES], MAX_COLS + 1);
  }

  // Clear rows 4..10
  for (int r = SCROLL_KEEP_LINES; r < MAX_ROWS; r++) {
    memset(pageBuffer[r], 0, sizeof(pageBuffer[r]));
    memset(struckChar[r], 0, sizeof(struckChar[r]));
  }

  cursorRow = SCROLL_KEEP_LINES; // line 4
  if (wrappedWord != nullptr && strlen(wrappedWord) > 0) {
    strncpy(pageBuffer[cursorRow], wrappedWord, MAX_COLS);
    cursorCol = strlen(wrappedWord);
  } else {
    cursorCol = 0;
  }
}

// -------------------------------------------------------------------------
// Screen Rendering & Visual Strikethrough Engine
// -------------------------------------------------------------------------
void drawPageLine(int row, int y) {
  display.setFont(&FreeMono9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(MARGIN_X, y);
  display.print(pageBuffer[row]);

  if (row == cursorRow && keyboardConnected) {
    display.print("_");
  }

  // Draw 2-pixel black strikeout lines for any characters marked in struckChar
  int col = 0;
  int lineLen = strlen(pageBuffer[row]);
  while (col < lineLen) {
    if (struckChar[row][col]) {
      int startCol = col;
      while (col < lineLen && struckChar[row][col]) {
        col++;
      }
      int endCol = col;
      int x1 = MARGIN_X + (startCol * 11);
      int x2 = MARGIN_X + (endCol * 11) - 2;
      int yStrike = y - 5;
      display.drawLine(x1, yStrike, x2, yStrike, GxEPD_BLACK);
      display.drawLine(x1, yStrike + 1, x2, yStrike + 1, GxEPD_BLACK);
    } else {
      col++;
    }
  }
}

void drawStatusBar() {
  display.drawLine(MARGIN_X, STATUS_LINE_Y, display.width() - MARGIN_X, STATUS_LINE_Y, GxEPD_BLACK);

  display.setFont(&FreeMono9pt7b);
  display.setTextColor(GxEPD_BLACK);

  // Left: Status notice, connection status, Flow sprint timer, or draft title
  display.setCursor(MARGIN_X, STATUS_TEXT_Y);
  if (statusNotice != nullptr) {
    display.print(statusNotice);
  } else if (!keyboardConnected) {
    display.print("[Connecting keyboard...]");
  } else if (currentWritingMode == MODE_FLOW_15 || currentWritingMode == MODE_FLOW_25) {
    char flowBuf[48];
    long elapsedMs = millis() - flowSprintStartTime;
    long remainingMs = flowSprintDurationMs - elapsedMs;
    if (remainingMs > 0) {
      int minsLeft = (remainingMs + 59999) / 60000;
      snprintf(flowBuf, sizeof(flowBuf), "Flow: %dm left", minsLeft);
    } else {
      snprintf(flowBuf, sizeof(flowBuf), "Sprint Complete!");
    }
    display.print(flowBuf);
  } else {
    char titleBuf[48];
    const char* dName = (activeDraft[0] == '/') ? activeDraft + 1 : activeDraft;
    if (currentWritingMode == MODE_EDIT) {
      snprintf(titleBuf, sizeof(titleBuf), "[EDIT] %s", dName);
    } else {
      snprintf(titleBuf, sizeof(titleBuf), "%s", dName);
    }
    display.print(titleBuf);
  }

  // Right: Word count (includes sprint gain if in Flow Mode)
  char countStr[32];
  if (currentWritingMode == MODE_FLOW_15 || currentWritingMode == MODE_FLOW_25) {
    int sprintWords = totalDraftWords - flowSprintStartWords;
    if (sprintWords < 0) sprintWords = 0;
    snprintf(countStr, sizeof(countStr), "%d w (+%d)", totalDraftWords, sprintWords);
  } else {
    snprintf(countStr, sizeof(countStr), "%d words", totalDraftWords);
  }

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(countStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(display.width() - MARGIN_X - w, STATUS_TEXT_Y);
  display.print(countStr);
}

void redrawEntirePage() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    for (int r = 0; r <= cursorRow && r < MAX_ROWS; r++) {
      int y = TEXT_START_Y + (r * LINE_HEIGHT);
      drawPageLine(r, y);
    }

    drawStatusBar();
  } while (display.nextPage());
}

void renderLinePartial(int row) {
  int y = TEXT_START_Y + (row * LINE_HEIGHT);
  int windowY = y - 16;
  int windowH = LINE_HEIGHT;
  int windowW = display.width() - (MARGIN_X * 2);

  display.setPartialWindow(MARGIN_X, windowY, windowW, windowH);
  display.firstPage();
  do {
    display.fillRect(MARGIN_X, windowY, windowW, windowH, GxEPD_WHITE);
    drawPageLine(row, y);
  } while (display.nextPage());
}

void renderStatusBarPartial() {
  int windowY = STATUS_LINE_Y;
  int windowH = display.height() - STATUS_LINE_Y;
  int windowW = display.width() - (MARGIN_X * 2);

  display.setPartialWindow(MARGIN_X, windowY, windowW, windowH);
  display.firstPage();
  do {
    display.fillRect(MARGIN_X, windowY, windowW, windowH, GxEPD_WHITE);
    drawStatusBar();
  } while (display.nextPage());
}

// -------------------------------------------------------------------------
// Screen Rendering Functions (Menus & Export)
// -------------------------------------------------------------------------
void drawControlMenu() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(MARGIN_X, 32);
    display.print("E-TYPE MENU [OTA v1.3]");
    display.drawLine(MARGIN_X, 42, display.width() - MARGIN_X, 42, GxEPD_BLACK);

    display.setFont(&FreeMono9pt7b);
    int y = 74;
    int dy = 28;

    display.setCursor(MARGIN_X, y);          display.print("[N]   New Draft");
    display.setCursor(MARGIN_X, y + dy);     display.print("[O]   Open / Switch Draft");
    display.setCursor(MARGIN_X, y + 2 * dy); display.print("[R]   Rename Active Draft");
    display.setCursor(MARGIN_X, y + 3 * dy); display.print("[E]   Export & Web OTA (WiFi)");
    display.setCursor(MARGIN_X, y + 4 * dy); display.print("[G]   Sync to Google Drive");

    display.setCursor(MARGIN_X, y + 5 * dy);
    display.print("[M]   Mode: ");
    if (currentWritingMode == MODE_FORWARD_ONLY) {
      display.print("Forward-Only");
    } else if (currentWritingMode == MODE_FLOW_15) {
      display.print("Flow (15m Sprint)");
    } else if (currentWritingMode == MODE_FLOW_25) {
      display.print("Flow (25m Pomodoro)");
    } else {
      display.print("Edit (Revisions)");
    }

    display.setCursor(MARGIN_X, y + 6 * dy); display.print("[ESC] Return to Writing");

    display.drawLine(MARGIN_X, STATUS_LINE_Y, display.width() - MARGIN_X, STATUS_LINE_Y, GxEPD_BLACK);
    display.setCursor(MARGIN_X, STATUS_TEXT_Y);
    if (statusNotice != nullptr) {
      display.setFont(&FreeMonoBold9pt7b);
      display.print(statusNotice);
    } else {
      char statusBuf[64];
      const char* dName = (activeDraft[0] == '/') ? activeDraft + 1 : activeDraft;
      snprintf(statusBuf, sizeof(statusBuf), "Active: %s (%d words)", dName, totalDraftWords);
      display.print(statusBuf);
    }
  } while (display.nextPage());
}

void drawFileSelectMenu() {
  scanDraftFiles();

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(MARGIN_X, 32);
    display.print("SELECT DRAFT TO OPEN");
    display.drawLine(MARGIN_X, 42, display.width() - MARGIN_X, 42, GxEPD_BLACK);

    display.setFont(&FreeMono9pt7b);
    int y = 68;
    int dy = 22;

    for (int i = 0; i < draftCount && i < 8; i++) {
      display.setCursor(MARGIN_X, y + (i * dy));
      char item[64];
      const char* dName = (draftList[i].name[0] == '/') ? draftList[i].name + 1 : draftList[i].name;
      bool isActive = (strcmp(draftList[i].name, activeDraft) == 0);
      snprintf(item, sizeof(item), "[%d] %s (%dw)%s", 
               i + 1, 
               dName, 
               draftList[i].words,
               isActive ? " *" : "");
      display.print(item);
    }

    display.drawLine(MARGIN_X, STATUS_LINE_Y, display.width() - MARGIN_X, STATUS_LINE_Y, GxEPD_BLACK);
    display.setCursor(MARGIN_X, STATUS_TEXT_Y);
    display.print("Press [1-8] to open, [ESC] back");
  } while (display.nextPage());
}

void drawRenameScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(MARGIN_X, 32);
    display.print("RENAME ACTIVE DRAFT");
    display.drawLine(MARGIN_X, 42, display.width() - MARGIN_X, 42, GxEPD_BLACK);

    display.setFont(&FreeMono9pt7b);
    int y = 74;

    display.setCursor(MARGIN_X, y);
    const char* dName = (activeDraft[0] == '/') ? activeDraft + 1 : activeDraft;
    display.printf("Current: %s", dName);

    display.setCursor(MARGIN_X, y + 36);
    display.print("Enter new title (.md / .txt):");

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(MARGIN_X, y + 72);
    display.print("> ");
    display.print(renameBuffer);
    display.print("_");

    display.setFont(&FreeMono9pt7b);
    display.drawLine(MARGIN_X, STATUS_LINE_Y, display.width() - MARGIN_X, STATUS_LINE_Y, GxEPD_BLACK);
    display.setCursor(MARGIN_X, STATUS_TEXT_Y);
    display.print("[Enter] Save  |  [ESC] Cancel");
  } while (display.nextPage());
}

void drawWiFiExportScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(MARGIN_X, 32);
    display.print("DRAFT EXPORT & WEB OTA");
    display.drawLine(MARGIN_X, 42, display.width() - MARGIN_X, 42, GxEPD_BLACK);

    display.setFont(&FreeMono9pt7b);
    int y = 68;
    int dy = 24;

    display.setCursor(MARGIN_X, y);
    display.print("1. Connect to WiFi network:");
    display.setCursor(MARGIN_X + 16, y + dy);
    display.print("SSID: EType-Drafts");
    display.setCursor(MARGIN_X + 16, y + 2 * dy);
    display.print("Pass: (none)");

    display.setCursor(MARGIN_X, y + 3 * dy + 6);
    display.print("2. Open browser to:");
    display.setCursor(MARGIN_X + 16, y + 4 * dy + 6);
    display.print("http://192.168.4.1");

    display.setCursor(MARGIN_X, y + 5 * dy + 12);
    display.print("Download, OTA Flash, Google Sync.");

    display.drawLine(MARGIN_X, STATUS_LINE_Y, display.width() - MARGIN_X, STATUS_LINE_Y, GxEPD_BLACK);
    display.setCursor(MARGIN_X, STATUS_TEXT_Y);
    display.print("Press [ESC] to turn off WiFi");
  } while (display.nextPage());
}

// -------------------------------------------------------------------------
// Google Drive Sync Engine
// -------------------------------------------------------------------------
void syncToGoogleDrive() {
  bool wasAP = wifiActive;
  loadConfig();
  if (wifiSSID.length() == 0 || googleScriptUrl.length() == 0) {
    statusNotice = "[No Wi-Fi/URL Saved - Press E]";
    statusNoticeTime = millis();
    drawControlMenu();
    return;
  }

  statusNotice = "[Connecting Wi-Fi...]";
  statusNoticeTime = millis();
  drawControlMenu();

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(300);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    statusNotice = "[Wi-Fi Failed: Check SSID/Pass]";
    statusNoticeTime = millis();
    WiFi.disconnect(true);
    if (wasAP) {
      WiFi.mode(WIFI_AP);
      WiFi.softAP("EType-Drafts");
    } else {
      WiFi.mode(WIFI_OFF);
    }
    drawControlMenu();
    return;
  }

  statusNotice = "[Connected! Syncing to Drive...]";
  statusNoticeTime = millis();
  drawControlMenu();

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (http.begin(client, googleScriptUrl)) {
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/json");

    String mdContent = convertToMarkdown(currentDraftText);
    String escaped = "";
    escaped.reserve(mdContent.length() + 128);
    for (unsigned int i = 0; i < mdContent.length(); i++) {
      char c = mdContent[i];
      if (c == '"') escaped += "\\\"";
      else if (c == '\\') escaped += "\\\\";
      else if (c == '\n') escaped += "\\n";
      else if (c == '\r') continue;
      else if (c == '\t') escaped += "\\t";
      else escaped += c;
    }

    String baseName = (activeDraft[0] == '/') ? String(activeDraft + 1) : String(activeDraft);
    if (baseName.endsWith(".txt")) {
      baseName = baseName.substring(0, baseName.length() - 4) + ".md";
    }

    String postData = "{\"filename\":\"" + baseName + "\",\"content\":\"" + escaped + "\"}";
    int httpCode = http.POST(postData);

    if (httpCode == 200 || httpCode == 302) {
      Serial.printf("[Google Sync] Success! HTTP %d\n", httpCode);
      statusNotice = "[Synced to Google Drive!]";
    } else if (httpCode > 0) {
      Serial.printf("[Google Sync] HTTP %d\n", httpCode);
      static char errBuf[32];
      snprintf(errBuf, sizeof(errBuf), "[Google HTTP %d Error]", httpCode);
      statusNotice = errBuf;
    } else {
      Serial.printf("[Google Sync] Error: %s\n", http.errorToString(httpCode).c_str());
      statusNotice = "[Sync Request Failed]";
    }
    http.end();
  } else {
    statusNotice = "[HTTP Init Failed]";
  }

  statusNoticeTime = millis();
  WiFi.disconnect(true);
  if (wasAP) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("EType-Drafts");
  } else {
    WiFi.mode(WIFI_OFF);
  }
  drawControlMenu();
}

// -------------------------------------------------------------------------
// WiFi Web Server Handlers (Markdown, OTA Flasher, Google Sync)
// -------------------------------------------------------------------------
void handleWebRoot() {
  scanDraftFiles();
  loadConfig();

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>E-Type Typewriter Portal</title><style>";
  html += ":root { --paper: #f5f0e8; --ink: #2a2a2a; --card: #ece6dc; --border: #d4cfc7; --accent: #5a5550; --danger: #b33939; }";
  html += "* { box-sizing: border-box; margin: 0; padding: 0; }";
  html += "body { background: var(--paper); color: var(--ink); font-family: 'Courier Prime', monospace, monospace; padding: 24px 16px; line-height: 1.5; }";
  html += ".container { max-width: 680px; margin: 0 auto; }";
  html += "header { border-bottom: 2px solid var(--ink); padding-bottom: 12px; margin-bottom: 24px; display: flex; justify-content: space-between; align-items: baseline; }";
  html += "h1 { font-size: 1.4rem; letter-spacing: 0.05em; }";
  html += ".card { background: var(--card); border: 1px solid var(--border); border-radius: 6px; padding: 14px 16px; margin-bottom: 14px; }";
  html += ".card-row { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 10px; }";
  html += ".card-info { flex: 1; min-width: 180px; }";
  html += ".card-title { font-weight: bold; font-size: 1.05rem; }";
  html += ".card-meta { font-size: 0.85rem; color: var(--accent); margin-top: 4px; }";
  html += ".actions { display: flex; gap: 8px; flex-wrap: wrap; }";
  html += ".btn { display: inline-block; padding: 6px 12px; font-family: inherit; font-size: 0.85rem; text-decoration: none; border: 1px solid var(--ink); border-radius: 4px; background: var(--paper); color: var(--ink); cursor: pointer; }";
  html += ".btn:hover { background: #e5ded3; }";
  html += ".btn-primary { background: var(--ink); color: var(--paper); }";
  html += ".btn-primary:hover { background: #404040; }";
  html += ".btn-danger { border-color: var(--danger); color: var(--danger); }";
  html += ".btn-danger:hover { background: #fbeae8; }";
  html += "input[type='text'], input[type='password'] { width: 100%; padding: 6px 10px; font-family: inherit; font-size: 0.85rem; border: 1px solid var(--border); border-radius: 4px; background: var(--paper); color: var(--ink); margin-top: 4px; }";
  html += ".form-group { margin-bottom: 10px; }";
  html += "label { font-size: 0.8rem; color: var(--accent); display: block; }";
  html += ".footer { margin-top: 28px; text-align: center; font-size: 0.8rem; color: var(--accent); border-top: 1px solid var(--border); padding-top: 14px; }";
  html += "</style></head><body><div class='container'>";
  html += "<header><h1>E - T Y P E v1.3</h1><span>Control Portal</span></header>";

  html += "<h2 style='font-size:1.1rem;margin-bottom:12px;'>Saved Drafts</h2>";

  if (draftCount == 0) {
    html += "<p style='padding: 16px 0; color: var(--accent);'>No drafts found on typewriter.</p>";
  } else {
    for (int i = 0; i < draftCount; i++) {
      const char* dName = (draftList[i].name[0] == '/') ? draftList[i].name + 1 : draftList[i].name;
      bool isActive = (strcmp(draftList[i].name, activeDraft) == 0);

      html += "<div class='card'><div class='card-row'>";
      html += "<div class='card-info'><div class='card-title'>" + String(dName) + (isActive ? " (Active)" : "") + "</div>";
      html += "<div class='card-meta'>" + String(draftList[i].words) + " words &bull; " + String(draftList[i].size) + " bytes</div></div>";
      html += "<div class='actions'>";
      html += "<a href='/view?file=" + String(dName) + "' class='btn' target='_blank'>View</a>";
      html += "<a href='/download?file=" + String(dName) + "' class='btn btn-primary' download>Download .md</a>";
      html += "<a href='/delete?file=" + String(dName) + "' class='btn btn-danger' onclick=\"return confirm('Permanently delete " + String(dName) + " from typewriter?');\">Delete</a>";
      html += "</div></div></div>";
    }
  }

  // Google Drive & Home Wi-Fi Card
  html += "<h2 style='font-size:1.1rem;margin:24px 0 12px 0;'>Google Drive & Home Wi-Fi</h2>";
  if (server.hasArg("saved")) {
    html += "<div style='background:#d4edda;color:#155724;border:1px solid #c3e6cb;padding:10px 14px;border-radius:4px;margin-bottom:14px;font-size:0.85rem;'><b>✓ Settings saved to typewriter flash!</b></div>";
  }
  html += "<div class='card'><form method='POST' action='/save_config'>";
  html += "<div class='form-group'><label>Home Wi-Fi Network Name (SSID):</label>";
  html += "<input type='text' name='ssid' value='" + wifiSSID + "' placeholder='MyHomeWiFi'></div>";
  html += "<div class='form-group'><label>Home Wi-Fi Password:</label>";
  html += "<input type='password' name='pass' value='" + wifiPass + "'></div>";
  html += "<div class='form-group'><label>Google Apps Script Webhook URL:</label>";
  html += "<input type='text' name='url' value='" + googleScriptUrl + "' placeholder='https://script.google.com/macros/s/.../exec'></div>";
  html += "<div style='display:flex;gap:10px;margin-top:14px;'>";
  html += "<button type='submit' class='btn btn-primary'>Save Wi-Fi & Google Settings</button>";
  html += "<a href='/sync_web' class='btn' onclick=\"alert('Syncing active draft to Google Drive...');\">Sync Active Draft Now</a>";
  html += "</div></form></div>";

  // Wireless Web OTA Firmware Flasher Card
  html += "<h2 style='font-size:1.1rem;margin:24px 0 12px 0;'>Wireless Firmware Update (OTA)</h2>";
  html += "<div class='card'>";
  html += "<p style='font-size:0.85rem;color:var(--accent);margin-bottom:12px;'>Upload compiled .bin to update typewriter wirelessly (never unscrew keyboard wires again!).</p>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='update' accept='.bin' required style='display:block;margin-bottom:12px;font-size:0.85rem;'>";
  html += "<button type='submit' class='btn btn-primary' onclick=\"this.innerText='Uploading & Flashing...';\">Flash Firmware (.bin)</button>";
  html += "</form></div>";

  html += "<div class='footer'>E-Type Distraction-Free Typewriter &bull; Press [ESC] on keyboard to exit WiFi</div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleWebDownload() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }
  String filename = server.arg("file");
  if (!filename.startsWith("/")) filename = "/" + filename;

  if (!LittleFS.exists(filename)) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  File f = LittleFS.open(filename, "r");
  String raw = f.readString();
  f.close();

  String md = convertToMarkdown(raw);

  String baseName = filename.substring(1);
  if (baseName.endsWith(".txt")) {
    baseName = baseName.substring(0, baseName.length() - 4) + ".md";
  }

  server.sendHeader("Content-Disposition", "attachment; filename=\"" + baseName + "\"");
  server.send(200, "text/markdown", md);
}

void handleWebView() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }
  String filename = server.arg("file");
  if (!filename.startsWith("/")) filename = "/" + filename;

  if (!LittleFS.exists(filename)) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  File f = LittleFS.open(filename, "r");
  String raw = f.readString();
  f.close();

  String md = convertToMarkdown(raw);
  server.send(200, "text/plain; charset=utf-8", md);
}

void handleWebDelete() {
  if (server.hasArg("file")) {
    String filename = server.arg("file");
    if (!filename.startsWith("/")) filename = "/" + filename;

    if (LittleFS.exists(filename)) {
      LittleFS.remove(filename);
      Serial.printf("[LittleFS] Deleted file: %s\n", filename.c_str());

      if (filename == activeDraft) {
        createNewDraft();
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSaveConfig() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String url  = server.arg("url");

  saveConfig(ssid, pass, url);
  server.sendHeader("Location", "/?saved=1");
  server.send(303);
}

void handleSyncWeb() {
  loadConfig();
  if (wifiSSID.length() == 0 || googleScriptUrl.length() == 0) {
    server.send(200, "text/html", "<body style='font-family:monospace;padding:30px;line-height:1.6;'><h3>Configuration Missing</h3><p>Please enter your Home Wi-Fi name, password, and Google Webhook URL, then click <b>Save Wi-Fi & Google Settings</b> first!</p><p><a href='/'>&larr; Return to Portal</a></p></body>");
    return;
  }
  syncToGoogleDrive();
  server.send(200, "text/html", "<body style='font-family:monospace;padding:30px;line-height:1.6;'><h3>Sync Triggered!</h3><p>Draft was sent to your Google Drive 'E-Type Drafts' folder.</p><p><a href='/'>&larr; Return to Portal</a></p></body>");
}

void startWiFiExport() {
  Serial.println("[WiFi] Starting Access Point 'EType-Drafts'...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("EType-Drafts");

  static bool routesConfigured = false;
  if (!routesConfigured) {
    server.on("/", handleWebRoot);
    server.on("/download", handleWebDownload);
    server.on("/view", handleWebView);
    server.on("/delete", handleWebDelete);
    server.on("/save_config", HTTP_POST, handleSaveConfig);
    server.on("/sync_web", handleSyncWeb);

    // Wireless Web OTA Firmware Flasher Route
    server.on("/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/html", "<body style='font-family:monospace;text-align:center;padding:50px;'><h2>Firmware Flashed Successfully!</h2><p>Rebooting typewriter...</p></body>");
      delay(800);
      ESP.restart();
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] Flashing: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
          Serial.printf("[OTA] Success: %u bytes\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }
    });

    routesConfigured = true;
  }
  server.begin();
  wifiActive = true;
  Serial.println("[WiFi] Web server & OTA active at http://192.168.4.1");
}

void stopWiFiExport() {
  if (wifiActive) {
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiActive = false;
    Serial.println("[WiFi] Web server & WiFi stopped. Radio OFF.");
  }
}

// -------------------------------------------------------------------------
// Setup
// -------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[E-Type] Initializing Typewriter Hardware...");

  currentDraftText.reserve(32768);

  initStorage();

  keyQueue = xQueueCreate(64, sizeof(KeyMessage));

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);

  display.init(115200, true, 2, false);
  display.setRotation(0);

  redrawEntirePage();

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
    Serial.println("[USB] Keyboard connected!");
    keyboardConnected = true;
    KeyMessage msg = { 0, 0xFF, 0 };
    xQueueSend(keyQueue, &msg, 0);
  });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
    Serial.println("[USB] Keyboard disconnected!");
    keyboardConnected = false;
    KeyMessage msg = { 0, 0xFE, 0 };
    xQueueSend(keyQueue, &msg, 0);
  });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (!event.pressed) return;

    KeyMessage msg;
    msg.ascii = event.ascii;
    msg.keycode = event.keycode;
    msg.modifiers = event.modifiers;

    xQueueSend(keyQueue, &msg, 0);
  });

  if (!usb.begin()) {
    Serial.printf("[ERROR] USB Host init failed: %s\n", usb.lastErrorName());
  } else {
    Serial.println("[USB] Host stack ready.");
  }
}

// -------------------------------------------------------------------------
// Main Loop: Keystrokes, Auto-Save & Web Server
// -------------------------------------------------------------------------
void loop() {
  if (currentMode == MODE_WIFI_EXPORT && wifiActive) {
    server.handleClient();
  }

  // Periodic Auto-save in Typing Mode (after 2s of typing pause)
  if (currentMode == MODE_TYPING && draftDirty && (millis() - lastTypeTime > 2000)) {
    saveCurrentDraft();
  }

  KeyMessage key;
  TickType_t waitTicks = (currentMode == MODE_WIFI_EXPORT) ? pdMS_TO_TICKS(1) : pdMS_TO_TICKS(20);

  if (xQueueReceive(keyQueue, &key, waitTicks) == pdTRUE) {
    // -----------------------------------------------------------------------
    // Connect / Disconnect Hardware Events
    // -----------------------------------------------------------------------
    if (key.keycode == 0xFF || key.keycode == 0xFE) {
      if (currentMode == MODE_TYPING) {
        redrawEntirePage();
      }
      return;
    }

    // -----------------------------------------------------------------------
    // ESC KEY (ASCII 0x1B or Keycode 0x29) - Mode Switching
    // -----------------------------------------------------------------------
    if (key.ascii == 0x1B || key.keycode == 0x29) {
      if (currentMode == MODE_TYPING) {
        saveCurrentDraft();
        currentMode = MODE_CONTROL_MENU;
        drawControlMenu();
        return;
      } else if (currentMode == MODE_CONTROL_MENU) {
        currentMode = MODE_TYPING;
        redrawEntirePage();
        return;
      } else if (currentMode == MODE_FILE_SELECT || currentMode == MODE_RENAME) {
        currentMode = MODE_CONTROL_MENU;
        drawControlMenu();
        return;
      } else if (currentMode == MODE_WIFI_EXPORT) {
        stopWiFiExport();
        currentMode = MODE_CONTROL_MENU;
        drawControlMenu();
        return;
      }
    }

    // -----------------------------------------------------------------------
    // MODE_CONTROL_MENU Handling
    // -----------------------------------------------------------------------
    if (currentMode == MODE_CONTROL_MENU) {
      // [N] New Draft
      if (key.ascii == 'n' || key.ascii == 'N') {
        createNewDraft();
        currentMode = MODE_TYPING;
        redrawEntirePage();
        return;
      }

      // [O] Open Draft
      if (key.ascii == 'o' || key.ascii == 'O') {
        currentMode = MODE_FILE_SELECT;
        drawFileSelectMenu();
        return;
      }

      // [R] Rename Active Draft
      if (key.ascii == 'r' || key.ascii == 'R') {
        currentMode = MODE_RENAME;
        const char* dName = (activeDraft[0] == '/') ? activeDraft + 1 : activeDraft;
        strncpy(renameBuffer, dName, sizeof(renameBuffer));
        renameCursor = strlen(renameBuffer);
        drawRenameScreen();
        return;
      }

      // [E] Export / Sync & OTA (WiFi Web)
      if (key.ascii == 'e' || key.ascii == 'E') {
        currentMode = MODE_WIFI_EXPORT;
        startWiFiExport();
        drawWiFiExportScreen();
        return;
      }

      // [G] Sync Active Draft to Google Drive
      if (key.ascii == 'g' || key.ascii == 'G') {
        syncToGoogleDrive();
        return;
      }

      // [M] Toggle Writing Mode: Forward-Only -> Flow 15m -> Flow 25m -> Edit
      if (key.ascii == 'm' || key.ascii == 'M') {
        currentWritingMode = (WritingMode)((currentWritingMode + 1) % 4);
        if (currentWritingMode == MODE_FLOW_15) {
          flowSprintDurationMs = 15 * 60 * 1000;
          flowSprintStartTime = millis();
          flowSprintStartWords = totalDraftWords;
          flowSprintActive = true;
        } else if (currentWritingMode == MODE_FLOW_25) {
          flowSprintDurationMs = 25 * 60 * 1000;
          flowSprintStartTime = millis();
          flowSprintStartWords = totalDraftWords;
          flowSprintActive = true;
        } else {
          flowSprintActive = false;
        }
        drawControlMenu();
        return;
      }

      return;
    }

    // -----------------------------------------------------------------------
    // MODE_RENAME Handling
    // -----------------------------------------------------------------------
    if (currentMode == MODE_RENAME) {
      // Backspace in rename
      if (key.ascii == 0x08 || key.keycode == 0x2A) {
        if (renameCursor > 0) {
          renameCursor--;
          renameBuffer[renameCursor] = '\0';
          drawRenameScreen();
        }
        return;
      }

      // Enter in rename: commit new name
      if (key.ascii == '\r' || key.ascii == '\n') {
        if (renameCursor > 0) {
          String newName = String(renameBuffer);
          if (!newName.endsWith(".txt") && !newName.endsWith(".md")) {
            newName += ".txt";
          }
          if (!newName.startsWith("/")) {
            newName = "/" + newName;
          }

          if (LittleFS.rename(activeDraft, newName.c_str())) {
            strncpy(activeDraft, newName.c_str(), sizeof(activeDraft));
            statusNotice = "[Draft Renamed]";
          } else {
            statusNotice = "[Rename Failed]";
          }
        }
        currentMode = MODE_CONTROL_MENU;
        statusNoticeTime = millis();
        drawControlMenu();
        return;
      }

      // Printable characters for filename
      if (key.ascii >= 0x20 && key.ascii <= 0x7E && renameCursor < 26) {
        char ch = (char)key.ascii;
        // Safe characters for filenames
        if (isalnum(ch) || ch == '_' || ch == '-' || ch == '.') {
          renameBuffer[renameCursor++] = ch;
          renameBuffer[renameCursor] = '\0';
          drawRenameScreen();
        }
      }
      return;
    }

    // -----------------------------------------------------------------------
    // MODE_FILE_SELECT Handling
    // -----------------------------------------------------------------------
    if (currentMode == MODE_FILE_SELECT) {
      if (key.ascii >= '1' && key.ascii <= '8') {
        int idx = key.ascii - '1';
        if (idx < draftCount) {
          saveCurrentDraft();
          loadDraft(draftList[idx].name);
          currentMode = MODE_TYPING;
          redrawEntirePage();
          return;
        }
      }
      return;
    }

    // -----------------------------------------------------------------------
    // MODE_WIFI_EXPORT Handling
    // -----------------------------------------------------------------------
    if (currentMode == MODE_WIFI_EXPORT) {
      return;
    }

    // -----------------------------------------------------------------------
    // MODE_TYPING
    // -----------------------------------------------------------------------
    bool needFullRedraw = false;
    bool lineChanged = false;
    bool statusChanged = false;
    int prevWordCount = totalDraftWords;

    // Drain burst keystrokes to ensure zero typing latency
    do {
      // F5 Key (0x3E): Force full refresh to clear ghosting on demand
      if (key.keycode == 0x3E) {
        statusNotice = "[Ghosting Cleared]";
        statusNoticeTime = millis();
        needFullRedraw = true;
        break;
      }

      // ---------------------------------------------------------------------
      // EDIT MODE: Backspace, Delete, and Arrow Key Navigation
      // ---------------------------------------------------------------------
      if (currentWritingMode == MODE_EDIT) {
        // Backspace
        if (key.ascii == 0x08 || key.keycode == 0x2A) {
          if (cursorCol > 0) {
            cursorCol--;
            pageBuffer[cursorRow][cursorCol] = '\0';
            struckChar[cursorRow][cursorCol] = false;
            if (currentDraftText.length() > 0) {
              currentDraftText.remove(currentDraftText.length() - 1);
            }
            draftDirty = true;
            lastTypeTime = millis();
            lineChanged = true;
            totalDraftWords = countWords(currentDraftText);
          }
          continue;
        }

        // Delete key
        if (key.ascii == 0x7F || key.keycode == 0x4C) {
          int len = strlen(pageBuffer[cursorRow]);
          if (cursorCol < len) {
            for (int c = cursorCol; c < len; c++) {
              pageBuffer[cursorRow][c] = pageBuffer[cursorRow][c + 1];
              struckChar[cursorRow][c] = struckChar[cursorRow][c + 1];
            }
            draftDirty = true;
            lastTypeTime = millis();
            lineChanged = true;
            totalDraftWords = countWords(currentDraftText);
          }
          continue;
        }

        // Arrow Left (0x50)
        if (key.keycode == 0x50 && cursorCol > 0) {
          cursorCol--;
          lineChanged = true;
          continue;
        }

        // Arrow Right (0x4F)
        if (key.keycode == 0x4F && cursorCol < (int)strlen(pageBuffer[cursorRow])) {
          cursorCol++;
          lineChanged = true;
          continue;
        }

        // Arrow Up (0x52)
        if (key.keycode == 0x52 && cursorRow > 0) {
          renderLinePartial(cursorRow);
          cursorRow--;
          if (cursorCol > (int)strlen(pageBuffer[cursorRow])) cursorCol = strlen(pageBuffer[cursorRow]);
          lineChanged = true;
          continue;
        }

        // Arrow Down (0x51)
        if (key.keycode == 0x51 && cursorRow < MAX_ROWS - 1 && strlen(pageBuffer[cursorRow + 1]) > 0) {
          renderLinePartial(cursorRow);
          cursorRow++;
          if (cursorCol > (int)strlen(pageBuffer[cursorRow])) cursorCol = strlen(pageBuffer[cursorRow]);
          lineChanged = true;
          continue;
        }
      }

      // Handle Enter / Return
      if (key.ascii == '\r' || key.ascii == '\n') {
        renderLinePartial(cursorRow);

        cursorRow++;
        cursorCol = 0;
        if (cursorRow < MAX_ROWS) {
          memset(pageBuffer[cursorRow], 0, sizeof(pageBuffer[cursorRow]));
          memset(struckChar[cursorRow], 0, sizeof(struckChar[cursorRow]));
        }
        currentDraftText += "\n";
        draftDirty = true;
        lastTypeTime = millis();

        if (cursorRow >= MAX_ROWS) {
          scrollTwoThirds();
          needFullRedraw = true;
          break;
        } else {
          lineChanged = true;
        }
        totalDraftWords = countWords(currentDraftText);
        continue;
      }

      // ---------------------------------------------------------------------
      // CTRL+BACKSPACE or ALT+BACKSPACE: Strikethrough Preceding Word
      // ---------------------------------------------------------------------
      bool isCtrlOrAlt = ((key.modifiers & 0x11) != 0) || ((key.modifiers & 0x44) != 0);
      bool isStrikeKey = (key.keycode == 0x2A && isCtrlOrAlt) || (key.ascii == 0x17); // 0x17 = Ctrl+W

      if (isStrikeKey) {
        int targetRow = cursorRow;
        int wEnd = cursorCol;

        // If at beginning of line and not first line, check end of previous line
        if (wEnd == 0 && targetRow > 0) {
          targetRow = cursorRow - 1;
          wEnd = strlen(pageBuffer[targetRow]);
        }

        // Skip any trailing spaces
        while (wEnd > 0 && (pageBuffer[targetRow][wEnd - 1] == ' ' || pageBuffer[targetRow][wEnd - 1] == '\t')) {
          wEnd--;
        }

        int wStart = wEnd;
        // Find start of previous unstruck word
        while (wStart > 0 && pageBuffer[targetRow][wStart - 1] != ' ' && 
               pageBuffer[targetRow][wStart - 1] != '\t' && 
               !struckChar[targetRow][wStart - 1]) {
          wStart--;
        }

        if (wEnd > wStart) {
          // Mark characters as struck out
          for (int c = wStart; c < wEnd; c++) {
            struckChar[targetRow][c] = true;
          }

          // Extract word
          char struckWordBuf[MAX_COLS + 1];
          int wordLen = wEnd - wStart;
          strncpy(struckWordBuf, &pageBuffer[targetRow][wStart], wordLen);
          struckWordBuf[wordLen] = '\0';

          // Wrap the word in ~~...~~ in currentDraftText
          int lastOccur = currentDraftText.lastIndexOf(struckWordBuf);
          if (lastOccur != -1) {
            if (lastOccur < 2 || currentDraftText.substring(lastOccur - 2, lastOccur) != "~~") {
              currentDraftText = currentDraftText.substring(0, lastOccur) + "~~" + String(struckWordBuf) + "~~" + currentDraftText.substring(lastOccur + wordLen);
            }
          }

          totalDraftWords = countWords(currentDraftText);
          draftDirty = true;
          lastTypeTime = millis();

          statusNotice = "[Struck out]";
          statusNoticeTime = millis();
          statusChanged = true;

          renderLinePartial(targetRow);
        }
        continue;
      }

      // Forward-Only & Flow Modes: Backspace is locked!
      if (key.ascii == 0x08 || key.keycode == 0x2A) {
        statusNotice = "[Write forward! Ctrl+Backspace to strike]";
        statusNoticeTime = millis();
        statusChanged = true;
        continue;
      }

      // Tab key -> 2 spaces
      if (key.ascii == '\t') {
        for (int i = 0; i < 2 && cursorCol < MAX_COLS; i++) {
          struckChar[cursorRow][cursorCol] = false;
          pageBuffer[cursorRow][cursorCol++] = ' ';
          currentDraftText += ' ';
        }
        pageBuffer[cursorRow][cursorCol] = '\0';
        draftDirty = true;
        lastTypeTime = millis();
        lineChanged = true;
        continue;
      }

      // Printable characters (ASCII 32 to 126)
      if (key.ascii >= 0x20 && key.ascii <= 0x7E) {
        char ch = (char)key.ascii;

        // Word Wrap: Don't break words across lines
        if (cursorCol >= MAX_COLS) {
          int lastSpace = -1;
          for (int c = cursorCol - 1; c >= 0; c--) {
            if (pageBuffer[cursorRow][c] == ' ') {
              lastSpace = c;
              break;
            }
          }

          if (lastSpace > 0 && lastSpace > (cursorCol - 12)) {
            char wrappedWord[MAX_COLS + 1];
            strcpy(wrappedWord, &pageBuffer[cursorRow][lastSpace + 1]);

            bool wrappedStruck[MAX_COLS + 1];
            int wrapLen = strlen(wrappedWord);
            for (int i = 0; i < wrapLen; i++) {
              wrappedStruck[i] = struckChar[cursorRow][lastSpace + 1 + i];
            }
            for (int i = lastSpace; i < MAX_COLS; i++) {
              struckChar[cursorRow][i] = false;
            }

            pageBuffer[cursorRow][lastSpace] = '\0';
            renderLinePartial(cursorRow);

            cursorRow++;
            cursorCol = 0;
            if (cursorRow >= MAX_ROWS) {
              scrollTwoThirds(wrappedWord);
              needFullRedraw = true;
              break;
            }

            memset(struckChar[cursorRow], 0, sizeof(struckChar[cursorRow]));
            strcpy(pageBuffer[cursorRow], wrappedWord);
            for (int i = 0; i < wrapLen; i++) {
              struckChar[cursorRow][i] = wrappedStruck[i];
            }
            cursorCol = wrapLen;
          } else {
            // Long word fallback
            renderLinePartial(cursorRow);
            cursorRow++;
            cursorCol = 0;
            if (cursorRow >= MAX_ROWS) {
              scrollTwoThirds();
              needFullRedraw = true;
              break;
            }
            memset(struckChar[cursorRow], 0, sizeof(struckChar[cursorRow]));
          }
        }

        pageBuffer[cursorRow][cursorCol] = ch;
        struckChar[cursorRow][cursorCol] = false;
        cursorCol++;
        pageBuffer[cursorRow][cursorCol] = '\0';
        currentDraftText += ch;
        draftDirty = true;
        lastTypeTime = millis();
        lineChanged = true;

        if (ch == ' ') {
          totalDraftWords = countWords(currentDraftText);
        }
      }
    } while (xQueueReceive(keyQueue, &key, 0) == pdTRUE);

    // Apply screen updates
    if (needFullRedraw) {
      redrawEntirePage();
    } else {
      if (lineChanged) {
        renderLinePartial(cursorRow);
      }
      if (totalDraftWords != prevWordCount || statusChanged) {
        renderStatusBarPartial();
      }
    }
  }

  // Flow Mode Sprint periodic countdown check (updates every 60s without keystrokes)
  static unsigned long lastSprintCheck = 0;
  if ((currentWritingMode == MODE_FLOW_15 || currentWritingMode == MODE_FLOW_25) && flowSprintActive) {
    if (millis() - lastSprintCheck > 60000) {
      lastSprintCheck = millis();
      renderStatusBarPartial();

      if (millis() - flowSprintStartTime >= flowSprintDurationMs) {
        statusNotice = "[Sprint Complete!]";
        statusNoticeTime = millis();
        flowSprintActive = false;
        redrawEntirePage();
      }
    }
  }

  // Clear temporary status notice after 3 seconds
  if (statusNotice != nullptr && (millis() - statusNoticeTime > 3000)) {
    statusNotice = nullptr;
    if (currentMode == MODE_TYPING) {
      renderStatusBarPartial();
    }
  }
}
