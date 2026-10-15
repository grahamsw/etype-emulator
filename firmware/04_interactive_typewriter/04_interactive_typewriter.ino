/*
 * E-Type Hardware: Step 4 - Live Interactive E-Ink Typewriter with Control Mode & Storage
 * Hardware: ESP32-S3 (N16R8) + Waveshare 4.2" E-Paper (400x300) + USB Keyboard
 * 
 * Features:
 *   - Clean, text-first canvas inspired by the E-Type web emulator.
 *   - 11 lines of text with 33 columns for maximum writing area.
 *   - Intelligent word wrap: words are never split across lines.
 *   - 2/3 Screen Scrolling: When line 11 fills, scrolls 7 lines up, keeping 4 lines of
 *     context at top so the writer never loses orientation.
 *   - LittleFS Flash Storage: Auto-saves drafts under /draft_001.txt, etc.
 *   - Control Mode ([ESC] key):
 *       [N] New Draft
 *       [O] Open Draft
 *       [E] Export / Sync (On-Demand WiFi Web Portal at http://192.168.4.1)
 *       [D] Dump Draft to Serial (USB)
 *       [C] Clear Screen Ghosting
 *       [ESC] Return to Writing
 *   - Web Portal: Download .md / .txt files, view drafts, or "Delete from ESP" once offloaded.
 *   - Forward-only writing: Backspace blocked during drafting momentum.
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
// Application States & Buffers
// -------------------------------------------------------------------------
enum AppMode {
  MODE_TYPING,
  MODE_CONTROL_MENU,
  MODE_FILE_SELECT,
  MODE_WIFI_EXPORT
};

AppMode currentMode = MODE_TYPING;

char pageBuffer[MAX_ROWS][MAX_COLS + 1];
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
// Web Server (On-Demand WiFi Export)
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
void startWiFiExport();
void stopWiFiExport();
void redrawEntirePage();
void renderLinePartial(int row);
void renderStatusBarPartial();
void drawControlMenu();
void drawFileSelectMenu();
void drawWiFiExportScreen();
void scrollTwoThirds(const char* wrappedWord = nullptr);

// -------------------------------------------------------------------------
// Word Count Utility
// -------------------------------------------------------------------------
int countWords(const String &text) {
  int count = 0;
  bool inWord = false;
  for (unsigned int i = 0; i < text.length(); i++) {
    char ch = text[i];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      inWord = false;
    } else if (!inWord) {
      inWord = true;
      count++;
    }
  }
  return count;
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

  scanDraftFiles();

  if (draftCount == 0) {
    // Brand new typewriter setup: create draft_001.txt
    createNewDraft();
  } else {
    // Open the most recent draft
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

    if (fname.startsWith("/draft_") && fname.endsWith(".txt")) {
      strncpy(draftList[draftCount].name, fname.c_str(), sizeof(draftList[draftCount].name));
      draftList[draftCount].size = file.size();

      // Read content to count words
      String content = file.readString();
      draftList[draftCount].words = countWords(content);
      draftCount++;
    }
    file = root.openNextFile();
  }
  root.close();

  // Sort drafts alphabetically by name (draft_001, draft_002...)
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
  }
  cursorRow = 0;
  cursorCol = 0;

  if (text.length() == 0) return;

  // Split text into lines matching MAX_COLS with word-wrap
  char tempLine[MAX_COLS + 1];
  int tempLen = 0;
  memset(tempLine, 0, sizeof(tempLine));

  // Temporary sliding window of lines
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
      // Find last space to wrap
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

  // Take the last MAX_ROWS lines to display
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

  // Find next draft filename
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

  // Save progress to flash
  saveCurrentDraft();

  // Shift rows 7..10 into rows 0..3 (4 lines of context kept at top)
  for (int r = 0; r < SCROLL_KEEP_LINES; r++) {
    strncpy(pageBuffer[r], pageBuffer[r + SCROLL_LINES], MAX_COLS + 1);
  }

  // Clear rows 4..10
  for (int r = SCROLL_KEEP_LINES; r < MAX_ROWS; r++) {
    memset(pageBuffer[r], 0, sizeof(pageBuffer[r]));
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
// Screen Rendering Functions (Typing Mode)
// -------------------------------------------------------------------------
void drawStatusBar() {
  display.drawLine(MARGIN_X, STATUS_LINE_Y, display.width() - MARGIN_X, STATUS_LINE_Y, GxEPD_BLACK);

  display.setFont(&FreeMono9pt7b);
  display.setTextColor(GxEPD_BLACK);

  // Left: Active file name or status notice
  display.setCursor(MARGIN_X, STATUS_TEXT_Y);
  if (statusNotice != nullptr) {
    display.print(statusNotice);
  } else if (!keyboardConnected) {
    display.print("[Connecting keyboard...]");
  } else {
    // Show active draft name (omit leading '/')
    const char* displayName = (activeDraft[0] == '/') ? activeDraft + 1 : activeDraft;
    display.print(displayName);
  }

  // Right: Word count
  char countStr[24];
  snprintf(countStr, sizeof(countStr), "%d words", totalDraftWords);
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

    display.setFont(&FreeMono9pt7b);
    display.setTextColor(GxEPD_BLACK);

    for (int r = 0; r <= cursorRow && r < MAX_ROWS; r++) {
      int y = TEXT_START_Y + (r * LINE_HEIGHT);
      display.setCursor(MARGIN_X, y);
      display.print(pageBuffer[r]);
      if (r == cursorRow && keyboardConnected) {
        display.print("_");
      }
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
    display.setFont(&FreeMono9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(MARGIN_X, y);
    display.print(pageBuffer[row]);
    if (row == cursorRow) {
      display.print("_");
    }
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
    display.print("E-TYPE CONTROL MENU");
    display.drawLine(MARGIN_X, 42, display.width() - MARGIN_X, 42, GxEPD_BLACK);

    display.setFont(&FreeMono9pt7b);
    int y = 72;
    int dy = 26;

    display.setCursor(MARGIN_X, y);          display.print("[N]   New Draft");
    display.setCursor(MARGIN_X, y + dy);     display.print("[O]   Open / Switch Draft");
    display.setCursor(MARGIN_X, y + 2 * dy); display.print("[E]   Export Files (WiFi Web)");
    display.setCursor(MARGIN_X, y + 3 * dy); display.print("[D]   Dump Draft to Serial");
    display.setCursor(MARGIN_X, y + 4 * dy); display.print("[C]   Clear Ghosting");
    display.setCursor(MARGIN_X, y + 5 * dy); display.print("[ESC] Return to Writing");

    display.drawLine(MARGIN_X, STATUS_LINE_Y, display.width() - MARGIN_X, STATUS_LINE_Y, GxEPD_BLACK);
    display.setCursor(MARGIN_X, STATUS_TEXT_Y);
    char statusBuf[64];
    const char* dName = (activeDraft[0] == '/') ? activeDraft + 1 : activeDraft;
    snprintf(statusBuf, sizeof(statusBuf), "Active: %s (%d words)", dName, totalDraftWords);
    display.print(statusBuf);
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

void drawWiFiExportScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(MARGIN_X, 32);
    display.print("DRAFT EXPORT (WIFI)");
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
    display.print("Download .md or Delete files.");

    display.drawLine(MARGIN_X, STATUS_LINE_Y, display.width() - MARGIN_X, STATUS_LINE_Y, GxEPD_BLACK);
    display.setCursor(MARGIN_X, STATUS_TEXT_Y);
    display.print("Press [ESC] to turn off WiFi");
  } while (display.nextPage());
}

// -------------------------------------------------------------------------
// WiFi Web Server Handlers (with Delete from ESP)
// -------------------------------------------------------------------------
void handleWebRoot() {
  scanDraftFiles();

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>E-Type Drafts</title><style>";
  html += ":root { --paper: #f5f0e8; --ink: #2a2a2a; --card: #ece6dc; --border: #d4cfc7; --accent: #5a5550; --danger: #b33939; }";
  html += "* { box-sizing: border-box; margin: 0; padding: 0; }";
  html += "body { background: var(--paper); color: var(--ink); font-family: 'Courier Prime', monospace, monospace; padding: 24px 16px; line-height: 1.5; }";
  html += ".container { max-width: 680px; margin: 0 auto; }";
  html += "header { border-bottom: 2px solid var(--ink); padding-bottom: 12px; margin-bottom: 24px; display: flex; justify-content: space-between; align-items: baseline; }";
  html += "h1 { font-size: 1.4rem; letter-spacing: 0.05em; }";
  html += ".card { background: var(--card); border: 1px solid var(--border); border-radius: 6px; padding: 14px 16px; margin-bottom: 12px; display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 10px; }";
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
  html += ".footer { margin-top: 28px; text-align: center; font-size: 0.8rem; color: var(--accent); border-top: 1px solid var(--border); padding-top: 14px; }";
  html += "</style></head><body><div class='container'>";
  html += "<header><h1>E - T Y P E</h1><span>Draft Offloader</span></header>";

  if (draftCount == 0) {
    html += "<p style='padding: 24px 0; text-align: center; color: var(--accent);'>No drafts found on typewriter.</p>";
  } else {
    for (int i = 0; i < draftCount; i++) {
      const char* dName = (draftList[i].name[0] == '/') ? draftList[i].name + 1 : draftList[i].name;
      bool isActive = (strcmp(draftList[i].name, activeDraft) == 0);

      html += "<div class='card'>";
      html += "<div class='card-info'><div class='card-title'>" + String(dName) + (isActive ? " (Active)" : "") + "</div>";
      html += "<div class='card-meta'>" + String(draftList[i].words) + " words &bull; " + String(draftList[i].size) + " bytes</div></div>";
      html += "<div class='actions'>";
      html += "<a href='/view?file=" + String(dName) + "' class='btn' target='_blank'>View</a>";
      html += "<a href='/download?file=" + String(dName) + "' class='btn btn-primary' download>Download .md</a>";
      html += "<a href='/delete?file=" + String(dName) + "' class='btn btn-danger' onclick=\"return confirm('Permanently delete " + String(dName) + " from typewriter?');\">Delete from ESP</a>";
      html += "</div></div>";
    }
  }

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
  String baseName = filename.substring(1);
  if (baseName.endsWith(".txt")) {
    baseName = baseName.substring(0, baseName.length() - 4) + ".md";
  }

  server.sendHeader("Content-Disposition", "attachment; filename=\"" + baseName + "\"");
  server.streamFile(f, "text/markdown");
  f.close();
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
  server.streamFile(f, "text/plain; charset=utf-8");
  f.close();
}

void handleWebDelete() {
  if (server.hasArg("file")) {
    String filename = server.arg("file");
    if (!filename.startsWith("/")) filename = "/" + filename;

    if (LittleFS.exists(filename)) {
      LittleFS.remove(filename);
      Serial.printf("[LittleFS] Deleted file: %s\n", filename.c_str());

      // If active draft was deleted, create a fresh one
      if (filename == activeDraft) {
        createNewDraft();
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void startWiFiExport() {
  Serial.println("[WiFi] Starting Access Point 'EType-Drafts'...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("EType-Drafts");

  server.on("/", handleWebRoot);
  server.on("/download", handleWebDownload);
  server.on("/view", handleWebView);
  server.on("/delete", handleWebDelete);
  server.begin();
  wifiActive = true;
  Serial.println("[WiFi] Web server active at http://192.168.4.1");
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

void dumpActiveDraftSerial() {
  Serial.println("\n--- BEGIN E-TYPE DRAFT DUMP ---");
  Serial.printf("File: %s | Words: %d\n\n", activeDraft + 1, totalDraftWords);
  Serial.println(currentDraftText);
  Serial.println("--- END E-TYPE DRAFT DUMP ---\n");
}

// -------------------------------------------------------------------------
// Setup
// -------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[E-Type] Initializing Typewriter Hardware...");

  currentDraftText.reserve(32768);

  // Initialize LittleFS Flash Storage
  initStorage();

  // Create keystroke queue
  keyQueue = xQueueCreate(64, sizeof(KeyMessage));

  // Initialize SPI bus for E-Paper
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);

  // Initialize E-Paper Display
  display.init(115200, true, 2, false);
  display.setRotation(0);

  // Render initial document canvas
  redrawEntirePage();

  // Initialize USB Host keyboard driver
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
    if (!event.pressed) return; // Trigger on key down only

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
  // Handle web server requests while in WiFi Export Mode
  if (currentMode == MODE_WIFI_EXPORT && wifiActive) {
    server.handleClient();
  }

  // Periodic Auto-save in Typing Mode (after 2 seconds of typing pause)
  if (currentMode == MODE_TYPING && draftDirty && (millis() - lastTypeTime > 2000)) {
    saveCurrentDraft();
  }

  KeyMessage key;

  // Poll for keystroke with short timeout
  if (xQueueReceive(keyQueue, &key, pdMS_TO_TICKS(20)) == pdTRUE) {
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
      } else if (currentMode == MODE_FILE_SELECT) {
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

      // [E] Export / Sync (WiFi Web)
      if (key.ascii == 'e' || key.ascii == 'E') {
        currentMode = MODE_WIFI_EXPORT;
        startWiFiExport();
        drawWiFiExportScreen();
        return;
      }

      // [D] Dump Draft to Serial
      if (key.ascii == 'd' || key.ascii == 'D') {
        dumpActiveDraftSerial();
        statusNotice = "[Dumped to Serial]";
        statusNoticeTime = millis();
        drawControlMenu();
        return;
      }

      // [C] Clear Screen Ghosting
      if (key.ascii == 'c' || key.ascii == 'C') {
        drawControlMenu();
        return;
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
      // In WiFi export mode, only ESC exits (handled above)
      return;
    }

    // -----------------------------------------------------------------------
    // MODE_TYPING (Core Forward-Only Typewriter Engine)
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

      // Handle Enter / Return
      if (key.ascii == '\r' || key.ascii == '\n') {
        renderLinePartial(cursorRow);

        cursorRow++;
        cursorCol = 0;
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

      // Forward-only writing: Backspace does not delete
      if (key.ascii == 0x08 || key.keycode == 0x2A) {
        statusNotice = "[Write forward!]";
        statusNoticeTime = millis();
        statusChanged = true;
        continue;
      }

      // Tab key -> 2 spaces
      if (key.ascii == '\t') {
        for (int i = 0; i < 2 && cursorCol < MAX_COLS; i++) {
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

        // TRUE WORD WRAP: Don't split words across lines
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

            pageBuffer[cursorRow][lastSpace] = '\0';
            renderLinePartial(cursorRow);

            cursorRow++;
            cursorCol = 0;
            if (cursorRow >= MAX_ROWS) {
              scrollTwoThirds(wrappedWord);
              needFullRedraw = true;
              break;
            }

            strcpy(pageBuffer[cursorRow], wrappedWord);
            cursorCol = strlen(wrappedWord);
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
          }
        }

        pageBuffer[cursorRow][cursorCol++] = ch;
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

  // Clear temporary status notice after 3 seconds
  if (statusNotice != nullptr && (millis() - statusNoticeTime > 3000)) {
    statusNotice = nullptr;
    if (currentMode == MODE_TYPING) {
      renderStatusBarPartial();
    }
  }
}
