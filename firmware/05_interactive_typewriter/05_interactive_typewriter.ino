/*
 * E-Type Hardware: Step 5 - Live Interactive E-Ink Typewriter (v1.4)
 * Hardware: ESP32-S3 (N16R8) + Waveshare 4.2" E-Paper (400x300) + USB Keyboard
 * 
 * Resolves GitHub Issues:
 *   - #58: Eliminate periodic black screen flashes in Control Menu idle loop.
 *          All background timers (Flow sprint countdown, status notice clear, auto-save)
 *          are strictly gated on MODE_TYPING, and [M] mode toggling uses a fast partial update.
 *   - #59: Switch Draft Renaming (MODE_RENAME) from full-window redraws to fast partial
 *          window updates on the input line, with burst keystroke draining and Ctrl+Backspace clear.
 *   - #60: Edit Mode (MODE_EDIT) overhaul: high-contrast inverted block cursor (█) at the
 *          active column, batched multi-row partial updates (single refresh cycle across line
 *          transitions), in-place character insertion/deletion, and smooth line-boundary navigation.
 *   - Smart Power Management (FORIOT 18650 Shield):
 *       1. Continuous 8s keep-alive pulses during both active typing and short pauses (<5 min)
 *          so the battery shield never cuts power mid-thought or mid-sentence.
 *       2. Fast 800ms auto-save to LittleFS flash + persistent active draft tracking (/last_draft.txt)
 *          so the latest words are never lost.
 *       3. Automatic 5-minute idle shutdown: saves the current buffer to flash, displays
 *          "[Saved - Sleeping]" on the status bar, hibernates the e-paper panel, and enters
 *          ESP32-S3 deep sleep so the FORIOT shield powers off cleanly.
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
#include <esp_sleep.h>

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
#define MAX_COLS            33   // Monospace characters per line (11px per char)
#define MAX_ROWS            11   // Visible lines of text on 4.2" screen
#define CHAR_WIDTH          11   // FreeMono9pt7b xAdvance in pixels
#define SCROLL_KEEP_LINES   4    // Lines of context retained at top on scroll
#define SCROLL_LINES        7    // (MAX_ROWS - SCROLL_KEEP_LINES) lines to scroll up
#define LINE_HEIGHT         22   // Comfortable line height
#define TEXT_START_Y        32   // Top margin: first line baseline
#define MARGIN_X            16   // Left & right margins (368px writable width)
#define STATUS_LINE_Y       272  // Dividing line above status bar
#define STATUS_TEXT_Y       290  // Baseline of bottom status text
#define RENAME_INPUT_Y      146  // Baseline of '> filename_' input in MODE_RENAME

// -------------------------------------------------------------------------
// Power Management & Auto-Save Thresholds
// -------------------------------------------------------------------------
#define AUTO_SAVE_PAUSE_MS      800UL               // Auto-save buffer 0.8s after last keystroke
#define KEEP_ALIVE_INTERVAL_MS  8000UL              // Pulse every 8s to keep FORIOT 18650 shield awake
#define IDLE_SHUTDOWN_MS        (5UL * 60UL * 1000UL) // 5 minutes idle -> save buffer & power off

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
bool hardNewline[MAX_ROWS];
int cursorRow = 0;
int cursorCol = 0;
int maxUsedRow = 0;
bool keyboardConnected = false;

// Complete draft text buffer (holds all text in active document)
String currentDraftText = "";
String draftPrefixText = ""; // Text scrolled off top of screen prior to pageBuffer[0]
char activeDraft[32] = "/draft_001.txt";
int totalDraftWords = 0;
bool draftDirty = false;
bool statusBarDirty = false;
unsigned long lastTypeTime = 0;
unsigned long lastKeyActivityTime = 0;

// Battery Keep-Alive (prevents FORIOT 18650 / MH-CD42 auto-shutoff until 5-min timeout)
bool batteryKeepAlive = true;
unsigned long lastKeepAlivePulse = 0;

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
void saveLastDraftName();
void createNewDraft();
void loadDraft(const char* filename);
void scanDraftFiles();
void loadConfig();
void saveConfig(const String &ssid, const String &pass, const String &url);
void syncToGoogleDrive();
void startWiFiExport();
void stopWiFiExport();
void saveAndPowerOff();
void redrawEntirePage();
void renderRowsPartial(int minRow, int maxRow);
void renderLinePartial(int row);
void renderStatusBarPartial();
void drawControlMenu();
void renderControlMenuModePartial();
void drawFileSelectMenu();
void drawRenameScreen();
void renderRenameInputPartial();
void drawWiFiExportScreen();
void scrollTwoThirds(const char* wrappedWord = nullptr);
void syncDraftTextFromPageBuffer();
String serializeRowRange(int startRow, int endRow, bool includeTrailingSeparator);
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
// Page Buffer Serialization (Keeps Edit Mode changes synced with Draft Text)
// -------------------------------------------------------------------------
void recomputeMaxUsedRow() {
  int highest = cursorRow;
  for (int r = MAX_ROWS - 1; r >= 0; r--) {
    if (strlen(pageBuffer[r]) > 0 || hardNewline[r]) {
      if (r > highest) highest = r;
      break;
    }
  }
  maxUsedRow = highest;
}

String serializeRowRange(int startRow, int endRow, bool includeTrailingSeparator) {
  String out = "";
  for (int r = startRow; r <= endRow && r < MAX_ROWS; r++) {
    int len = strlen(pageBuffer[r]);
    int c = 0;
    while (c < len) {
      if (struckChar[r][c]) {
        out += "~~";
        while (c < len && struckChar[r][c]) {
          out += pageBuffer[r][c];
          c++;
        }
        out += "~~";
      } else {
        out += pageBuffer[r][c];
        c++;
      }
    }

    if (r < endRow || includeTrailingSeparator) {
      if (hardNewline[r]) {
        out += "\n";
      } else if (len > 0 && pageBuffer[r][len - 1] != ' ') {
        out += " ";
      }
    }
  }
  return out;
}

void syncDraftTextFromPageBuffer() {
  recomputeMaxUsedRow();
  currentDraftText = draftPrefixText + serializeRowRange(0, maxUsedRow, false);
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
void saveLastDraftName() {
  if (strlen(activeDraft) == 0) return;
  File f = LittleFS.open("/last_draft.txt", "w");
  if (f) {
    f.print(activeDraft);
    f.close();
  }
}

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
    String lastDraft = "";
    if (LittleFS.exists("/last_draft.txt")) {
      File f = LittleFS.open("/last_draft.txt", "r");
      if (f) {
        lastDraft = f.readString();
        lastDraft.trim();
        f.close();
      }
    }
    if (lastDraft.length() > 0 && LittleFS.exists(lastDraft)) {
      loadDraft(lastDraft.c_str());
    } else {
      loadDraft(draftList[0].name);
    }
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
      if (fname != "/config.txt" && fname != "/last_draft.txt") {
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
    hardNewline[r] = false;
  }
  cursorRow = 0;
  cursorCol = 0;
  maxUsedRow = 0;
  draftPrefixText = "";

  if (text.length() == 0) return;

  char tempLine[MAX_COLS + 1];
  int tempLen = 0;
  memset(tempLine, 0, sizeof(tempLine));

  static char allLines[512][MAX_COLS + 1];
  static bool allHardNewline[512];
  int totalLines = 0;

  for (unsigned int i = 0; i < text.length() && totalLines < 510; i++) {
    char ch = text[i];
    if (ch == '\r') continue;

    if (ch == '\n') {
      tempLine[tempLen] = '\0';
      strncpy(allLines[totalLines], tempLine, MAX_COLS + 1);
      allHardNewline[totalLines] = true;
      totalLines++;
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
        strncpy(allLines[totalLines], tempLine, MAX_COLS + 1);
        allHardNewline[totalLines] = false;
        totalLines++;

        strcpy(tempLine, wrapped);
        tempLen = strlen(wrapped);
      } else {
        tempLine[tempLen] = '\0';
        strncpy(allLines[totalLines], tempLine, MAX_COLS + 1);
        allHardNewline[totalLines] = false;
        totalLines++;
        tempLen = 0;
        memset(tempLine, 0, sizeof(tempLine));
      }
    }
    tempLine[tempLen++] = ch;
  }

  tempLine[tempLen] = '\0';
  strncpy(allLines[totalLines], tempLine, MAX_COLS + 1);
  allHardNewline[totalLines] = false;
  totalLines++;

  int startLine = (totalLines > MAX_ROWS) ? (totalLines - MAX_ROWS) : 0;

  // Preserve scrolled-off lines in draftPrefixText so Edit Mode never loses earlier text
  for (int i = 0; i < startLine; i++) {
    draftPrefixText += allLines[i];
    if (allHardNewline[i]) {
      draftPrefixText += "\n";
    } else if (strlen(allLines[i]) > 0 && allLines[i][strlen(allLines[i]) - 1] != ' ') {
      draftPrefixText += " ";
    }
  }

  cursorRow = 0;
  for (int i = startLine; i < totalLines; i++) {
    strncpy(pageBuffer[cursorRow], allLines[i], MAX_COLS + 1);
    hardNewline[cursorRow] = allHardNewline[i];
    if (i == totalLines - 1) {
      cursorCol = strlen(pageBuffer[cursorRow]);
    } else {
      cursorRow++;
    }
  }
  maxUsedRow = cursorRow;
}

void loadDraft(const char* filename) {
  strncpy(activeDraft, filename, sizeof(activeDraft));
  saveLastDraftName();
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
  statusBarDirty = false;
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
  draftPrefixText = "";
  totalDraftWords = 0;
  draftDirty = false;
  statusBarDirty = false;

  for (int r = 0; r < MAX_ROWS; r++) {
    memset(pageBuffer[r], 0, sizeof(pageBuffer[r]));
    memset(struckChar[r], 0, sizeof(struckChar[r]));
    hardNewline[r] = false;
  }
  cursorRow = 0;
  cursorCol = 0;
  maxUsedRow = 0;

  saveCurrentDraft();
  saveLastDraftName();
  Serial.printf("[LittleFS] Created fresh new draft: %s\n", activeDraft);
}

// -------------------------------------------------------------------------
// 2/3 Screen Scrolling Architecture
// -------------------------------------------------------------------------
void scrollTwoThirds(const char* wrappedWord) {
  Serial.println("[Typewriter] Scrolling 2/3 screen (retaining 4 lines)...");

  // Append the 7 lines scrolling off the top (rows 0..6) to draftPrefixText
  draftPrefixText += serializeRowRange(0, SCROLL_LINES - 1, true);

  saveCurrentDraft();

  // Shift rows 7..10 into rows 0..3 (4 lines of context kept at top)
  for (int r = 0; r < SCROLL_KEEP_LINES; r++) {
    strncpy(pageBuffer[r], pageBuffer[r + SCROLL_LINES], MAX_COLS + 1);
    memcpy(struckChar[r], struckChar[r + SCROLL_LINES], MAX_COLS + 1);
    hardNewline[r] = hardNewline[r + SCROLL_LINES];
  }

  // Clear rows 4..10
  for (int r = SCROLL_KEEP_LINES; r < MAX_ROWS; r++) {
    memset(pageBuffer[r], 0, sizeof(pageBuffer[r]));
    memset(struckChar[r], 0, sizeof(struckChar[r]));
    hardNewline[r] = false;
  }

  cursorRow = SCROLL_KEEP_LINES; // line 4
  if (wrappedWord != nullptr && strlen(wrappedWord) > 0) {
    strncpy(pageBuffer[cursorRow], wrappedWord, MAX_COLS);
    cursorCol = strlen(wrappedWord);
  } else {
    cursorCol = 0;
  }
  maxUsedRow = cursorRow;
}

// -------------------------------------------------------------------------
// Screen Rendering & Visual Strikethrough Engine (Issues #58, #59, #60)
// -------------------------------------------------------------------------
void drawPageLine(int row, int y) {
  display.setFont(&FreeMono9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(MARGIN_X, y);
  display.print(pageBuffer[row]);

  int lineLen = strlen(pageBuffer[row]);

  // Draw 2-pixel black strikeout lines for any characters marked in struckChar
  int col = 0;
  while (col < lineLen) {
    if (struckChar[row][col]) {
      int startCol = col;
      while (col < lineLen && struckChar[row][col]) {
        col++;
      }
      int endCol = col;
      int x1 = MARGIN_X + (startCol * CHAR_WIDTH);
      int x2 = MARGIN_X + (endCol * CHAR_WIDTH) - 2;
      int yStrike = y - 5;
      display.drawLine(x1, yStrike, x2, yStrike, GxEPD_BLACK);
      display.drawLine(x1, yStrike + 1, x2, yStrike + 1, GxEPD_BLACK);
    } else {
      col++;
    }
  }

  // Draw Cursor at active insertion column (Issue #60)
  if (row == cursorRow && keyboardConnected) {
    int curX = MARGIN_X + (cursorCol * CHAR_WIDTH);
    if (currentWritingMode == MODE_EDIT) {
      // High-contrast inverted block cursor (█) at exact active column
      display.fillRect(curX, y - 14, CHAR_WIDTH, 19, GxEPD_BLACK);
      if (cursorCol < lineLen && pageBuffer[row][cursorCol] > ' ') {
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(curX, y);
        display.write(pageBuffer[row][cursorCol]);
        if (struckChar[row][cursorCol]) {
          int yStrike = y - 5;
          display.drawLine(curX, yStrike, curX + CHAR_WIDTH - 2, yStrike, GxEPD_WHITE);
          display.drawLine(curX, yStrike + 1, curX + CHAR_WIDTH - 2, yStrike + 1, GxEPD_WHITE);
        }
        display.setTextColor(GxEPD_BLACK);
      }
    } else {
      display.setCursor(curX, y);
      display.print("_");
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
  recomputeMaxUsedRow();
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    for (int r = 0; r <= maxUsedRow && r < MAX_ROWS; r++) {
      int y = TEXT_START_Y + (r * LINE_HEIGHT);
      drawPageLine(r, y);
    }

    drawStatusBar();
  } while (display.nextPage());
  statusBarDirty = false;
}

void renderRowsPartial(int minRow, int maxRow) {
  if (minRow < 0) minRow = 0;
  if (maxRow >= MAX_ROWS) maxRow = MAX_ROWS - 1;
  if (minRow > maxRow) return;

  int windowY = (TEXT_START_Y - 16) + (minRow * LINE_HEIGHT);
  int windowH = (maxRow - minRow + 1) * LINE_HEIGHT;
  int windowW = display.width() - (MARGIN_X * 2);

  display.setPartialWindow(MARGIN_X, windowY, windowW, windowH);
  display.firstPage();
  do {
    display.fillRect(MARGIN_X, windowY, windowW, windowH, GxEPD_WHITE);
    for (int r = minRow; r <= maxRow; r++) {
      int y = TEXT_START_Y + (r * LINE_HEIGHT);
      drawPageLine(r, y);
    }
  } while (display.nextPage());
}

void renderLinePartial(int row) {
  renderRowsPartial(row, row);
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
  statusBarDirty = false;
}

// -------------------------------------------------------------------------
// Screen Rendering Functions (Menus & Export - Issues #58 & #59)
// -------------------------------------------------------------------------
void drawControlMenuModeText(int yBaseline) {
  display.setFont(&FreeMono9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(MARGIN_X, yBaseline);
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
}

void drawControlMenu() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(MARGIN_X, 32);
    display.print("E-TYPE MENU [OTA v1.4]");
    display.drawLine(MARGIN_X, 42, display.width() - MARGIN_X, 42, GxEPD_BLACK);

    display.setFont(&FreeMono9pt7b);
    int y = 74;
    int dy = 28;

    display.setCursor(MARGIN_X, y);          display.print("[N]   New Draft");
    display.setCursor(MARGIN_X, y + dy);     display.print("[O]   Open / Switch Draft");
    display.setCursor(MARGIN_X, y + 2 * dy); display.print("[R]   Rename Active Draft");
    display.setCursor(MARGIN_X, y + 3 * dy); display.print("[E]   Export & Web OTA (WiFi)");
    display.setCursor(MARGIN_X, y + 4 * dy); display.print("[G]   Sync to Google Drive");

    drawControlMenuModeText(y + 5 * dy);

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

// Fast partial window update when cycling [M] Mode inside Control Menu (Issue #58)
void renderControlMenuModePartial() {
  int yBaseline = 74 + (5 * 28); // 214
  int windowY = yBaseline - 18;
  int windowH = 26;
  int windowW = display.width() - (MARGIN_X * 2);

  display.setPartialWindow(MARGIN_X, windowY, windowW, windowH);
  display.firstPage();
  do {
    display.fillRect(MARGIN_X, windowY, windowW, windowH, GxEPD_WHITE);
    drawControlMenuModeText(yBaseline);
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

void drawRenameInputLine() {
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(MARGIN_X, RENAME_INPUT_Y);
  display.print("> ");
  display.print(renameBuffer);

  int curX = MARGIN_X + ((strlen(renameBuffer) + 2) * CHAR_WIDTH);
  display.fillRect(curX, RENAME_INPUT_Y - 14, CHAR_WIDTH - 1, 18, GxEPD_BLACK);
}

// Initial full-window setup upon entering MODE_RENAME (Issue #59)
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
    display.print("Enter new title (.txt auto-added):");

    drawRenameInputLine();

    display.setFont(&FreeMono9pt7b);
    display.drawLine(MARGIN_X, STATUS_LINE_Y, display.width() - MARGIN_X, STATUS_LINE_Y, GxEPD_BLACK);
    display.setCursor(MARGIN_X, STATUS_TEXT_Y);
    display.print("[Enter] Save  |  [ESC] Cancel");
  } while (display.nextPage());
}

// Fast partial window update on each keystroke/backspace in MODE_RENAME (Issue #59)
void renderRenameInputPartial() {
  int windowY = RENAME_INPUT_Y - 18;
  int windowH = 26;
  int windowW = display.width() - (MARGIN_X * 2);

  display.setPartialWindow(MARGIN_X, windowY, windowW, windowH);
  display.firstPage();
  do {
    display.fillRect(MARGIN_X, windowY, windowW, windowH, GxEPD_WHITE);
    drawRenameInputLine();
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
  html += "<header><h1>E - T Y P E v1.4</h1><span>Control Portal</span></header>";

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
// Battery Keep-Alive Pulse & 5-Minute Idle Save + Power-Off
// -------------------------------------------------------------------------
void pulseBatteryKeepAlive() {
  if (!batteryKeepAlive || wifiActive) return;

  // Brief ~90ms RF + CPU current pulse (~140mA) resets battery shield standby timer
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  unsigned long t0 = millis();
  volatile uint32_t acc = 0;
  while (millis() - t0 < 90) {
    for (int i = 0; i < 4000; i++) {
      acc += i;
    }
    if (uxQueueMessagesWaiting(keyQueue) > 0) break; // Abort immediately on keystroke
  }
  WiFi.mode(WIFI_OFF);
}

void saveAndPowerOff() {
  Serial.println("[Power] 5 minutes of inactivity reached. Saving buffer and powering off...");

  // 1. Flush and save the current draft buffer and active draft pointer to LittleFS
  if (currentWritingMode == MODE_EDIT) {
    syncDraftTextFromPageBuffer();
    totalDraftWords = countWords(currentDraftText);
  }
  saveCurrentDraft();
  saveLastDraftName();
  LittleFS.end();

  // 2. Show "[Saved - Sleeping]" on the e-ink screen before hibernating
  statusNotice = "[Saved - Sleeping]";
  if (currentMode == MODE_TYPING) {
    // Erase cursor on active line and update status bar cleanly
    keyboardConnected = false;
    renderLinePartial(cursorRow);
    renderStatusBarPartial();
  } else {
    keyboardConnected = false;
    currentMode = MODE_TYPING;
    redrawEntirePage();
  }

  // 3. Put e-paper controller into deep hibernate and shut down ESP32-S3
  display.hibernate();
  stopWiFiExport();
  WiFi.mode(WIFI_OFF);
  batteryKeepAlive = false;

  Serial.println("[Power] Entering deep sleep (FORIOT shield will now auto-shutoff).");
  Serial.flush();
  delay(100);
  esp_deep_sleep_start();
}

// -------------------------------------------------------------------------
// Setup
// -------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[E-Type] Initializing Typewriter Hardware (v1.4)...");

  currentDraftText.reserve(32768);
  draftPrefixText.reserve(16384);

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

  lastKeyActivityTime = millis();
  lastKeepAlivePulse = millis();
}

// -------------------------------------------------------------------------
// Main Loop: Keystrokes, Auto-Save, Battery Keep-Alive & 5-Min Shutdown
// -------------------------------------------------------------------------
void loop() {
  static unsigned long lastSprintCheck = 0;

  if (currentMode == MODE_WIFI_EXPORT && wifiActive) {
    server.handleClient();
  }

  unsigned long now = millis();
  unsigned long idleMs = now - lastKeyActivityTime;

  // 1. Fast Auto-Save: commit buffer to flash 800ms after pausing typing
  if (draftDirty && (now - lastTypeTime >= AUTO_SAVE_PAUSE_MS)) {
    saveCurrentDraft();
  }
  if (currentMode == MODE_TYPING && statusBarDirty && (now - lastTypeTime >= AUTO_SAVE_PAUSE_MS)) {
    renderStatusBarPartial();
  }

  // 2. 5-Minute Idle Shutdown: save buffer and power off after 5 minutes of no keystrokes
  if (currentMode != MODE_WIFI_EXPORT && idleMs >= IDLE_SHUTDOWN_MS) {
    saveAndPowerOff();
    return;
  }

  // 3. Battery Keep-Alive Pulse: fire every 8s during active sessions & short pauses (<5 min)
  //    (Runs between keystrokes once >400ms has elapsed so typing alone never starves the pulse)
  if (batteryKeepAlive && !wifiActive && idleMs < IDLE_SHUTDOWN_MS && idleMs > 400) {
    if (now - lastKeepAlivePulse >= KEEP_ALIVE_INTERVAL_MS && uxQueueMessagesWaiting(keyQueue) == 0) {
      lastKeepAlivePulse = now;
      pulseBatteryKeepAlive();
    }
  }

  KeyMessage key;
  TickType_t waitTicks = (currentMode == MODE_WIFI_EXPORT) ? pdMS_TO_TICKS(1) : pdMS_TO_TICKS(10);

  if (xQueueReceive(keyQueue, &key, waitTicks) == pdTRUE) {
    lastKeyActivityTime = millis();

    // -----------------------------------------------------------------------
    // Connect / Disconnect Hardware Events (Partial update only in MODE_TYPING)
    // -----------------------------------------------------------------------
    if (key.keycode == 0xFF || key.keycode == 0xFE) {
      if (currentMode == MODE_TYPING) {
        renderLinePartial(cursorRow);
        renderStatusBarPartial();
      }
      return;
    }

    // -----------------------------------------------------------------------
    // ESC KEY (ASCII 0x1B or Keycode 0x29) - Mode Switching
    // -----------------------------------------------------------------------
    if (key.ascii == 0x1B || key.keycode == 0x29) {
      if (currentMode == MODE_TYPING) {
        saveCurrentDraft();
        statusNotice = nullptr;
        currentMode = MODE_CONTROL_MENU;
        drawControlMenu();
        return;
      } else if (currentMode == MODE_CONTROL_MENU) {
        currentMode = MODE_TYPING;
        statusNotice = nullptr;
        lastSprintCheck = millis();
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
    // MODE_CONTROL_MENU Handling (Strictly Event-Driven - Issue #58)
    // -----------------------------------------------------------------------
    if (currentMode == MODE_CONTROL_MENU) {
      // [N] New Draft
      if (key.ascii == 'n' || key.ascii == 'N') {
        statusNotice = nullptr;
        createNewDraft();
        currentMode = MODE_TYPING;
        redrawEntirePage();
        return;
      }

      // [O] Open Draft
      if (key.ascii == 'o' || key.ascii == 'O') {
        statusNotice = nullptr;
        currentMode = MODE_FILE_SELECT;
        drawFileSelectMenu();
        return;
      }

      // [R] Rename Active Draft (Issue #59)
      if (key.ascii == 'r' || key.ascii == 'R') {
        statusNotice = nullptr;
        currentMode = MODE_RENAME;
        const char* dName = (activeDraft[0] == '/') ? activeDraft + 1 : activeDraft;
        strncpy(renameBuffer, dName, sizeof(renameBuffer) - 1);
        renameBuffer[sizeof(renameBuffer) - 1] = '\0';

        // Strip trailing .txt extension so user can edit title immediately
        int rLen = strlen(renameBuffer);
        if (rLen > 4 && strcmp(&renameBuffer[rLen - 4], ".txt") == 0) {
          renameBuffer[rLen - 4] = '\0';
        }
        renameCursor = strlen(renameBuffer);
        drawRenameScreen();
        return;
      }

      // [E] Export / Sync & OTA (WiFi Web)
      if (key.ascii == 'e' || key.ascii == 'E') {
        statusNotice = nullptr;
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
          lastSprintCheck = millis();
        } else if (currentWritingMode == MODE_FLOW_25) {
          flowSprintDurationMs = 25 * 60 * 1000;
          flowSprintStartTime = millis();
          flowSprintStartWords = totalDraftWords;
          flowSprintActive = true;
          lastSprintCheck = millis();
        } else {
          flowSprintActive = false;
        }
        // Fast partial update of only the [M] Mode line (no full screen flash!)
        renderControlMenuModePartial();
        return;
      }

      return;
    }

    // -----------------------------------------------------------------------
    // MODE_RENAME Handling (Partial Window Updates & Burst Draining - Issue #59)
    // -----------------------------------------------------------------------
    if (currentMode == MODE_RENAME) {
      bool renameInputChanged = false;

      do {
        // ESC inside burst: cancel rename and return to Control Menu
        if (key.ascii == 0x1B || key.keycode == 0x29) {
          currentMode = MODE_CONTROL_MENU;
          drawControlMenu();
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

            if (strcmp(activeDraft, newName.c_str()) == 0) {
              statusNotice = "[Name Unchanged]";
            } else if (LittleFS.rename(activeDraft, newName.c_str())) {
              strncpy(activeDraft, newName.c_str(), sizeof(activeDraft));
              saveLastDraftName();
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

        // Ctrl+Backspace or Alt+Backspace in rename: clear entire input line at once
        bool isCtrlOrAlt = ((key.modifiers & 0x11) != 0) || ((key.modifiers & 0x44) != 0);
        if ((key.keycode == 0x2A && isCtrlOrAlt) || key.ascii == 0x17) {
          if (renameCursor > 0) {
            renameCursor = 0;
            renameBuffer[0] = '\0';
            renameInputChanged = true;
          }
          continue;
        }

        // Backspace in rename
        if (key.ascii == 0x08 || key.keycode == 0x2A) {
          if (renameCursor > 0) {
            renameCursor--;
            renameBuffer[renameCursor] = '\0';
            renameInputChanged = true;
          }
          continue;
        }

        // Printable characters for filename
        if (key.ascii >= 0x20 && key.ascii <= 0x7E && renameCursor < 24) {
          char ch = (char)key.ascii;
          if (ch == ' ') ch = '_'; // Convert spaces to underscores automatically
          if (isalnum(ch) || ch == '_' || ch == '-' || ch == '.') {
            renameBuffer[renameCursor++] = ch;
            renameBuffer[renameCursor] = '\0';
            renameInputChanged = true;
          }
        }
      } while (xQueueReceive(keyQueue, &key, 0) == pdTRUE);

      if (renameInputChanged) {
        renderRenameInputPartial();
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
    // MODE_TYPING (Includes Edit Mode Improvements - Issue #60)
    // -----------------------------------------------------------------------
    bool needFullRedraw = false;
    bool statusChanged = false;
    bool editBufferModified = false;
    int dirtyMinRow = MAX_ROWS;
    int dirtyMaxRow = -1;
    int prevWordCount = totalDraftWords;

    auto markRowDirty = [&](int r) {
      if (r < 0) r = 0;
      if (r >= MAX_ROWS) r = MAX_ROWS - 1;
      if (r < dirtyMinRow) dirtyMinRow = r;
      if (r > dirtyMaxRow) dirtyMaxRow = r;
    };

    // Drain burst keystrokes to ensure zero typing/editing latency
    do {
      // ESC inside burst: flush and open Control Menu
      if (key.ascii == 0x1B || key.keycode == 0x29) {
        if (editBufferModified) {
          syncDraftTextFromPageBuffer();
          totalDraftWords = countWords(currentDraftText);
        }
        saveCurrentDraft();
        statusNotice = nullptr;
        currentMode = MODE_CONTROL_MENU;
        drawControlMenu();
        return;
      }

      // F5 Key (0x3E): Force full refresh to clear ghosting on demand
      if (key.keycode == 0x3E) {
        statusNotice = "[Ghosting Cleared]";
        statusNoticeTime = millis();
        needFullRedraw = true;
        break;
      }

      // ---------------------------------------------------------------------
      // EDIT MODE: Snappy Deletions, Mid-Line Editing & Boundary Navigation
      // ---------------------------------------------------------------------
      if (currentWritingMode == MODE_EDIT) {
        recomputeMaxUsedRow();

        // Backspace (0x08 / 0x2A) - ignoring Ctrl/Alt+Backspace which is Strikethrough below
        bool isCtrlOrAlt = ((key.modifiers & 0x11) != 0) || ((key.modifiers & 0x44) != 0);
        if ((key.ascii == 0x08 || key.keycode == 0x2A) && !isCtrlOrAlt) {
          int len = strlen(pageBuffer[cursorRow]);
          if (cursorCol > 0) {
            // Shift remaining characters in line left by 1
            for (int c = cursorCol - 1; c < len; c++) {
              pageBuffer[cursorRow][c] = pageBuffer[cursorRow][c + 1];
              struckChar[cursorRow][c] = struckChar[cursorRow][c + 1];
            }
            cursorCol--;
            markRowDirty(cursorRow);
            editBufferModified = true;
            draftDirty = true;
            lastTypeTime = millis();
          } else if (cursorRow > 0) {
            // Line boundary Backspace wrap: move to previous line and join/shift
            int prevRow = cursorRow - 1;
            int prevLen = strlen(pageBuffer[prevRow]);
            hardNewline[prevRow] = false;

            if (len == 0) {
              // Current line is empty: shift lower lines up and backspace into prevRow
              for (int r = cursorRow; r < MAX_ROWS - 1; r++) {
                strncpy(pageBuffer[r], pageBuffer[r + 1], MAX_COLS + 1);
                memcpy(struckChar[r], struckChar[r + 1], MAX_COLS + 1);
                hardNewline[r] = hardNewline[r + 1];
              }
              memset(pageBuffer[MAX_ROWS - 1], 0, sizeof(pageBuffer[MAX_ROWS - 1]));
              memset(struckChar[MAX_ROWS - 1], 0, sizeof(struckChar[MAX_ROWS - 1]));
              hardNewline[MAX_ROWS - 1] = false;

              cursorRow = prevRow;
              if (prevLen > 0) {
                cursorCol = prevLen - 1;
                pageBuffer[cursorRow][cursorCol] = '\0';
                struckChar[cursorRow][cursorCol] = false;
              } else {
                cursorCol = 0;
              }
              markRowDirty(prevRow);
              markRowDirty(maxUsedRow);
            } else if (prevLen + len <= MAX_COLS) {
              // Join current line onto previous line
              for (int c = 0; c <= len; c++) {
                pageBuffer[prevRow][prevLen + c] = pageBuffer[cursorRow][c];
                struckChar[prevRow][prevLen + c] = struckChar[cursorRow][c];
              }
              hardNewline[prevRow] = hardNewline[cursorRow];

              for (int r = cursorRow; r < MAX_ROWS - 1; r++) {
                strncpy(pageBuffer[r], pageBuffer[r + 1], MAX_COLS + 1);
                memcpy(struckChar[r], struckChar[r + 1], MAX_COLS + 1);
                hardNewline[r] = hardNewline[r + 1];
              }
              memset(pageBuffer[MAX_ROWS - 1], 0, sizeof(pageBuffer[MAX_ROWS - 1]));
              memset(struckChar[MAX_ROWS - 1], 0, sizeof(struckChar[MAX_ROWS - 1]));
              hardNewline[MAX_ROWS - 1] = false;

              cursorRow = prevRow;
              cursorCol = prevLen;
              markRowDirty(prevRow);
              markRowDirty(maxUsedRow);
            } else {
              // Previous line cannot fit whole current line: wrap cursor to end of prevRow
              markRowDirty(cursorRow);
              cursorRow = prevRow;
              if (prevLen > 0) {
                cursorCol = prevLen - 1;
                pageBuffer[cursorRow][cursorCol] = '\0';
                struckChar[cursorRow][cursorCol] = false;
              } else {
                cursorCol = 0;
              }
              markRowDirty(cursorRow);
            }
            editBufferModified = true;
            draftDirty = true;
            lastTypeTime = millis();
          }
          continue;
        }

        // Delete key (0x7F or Keycode 0x4C)
        if (key.ascii == 0x7F || key.keycode == 0x4C) {
          int len = strlen(pageBuffer[cursorRow]);
          if (cursorCol < len) {
            for (int c = cursorCol; c < len; c++) {
              pageBuffer[cursorRow][c] = pageBuffer[cursorRow][c + 1];
              struckChar[cursorRow][c] = struckChar[cursorRow][c + 1];
            }
            markRowDirty(cursorRow);
            editBufferModified = true;
            draftDirty = true;
            lastTypeTime = millis();
          } else if (cursorRow < maxUsedRow) {
            // At end of line: pull next line up if it fits
            int nextLen = strlen(pageBuffer[cursorRow + 1]);
            if (len + nextLen <= MAX_COLS) {
              for (int c = 0; c <= nextLen; c++) {
                pageBuffer[cursorRow][len + c] = pageBuffer[cursorRow + 1][c];
                struckChar[cursorRow][len + c] = struckChar[cursorRow + 1][c];
              }
              hardNewline[cursorRow] = hardNewline[cursorRow + 1];
              for (int r = cursorRow + 1; r < MAX_ROWS - 1; r++) {
                strncpy(pageBuffer[r], pageBuffer[r + 1], MAX_COLS + 1);
                memcpy(struckChar[r], struckChar[r + 1], MAX_COLS + 1);
                hardNewline[r] = hardNewline[r + 1];
              }
              memset(pageBuffer[MAX_ROWS - 1], 0, sizeof(pageBuffer[MAX_ROWS - 1]));
              memset(struckChar[MAX_ROWS - 1], 0, sizeof(struckChar[MAX_ROWS - 1]));
              hardNewline[MAX_ROWS - 1] = false;
              markRowDirty(cursorRow);
              markRowDirty(maxUsedRow);
              editBufferModified = true;
              draftDirty = true;
              lastTypeTime = millis();
            }
          }
          continue;
        }

        // Arrow Left (0x50) - wraps smoothly to end of previous line at col 0
        if (key.keycode == 0x50) {
          if (cursorCol > 0) {
            cursorCol--;
            markRowDirty(cursorRow);
          } else if (cursorRow > 0) {
            markRowDirty(cursorRow);
            cursorRow--;
            cursorCol = strlen(pageBuffer[cursorRow]);
            markRowDirty(cursorRow);
          }
          continue;
        }

        // Arrow Right (0x4F) - wraps smoothly to start of next line at line end
        if (key.keycode == 0x4F) {
          int len = strlen(pageBuffer[cursorRow]);
          if (cursorCol < len) {
            cursorCol++;
            markRowDirty(cursorRow);
          } else if (cursorRow < maxUsedRow) {
            markRowDirty(cursorRow);
            cursorRow++;
            cursorCol = 0;
            markRowDirty(cursorRow);
          }
          continue;
        }

        // Arrow Up (0x52) - single batched partial update across both rows
        if (key.keycode == 0x52) {
          if (cursorRow > 0) {
            markRowDirty(cursorRow);
            cursorRow--;
            int len = strlen(pageBuffer[cursorRow]);
            if (cursorCol > len) cursorCol = len;
            markRowDirty(cursorRow);
          } else if (cursorCol > 0) {
            cursorCol = 0;
            markRowDirty(cursorRow);
          }
          continue;
        }

        // Arrow Down (0x51) - navigates across blank lines up to maxUsedRow
        if (key.keycode == 0x51) {
          if (cursorRow < maxUsedRow) {
            markRowDirty(cursorRow);
            cursorRow++;
            int len = strlen(pageBuffer[cursorRow]);
            if (cursorCol > len) cursorCol = len;
            markRowDirty(cursorRow);
          } else {
            int len = strlen(pageBuffer[cursorRow]);
            if (cursorCol < len) {
              cursorCol = len;
              markRowDirty(cursorRow);
            }
          }
          continue;
        }

        // Home (0x4A) & End (0x4D)
        if (key.keycode == 0x4A) {
          if (cursorCol > 0) {
            cursorCol = 0;
            markRowDirty(cursorRow);
          }
          continue;
        }
        if (key.keycode == 0x4D) {
          int len = strlen(pageBuffer[cursorRow]);
          if (cursorCol != len) {
            cursorCol = len;
            markRowDirty(cursorRow);
          }
          continue;
        }
      }

      // Handle Enter / Return
      if (key.ascii == '\r' || key.ascii == '\n') {
        hardNewline[cursorRow] = true;
        markRowDirty(cursorRow);

        cursorRow++;
        cursorCol = 0;
        if (cursorRow < MAX_ROWS) {
          if (currentWritingMode != MODE_EDIT || cursorRow > maxUsedRow) {
            memset(pageBuffer[cursorRow], 0, sizeof(pageBuffer[cursorRow]));
            memset(struckChar[cursorRow], 0, sizeof(struckChar[cursorRow]));
            hardNewline[cursorRow] = false;
          }
          if (cursorRow > maxUsedRow) maxUsedRow = cursorRow;
        }
        currentDraftText += "\n";
        if (currentWritingMode == MODE_EDIT) editBufferModified = true;
        draftDirty = true;
        lastTypeTime = millis();

        if (cursorRow >= MAX_ROWS) {
          scrollTwoThirds();
          needFullRedraw = true;
          break;
        } else {
          markRowDirty(cursorRow);
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
          for (int c = wStart; c < wEnd; c++) {
            struckChar[targetRow][c] = true;
          }

          char struckWordBuf[MAX_COLS + 1];
          int wordLen = wEnd - wStart;
          strncpy(struckWordBuf, &pageBuffer[targetRow][wStart], wordLen);
          struckWordBuf[wordLen] = '\0';

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

          markRowDirty(targetRow);
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
          int lineLen = strlen(pageBuffer[cursorRow]);
          if (currentWritingMode == MODE_EDIT && cursorCol < lineLen && lineLen < MAX_COLS) {
            for (int c = lineLen; c >= cursorCol; c--) {
              pageBuffer[cursorRow][c + 1] = pageBuffer[cursorRow][c];
              struckChar[cursorRow][c + 1] = struckChar[cursorRow][c];
            }
          }
          struckChar[cursorRow][cursorCol] = false;
          pageBuffer[cursorRow][cursorCol++] = ' ';
          if (cursorCol > lineLen) pageBuffer[cursorRow][cursorCol] = '\0';
          currentDraftText += ' ';
        }
        if (currentWritingMode == MODE_EDIT) editBufferModified = true;
        draftDirty = true;
        lastTypeTime = millis();
        markRowDirty(cursorRow);
        continue;
      }

      // Printable characters (ASCII 32 to 126)
      if (key.ascii >= 0x20 && key.ascii <= 0x7E) {
        char ch = (char)key.ascii;
        int lineLen = strlen(pageBuffer[cursorRow]);

        // In-place insertion when cursor is inside an existing line in Edit Mode
        if (currentWritingMode == MODE_EDIT && cursorCol < lineLen) {
          if (lineLen < MAX_COLS) {
            for (int c = lineLen; c >= cursorCol; c--) {
              pageBuffer[cursorRow][c + 1] = pageBuffer[cursorRow][c];
              struckChar[cursorRow][c + 1] = struckChar[cursorRow][c];
            }
            pageBuffer[cursorRow][cursorCol] = ch;
            struckChar[cursorRow][cursorCol] = false;
            cursorCol++;
          } else {
            // Line is already at MAX_COLS: overwrite character in place
            pageBuffer[cursorRow][cursorCol] = ch;
            struckChar[cursorRow][cursorCol] = false;
            if (cursorCol < MAX_COLS) cursorCol++;
          }
          markRowDirty(cursorRow);
          editBufferModified = true;
          draftDirty = true;
          lastTypeTime = millis();
          continue;
        }

        // Word Wrap at end of line: Don't break words across lines
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
            hardNewline[cursorRow] = false;
            markRowDirty(cursorRow);

            cursorRow++;
            cursorCol = 0;
            if (cursorRow >= MAX_ROWS) {
              scrollTwoThirds(wrappedWord);
              needFullRedraw = true;
              break;
            }
            if (cursorRow > maxUsedRow) maxUsedRow = cursorRow;

            memset(struckChar[cursorRow], 0, sizeof(struckChar[cursorRow]));
            hardNewline[cursorRow] = false;
            strcpy(pageBuffer[cursorRow], wrappedWord);
            for (int i = 0; i < wrapLen; i++) {
              struckChar[cursorRow][i] = wrappedStruck[i];
            }
            cursorCol = wrapLen;
            markRowDirty(cursorRow);
          } else {
            // Long word fallback
            hardNewline[cursorRow] = false;
            markRowDirty(cursorRow);
            cursorRow++;
            cursorCol = 0;
            if (cursorRow >= MAX_ROWS) {
              scrollTwoThirds();
              needFullRedraw = true;
              break;
            }
            if (cursorRow > maxUsedRow) maxUsedRow = cursorRow;
            memset(pageBuffer[cursorRow], 0, sizeof(pageBuffer[cursorRow]));
            memset(struckChar[cursorRow], 0, sizeof(struckChar[cursorRow]));
            hardNewline[cursorRow] = false;
            markRowDirty(cursorRow);
          }
        }

        pageBuffer[cursorRow][cursorCol] = ch;
        struckChar[cursorRow][cursorCol] = false;
        cursorCol++;
        pageBuffer[cursorRow][cursorCol] = '\0';
        currentDraftText += ch;
        if (currentWritingMode == MODE_EDIT) editBufferModified = true;
        draftDirty = true;
        lastTypeTime = millis();
        markRowDirty(cursorRow);

        if (ch == ' ') {
          totalDraftWords = countWords(currentDraftText);
        }
      }
    } while (xQueueReceive(keyQueue, &key, 0) == pdTRUE);

    if (editBufferModified) {
      syncDraftTextFromPageBuffer();
      totalDraftWords = countWords(currentDraftText);
      if (totalDraftWords != prevWordCount) {
        statusBarDirty = true;
      }
    }

    // Apply screen updates in a single batched pass
    if (needFullRedraw) {
      redrawEntirePage();
    } else {
      if (dirtyMaxRow >= dirtyMinRow) {
        renderRowsPartial(dirtyMinRow, dirtyMaxRow);
      }
      if (statusChanged || (currentWritingMode != MODE_EDIT && totalDraftWords != prevWordCount)) {
        renderStatusBarPartial();
      }
    }
  }

  // Flow Mode Sprint periodic countdown check (STRICTLY in MODE_TYPING - Issue #58)
  if (currentMode == MODE_TYPING &&
      (currentWritingMode == MODE_FLOW_15 || currentWritingMode == MODE_FLOW_25) &&
      flowSprintActive) {
    if (millis() - lastSprintCheck > 60000) {
      lastSprintCheck = millis();

      if (millis() - flowSprintStartTime >= flowSprintDurationMs) {
        statusNotice = "[Sprint Complete!]";
        statusNoticeTime = millis();
        flowSprintActive = false;
        redrawEntirePage();
      } else {
        renderStatusBarPartial();
      }
    }
  }

  // Clear temporary status notice after 3 seconds (STRICTLY in MODE_TYPING - Issue #58)
  if (currentMode == MODE_TYPING && statusNotice != nullptr && (millis() - statusNoticeTime > 3000)) {
    statusNotice = nullptr;
    renderStatusBarPartial();
  }
}
