#include "display_ui.h"
#include <SPI.h>

uint8_t DisplayUI::s_frameBuffer[(EPD_WIDTH * EPD_HEIGHT) / 8];
uint32_t DisplayUI::s_lastDirtyTime = 0;
bool DisplayUI::s_isDirty = true;
uint8_t DisplayUI::s_lastRenderedMode = 255;
uint8_t DisplayUI::s_lastLayersMask = 255;

static SPIClass s_epdSpi(FSPI);

// Compact 5x7 ASCII Font (0x20 Space to 0x7E '~')
static const uint8_t FONT_5X7[][5] PROGMEM = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 0x20 ' '
  {0x00, 0x00, 0x5F, 0x00, 0x00}, // 0x21 '!'
  {0x00, 0x07, 0x00, 0x07, 0x00}, // 0x22 '"'
  {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 0x23 '#'
  {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 0x24 '$'
  {0x23, 0x13, 0x08, 0x64, 0x62}, // 0x25 '%'
  {0x36, 0x49, 0x55, 0x22, 0x50}, // 0x26 '&'
  {0x00, 0x05, 0x03, 0x00, 0x00}, // 0x27 '''
  {0x00, 0x1C, 0x22, 0x41, 0x00}, // 0x28 '('
  {0x00, 0x41, 0x22, 0x1C, 0x00}, // 0x29 ')'
  {0x14, 0x08, 0x3E, 0x08, 0x14}, // 0x2A '*'
  {0x08, 0x08, 0x3E, 0x08, 0x08}, // 0x2B '+'
  {0x00, 0x50, 0x30, 0x00, 0x00}, // 0x2C ','
  {0x08, 0x08, 0x08, 0x08, 0x08}, // 0x2D '-'
  {0x00, 0x60, 0x60, 0x00, 0x00}, // 0x2E '.'
  {0x20, 0x10, 0x08, 0x04, 0x02}, // 0x2F '/'
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0x30 '0'
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 0x31 '1'
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 0x32 '2'
  {0x21, 0x41, 0x45, 0x4B, 0x31}, // 0x33 '3'
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 0x34 '4'
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 0x35 '5'
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 0x36 '6'
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 0x37 '7'
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 0x38 '8'
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 0x39 '9'
  {0x00, 0x36, 0x36, 0x00, 0x00}, // 0x3A ':'
  {0x00, 0x56, 0x36, 0x00, 0x00}, // 0x3B ';'
  {0x08, 0x14, 0x22, 0x41, 0x00}, // 0x3C '<'
  {0x14, 0x14, 0x14, 0x14, 0x14}, // 0x3D '='
  {0x00, 0x41, 0x22, 0x14, 0x08}, // 0x3E '>'
  {0x02, 0x01, 0x51, 0x09, 0x06}, // 0x3F '?'
  {0x32, 0x49, 0x79, 0x41, 0x3E}, // 0x40 '@'
  {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 0x41 'A'
  {0x7F, 0x49, 0x49, 0x49, 0x36}, // 0x42 'B'
  {0x3E, 0x41, 0x41, 0x41, 0x22}, // 0x43 'C'
  {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 0x44 'D'
  {0x7F, 0x49, 0x49, 0x49, 0x41}, // 0x45 'E'
  {0x7F, 0x09, 0x09, 0x09, 0x01}, // 0x46 'F'
  {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 0x47 'G'
  {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 0x48 'H'
  {0x00, 0x41, 0x7F, 0x41, 0x00}, // 0x49 'I'
  {0x20, 0x40, 0x41, 0x3F, 0x01}, // 0x4A 'J'
  {0x7F, 0x08, 0x14, 0x22, 0x41}, // 0x4B 'K'
  {0x7F, 0x40, 0x40, 0x40, 0x40}, // 0x4C 'L'
  {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 0x4D 'M'
  {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 0x4E 'N'
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 0x4F 'O'
  {0x7F, 0x09, 0x09, 0x09, 0x06}, // 0x50 'P'
  {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 0x51 'Q'
  {0x7F, 0x09, 0x19, 0x29, 0x46}, // 0x52 'R'
  {0x46, 0x49, 0x49, 0x49, 0x31}, // 0x53 'S'
  {0x01, 0x01, 0x7F, 0x01, 0x01}, // 0x54 'T'
  {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 0x55 'U'
  {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 0x56 'V'
  {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 0x57 'W'
  {0x63, 0x14, 0x08, 0x14, 0x63}, // 0x58 'X'
  {0x07, 0x08, 0x70, 0x08, 0x07}, // 0x59 'Y'
  {0x61, 0x51, 0x49, 0x45, 0x43}, // 0x5A 'Z'
  {0x00, 0x7F, 0x41, 0x41, 0x00}, // 0x5B '['
  {0x02, 0x04, 0x08, 0x10, 0x20}, // 0x5C '\'
  {0x00, 0x41, 0x41, 0x7F, 0x00}, // 0x5D ']'
  {0x04, 0x02, 0x01, 0x02, 0x04}, // 0x5E '^'
  {0x40, 0x40, 0x40, 0x40, 0x40}, // 0x5F '_'
  {0x00, 0x01, 0x02, 0x04, 0x00}, // 0x60 '`'
  {0x20, 0x54, 0x54, 0x54, 0x78}, // 0x61 'a'
  {0x7F, 0x48, 0x44, 0x44, 0x38}, // 0x62 'b'
  {0x38, 0x44, 0x44, 0x44, 0x20}, // 0x63 'c'
  {0x38, 0x44, 0x44, 0x48, 0x7F}, // 0x64 'd'
  {0x38, 0x54, 0x54, 0x54, 0x18}, // 0x65 'e'
  {0x08, 0x7E, 0x09, 0x01, 0x02}, // 0x66 'f'
  {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 0x67 'g'
  {0x7F, 0x08, 0x04, 0x04, 0x78}, // 0x68 'h'
  {0x00, 0x44, 0x7D, 0x40, 0x00}, // 0x69 'i'
  {0x20, 0x40, 0x44, 0x3D, 0x00}, // 0x6A 'j'
  {0x7F, 0x10, 0x28, 0x44, 0x00}, // 0x6B 'k'
  {0x00, 0x41, 0x7F, 0x40, 0x00}, // 0x6C 'l'
  {0x7C, 0x04, 0x18, 0x04, 0x78}, // 0x6D 'm'
  {0x7C, 0x08, 0x04, 0x04, 0x78}, // 0x6E 'n'
  {0x38, 0x44, 0x44, 0x44, 0x38}, // 0x6F 'o'
  {0x7C, 0x14, 0x14, 0x14, 0x08}, // 0x70 'p'
  {0x08, 0x14, 0x14, 0x18, 0x7C}, // 0x71 'q'
  {0x7C, 0x08, 0x04, 0x04, 0x08}, // 0x72 'r'
  {0x48, 0x54, 0x54, 0x54, 0x20}, // 0x73 's'
  {0x04, 0x3F, 0x44, 0x40, 0x20}, // 0x74 't'
  {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 0x75 'u'
  {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 0x76 'v'
  {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 0x77 'w'
  {0x44, 0x28, 0x10, 0x28, 0x44}, // 0x78 'x'
  {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 0x79 'y'
  {0x44, 0x64, 0x54, 0x4C, 0x44}, // 0x7A 'z'
  {0x00, 0x08, 0x36, 0x41, 0x00}, // 0x7B '{'
  {0x00, 0x00, 0x7F, 0x00, 0x00}, // 0x7C '|'
  {0x00, 0x41, 0x36, 0x08, 0x00}, // 0x7D '}'
  {0x08, 0x04, 0x08, 0x10, 0x08}  // 0x7E '~'
};

// ==============================================================================
// Framebuffer Primitives (200x200 1-Bit)
// ==============================================================================

void DisplayUI::clearBuffer(bool blackBg) {
  // In SSD1681 RAM: 1 = White, 0 = Black
  memset(s_frameBuffer, blackBg ? 0x00 : 0xFF, sizeof(s_frameBuffer));
}

void DisplayUI::drawPixel(int x, int y, bool black) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
  uint16_t byteIdx = (y * (EPD_WIDTH / 8)) + (x / 8);
  uint8_t bitMask = 0x80 >> (x & 7);
  if (black) {
    s_frameBuffer[byteIdx] &= ~bitMask; // 0 = Black ink
  } else {
    s_frameBuffer[byteIdx] |= bitMask;  // 1 = White paper
  }
}

void DisplayUI::drawLine(int x0, int y0, int x1, int y1, bool black) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    drawPixel(x0, y0, black);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void DisplayUI::drawRect(int x, int y, int w, int h, bool black) {
  drawLine(x, y, x + w - 1, y, black);
  drawLine(x, y + h - 1, x + w - 1, y + h - 1, black);
  drawLine(x, y, x, y + h - 1, black);
  drawLine(x + w - 1, y, x + w - 1, y + h - 1, black);
}

void DisplayUI::fillRect(int x, int y, int w, int h, bool black) {
  for (int j = y; j < y + h; j++) {
    for (int i = x; i < x + w; i++) {
      drawPixel(i, j, black);
    }
  }
}

void DisplayUI::drawCircle(int cx, int cy, int r, bool black) {
  int x = -r, y = 0, err = 2 - 2 * r;
  do {
    drawPixel(cx - x, cy + y, black);
    drawPixel(cx - y, cy - x, black);
    drawPixel(cx + x, cy - y, black);
    drawPixel(cx + y, cy + x, black);
    r = err;
    if (r <= y) err += ++y * 2 + 1;
    if (r > x || err > y) err += ++x * 2 + 1;
  } while (x < 0);
}

void DisplayUI::fillCircle(int cx, int cy, int r, bool black) {
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy <= r * r) {
        drawPixel(cx + dx, cy + dy, black);
      }
    }
  }
}

void DisplayUI::drawChar(int x, int y, char c, bool black, uint8_t scale) {
  if (c < 0x20 || c > 0x7E) c = '?';
  uint8_t idx = c - 0x20;
  for (int col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(&FONT_5X7[idx][col]);
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        if (scale == 1) {
          drawPixel(x + col, y + row, black);
        } else {
          fillRect(x + col * scale, y + row * scale, scale, scale, black);
        }
      }
    }
  }
}

void DisplayUI::drawText(int x, int y, const char* str, bool black, uint8_t scale) {
  if (!str) return;
  int curX = x;
  while (*str) {
    drawChar(curX, y, *str, black, scale);
    curX += 6 * scale;
    str++;
  }
}

// ==============================================================================
// Non-Blocking Waveshare 1.54" SPI Driver (SSD1681 / SSD1608)
// ==============================================================================

void DisplayUI::epdSendCommand(uint8_t cmd) {
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_CS, LOW);
  s_epdSpi.transfer(cmd);
  digitalWrite(PIN_EPD_CS, HIGH);
}

void DisplayUI::epdSendData(uint8_t data) {
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  s_epdSpi.transfer(data);
  digitalWrite(PIN_EPD_CS, HIGH);
}

void DisplayUI::init() {
  pinMode(PIN_EPD_CS, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_BUSY, INPUT);

  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_RST, HIGH);

  // Initialize hardware SPI on pins SCK=12, MOSI=11
  s_epdSpi.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
  s_epdSpi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  // Hardware Reset Pulse (brief, during boot setup only)
  digitalWrite(PIN_EPD_RST, LOW);
  delay(10);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(10);

  // SSD1681 / 1.54" V2 Initialization Sequence
  epdSendCommand(0x12); // SWRESET
  delay(10);

  epdSendCommand(0x01); // Driver output control (200x200)
  epdSendData(0xC7);    // 199
  epdSendData(0x00);
  epdSendData(0x00);

  epdSendCommand(0x11); // Data entry mode
  epdSendData(0x03);    // X increment, Y increment

  epdSendCommand(0x44); // Set RAM X start/end
  epdSendData(0x00);
  epdSendData(0x18);    // 24 * 8 = 192..199 (25 bytes wide)

  epdSendCommand(0x45); // Set RAM Y start/end
  epdSendData(0x00);
  epdSendData(0x00);
  epdSendData(0xC7);    // 199
  epdSendData(0x00);

  epdSendCommand(0x3C); // Border Waveform
  epdSendData(0x05);

  clearBuffer(false);
  s_isDirty = true;
  s_lastDirtyTime = millis();
}

void DisplayUI::pushBufferToEPaper(bool fullRefresh) {
  // Never block real-time MIDI if the E-Paper panel is still busy refreshing
  if (digitalRead(PIN_EPD_BUSY) == HIGH) {
    return;
  }

  // Set RAM X/Y address counters to 0
  epdSendCommand(0x4E);
  epdSendData(0x00);
  epdSendCommand(0x4F);
  epdSendData(0x00);
  epdSendData(0x00);

  // Write 5,000-byte framebuffer to Black/White RAM (0x24)
  epdSendCommand(0x24);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  s_epdSpi.writeBytes(s_frameBuffer, sizeof(s_frameBuffer));
  digitalWrite(PIN_EPD_CS, HIGH);

#if EPD_3COLOR_MODE
  // For 3-color panels, clear the Red RAM plane (0x26)
  epdSendCommand(0x26);
  for (size_t i = 0; i < sizeof(s_frameBuffer); i++) {
    epdSendData(0x00);
  }
#endif

  // Trigger Display Update Control 2 (0x22) & Master Activation (0x20)
  // 0xFF = Fast Partial Update, 0xF7 = Standard Full Refresh
  epdSendCommand(0x22);
  epdSendData(fullRefresh ? 0xF7 : 0xFF);
  epdSendCommand(0x20); // Master Activation (non-blocking; panel pulls BUSY high while updating)
}

// ==============================================================================
// High-Level UI Update & Layout Composition
// ==============================================================================

void DisplayUI::markDirty() {
  s_isDirty = true;
  s_lastDirtyTime = millis();
}

void DisplayUI::update(
  const MidiEngine* currentEngine,
  uint8_t modeIndex,
  uint8_t activeLayersMask,
  const char* deviceName,
  const EngineVisualizer* visualizer,
  const EngineTuring* turing
) {
  if (!currentEngine) return;

  uint32_t now = millis();
  bool modeChanged = (modeIndex != s_lastRenderedMode) || (activeLayersMask != s_lastLayersMask);

  if (modeChanged) {
    s_isDirty = true;
    s_lastDirtyTime = now - 300; // Render immediately on screen or layer change
    s_lastRenderedMode = modeIndex;
    s_lastLayersMask = activeLayersMask;
  }

  // Debounced refresh: wait 220ms after last knob turn before refreshing E-Paper
  if (s_isDirty && (now - s_lastDirtyTime >= 220)) {
    composeFrame(currentEngine, modeIndex, activeLayersMask, deviceName, visualizer, turing);
    pushBufferToEPaper(modeChanged);
    renderSerialHUD(currentEngine, modeIndex, activeLayersMask, deviceName);
    s_isDirty = false;
  }
}

void DisplayUI::composeFrame(
  const MidiEngine* currentEngine,
  uint8_t modeIndex,
  uint8_t activeLayersMask,
  const char* deviceName,
  const EngineVisualizer* visualizer,
  const EngineTuring* turing
) {
  bool darkTheme = (modeIndex == MODE_VISUALIZER && visualizer && visualizer->isInverted());
  bool ink = !darkTheme; // true = draw black pixels on white bg; false = draw white pixels on black bg

  clearBuffer(darkTheme);
  char headerTitle[32];
  if (modeIndex >= MODE_LOFI && modeIndex <= MODE_PHYSICS) {
    snprintf(headerTitle, sizeof(headerTitle), "[%s] %s",
             currentEngine->isLayerActive() ? "ON" : "OFF",
             currentEngine->getName());
  } else {
    strncpy(headerTitle, currentEngine->getName(), sizeof(headerTitle) - 1);
    headerTitle[sizeof(headerTitle) - 1] = '\0';
  }
  drawHeader(headerTitle, currentEngine->getBpm(), activeLayersMask, deviceName, ink);
  drawCenterGraphic(currentEngine, modeIndex, visualizer, turing, ink);
  drawFooterParameters(currentEngine, ink);
}

void DisplayUI::drawHeader(
  const char* modeName,
  uint32_t bpm,
  uint8_t activeLayersMask,
  const char* deviceName,
  bool ink
) {
  drawText(4, 4, modeName, ink, 1);

  // Draw 5 Sound Layer Status Badges in top-right corner: [L][T][M][U][P]
  const char badgeChars[5] = {'L', 'T', 'M', 'U', 'P'};
  for (int i = 0; i < 5; i++) {
    int bx = 138 + i * 12;
    bool layerOn = (activeLayersMask & (1 << i)) != 0;
    if (layerOn) {
      fillRect(bx, 2, 10, 11, ink);
      drawChar(bx + 3, 4, badgeChars[i], !ink, 1);
    } else {
      drawRect(bx, 2, 10, 11, ink);
      drawChar(bx + 3, 4, badgeChars[i], ink, 1);
    }
  }

  // Sub-header: Connected USB / BLE Device & BPM
  char subBar[36];
  snprintf(subBar, sizeof(subBar), "DEV:%-14s %3dBPM N:%d", deviceName, bpm, MidiIO::getLastActiveNote());
  drawText(4, 17, subBar, ink, 1);
  drawLine(0, 27, EPD_WIDTH - 1, 27, ink);
}

void DisplayUI::drawCenterGraphic(
  const MidiEngine* currentEngine,
  uint8_t modeIndex,
  const EngineVisualizer* visualizer,
  const EngineTuring* turing,
  bool ink
) {
  switch (modeIndex) {
    case MODE_BYPASS: {
      // Signal Flow Diagram + Velocity Bar
      drawRect(12, 55, 46, 26, ink);
      drawText(20, 65, "USB/BLE", ink, 1);
      drawLine(58, 68, 78, 68, ink);

      drawRect(78, 50, 44, 36, ink);
      drawText(86, 60, "BYPASS", ink, 1);
      drawText(86, 70, "ENGINE", ink, 1);
      drawLine(122, 68, 142, 68, ink);

      drawRect(142, 55, 46, 26, ink);
      drawText(150, 65, "FM-1 OUT", ink, 1);

      // Velocity Activity Meter
      uint8_t velBar = map(MidiIO::getLastActiveVelocity(), 0, 127, 0, 160);
      drawRect(20, 105, 160, 10, ink);
      fillRect(20, 105, velBar, 10, ink);
      drawText(20, 95, "LIVE VELOCITY MONITOR:", ink, 1);
      break;
    }

    case MODE_PATCHBAY: {
      // Dynamic Matrix Routing Visual
      const PatchbayConfig& pb = MidiIO::getPatchbayConfig();
      drawText(10, 35, "GLOBAL ROUTING MATRIX", ink, 1);

      drawRect(10, 50, 72, 34, ink);
      drawText(16, 56, "USB1:", ink, 1);
      drawText(16, 68, MidiIO::getUsbDeviceName(0), ink, 1);

      drawRect(118, 50, 72, 34, ink);
      drawText(124, 56, "DEST PORTS:", ink, 1);
      const char* outNames[] = {"ALL PORTS", "TRS 3.5mm", "USB SYNTH", "BLE SYNTH", "TRS + USB"};
      drawText(124, 68, outNames[pb.outPort % NUM_OUT_PORTS], ink, 1);

      // Patch cable connection
      drawLine(82, 67, 118, 67, ink);
      fillCircle(82, 67, 3, ink);
      fillCircle(118, 67, 3, ink);

      // CC Learn Status Box
      uint8_t lastCC = MidiIO::getLastLearnedCC();
      char ccStr[36];
      snprintf(ccStr, sizeof(ccStr), "LAST KNOB: CC#%d -> CC#%d", lastCC, pb.ccMap[lastCC]);
      drawRect(10, 96, 180, 24, ink);
      drawText(18, 105, ccStr, ink, 1);
      break;
    }

    case MODE_LOFI: {
      // Vintage Cassette Tape Graphic
      drawRect(24, 36, 152, 92, ink);
      drawRect(34, 44, 132, 22, ink);
      drawText(46, 52, "DX7 LO-FI CHROME 90", ink, 1);

      // Left & Right Spool Reels
      drawCircle(68, 92, 18, ink);
      drawCircle(68, 92, 6, ink);
      drawCircle(132, 92, 18, ink);
      drawCircle(132, 92, 6, ink);

      // Tape window between reels
      drawRect(88, 82, 24, 20, ink);
      drawLine(68, 110, 132, 110, ink);
      break;
    }

    case MODE_TURING: {
      // 16-Step Circular Shift-Register Ring
      uint16_t reg = turing ? turing->getShiftRegister() : 0xA5C3;
      uint8_t curStep = turing ? turing->getCurrentStep() : 0;
      int cx = 100, cy = 84, radius = 44;

      for (int i = 0; i < 16; i++) {
        float angle = (i * 2.0f * PI / 16.0f) - (PI / 2.0f);
        int nx = cx + (int)(cosf(angle) * radius);
        int ny = cy + (int)(sinf(angle) * radius);
        bool bitOn = (reg >> i) & 0x01;

        if (bitOn) fillCircle(nx, ny, 4, ink);
        else       drawCircle(nx, ny, 4, ink);

        if (i == curStep) {
          drawCircle(nx, ny, 7, ink);
        }
      }
      drawText(76, 80, "16-BIT REG", ink, 1);
      break;
    }

    case MODE_MORPH: {
      // 2D Vector Crossfader & Wave Interpolation
      uint8_t morphPct = currentEngine->getParamValuePercent(0);
      drawText(14, 42, "STATE A", ink, 1);
      drawText(144, 42, "STATE B", ink, 1);

      drawRect(20, 56, 160, 24, ink);
      int handleX = 20 + (morphPct * 156) / 100;
      fillRect(handleX - 2, 52, 5, 32, ink);

      // Draw morphed FM waveform below
      int prevX = 20, prevY = 112;
      for (int x = 20; x < 180; x += 2) {
        float phase = (x - 20) * 0.08f;
        float modIndex = 0.5f + (morphPct / 100.0f) * 4.0f;
        float yVal = sinf(phase + modIndex * sinf(phase * 3.0f));
        int y = 112 - (int)(yVal * 16.0f);
        drawLine(prevX, prevY, x, y, ink);
        prevX = x; prevY = y;
      }
      break;
    }

    case MODE_TUNING: {
      // Harmonic Cents Deviation Bar Chart across 12 pitch classes
      drawText(32, 36, "PURE HARMONIC RATIOS", ink, 1);
      drawLine(16, 90, 184, 90, ink); // 0 cents center line
      const int8_t barHeights[12] = {0, 12, 4, 16, -14, -2, -18, 2, 14, -16, -4, -12};
      for (int i = 0; i < 12; i++) {
        int bx = 20 + i * 14;
        int h = barHeights[i];
        if (h >= 0) fillRect(bx, 90 - h, 8, h + 1, ink);
        else        fillRect(bx, 90, 8, -h, ink);
      }
      drawText(38, 120, "5:4 M3  |  3:2 P5  |  6:5 m3", ink, 1);
      break;
    }

    case MODE_PHYSICS: {
      // Parabolic Bouncing Ball Trajectories
      drawLine(14, 126, 186, 126, ink); // Floor
      int bx = 24;
      int bounceH = 74;
      int bounceW = 42;
      for (int b = 0; b < 4; b++) {
        for (int x = 0; x < bounceW; x++) {
          float norm = (float)x / bounceW; // 0..1
          float parabola = 4.0f * norm * (1.0f - norm);
          int py = 126 - (int)(parabola * bounceH);
          drawPixel(bx + x, py, ink);
        }
        fillCircle(bx + bounceW / 2, 126 - bounceH, 3, ink);
        bx += bounceW;
        bounceH = (bounceH * 68) / 100;
        bounceW = (bounceW * 78) / 100;
      }
      break;
    }

    case MODE_VISUALIZER: {
      if (!visualizer) break;
      // Show active background sound engine badge
      const MidiEngine* bg = visualizer->getBackgroundSoundEngine();
      if (bg) {
        char bgLabel[32];
        snprintf(bgLabel, sizeof(bgLabel), "AUDIO FX: %s", bg->getName());
        drawText(6, 31, bgLabel, ink, 1);
      }

      uint8_t style = visualizer->getStyle();
      if (style == VIS_CONSTELLATION) {
        // 12-Node Circle of Fifths Harmonic Constellation
        int cx = 100, cy = 88, r = 42;
        int nodeX[12], nodeY[12];
        uint16_t mask = visualizer->getNotesMask();
        if (mask == 0) mask = (1 << (visualizer->getLastPitch() % 12)) | (1 << ((visualizer->getLastPitch() + 4) % 12)) | (1 << ((visualizer->getLastPitch() + 7) % 12));

        for (int i = 0; i < 12; i++) {
          int fifthIdx = (i * 7) % 12;
          float a = (i * 2.0f * PI / 12.0f) - (PI / 2.0f);
          nodeX[fifthIdx] = cx + (int)(cosf(a) * r);
          nodeY[fifthIdx] = cy + (int)(sinf(a) * r);
          if (mask & (1 << fifthIdx)) fillCircle(nodeX[fifthIdx], nodeY[fifthIdx], 4, ink);
          else                        drawCircle(nodeX[fifthIdx], nodeY[fifthIdx], 2, ink);
        }
        // Connect active chord nodes into geometric polygons
        for (int i = 0; i < 12; i++) {
          if (!(mask & (1 << i))) continue;
          for (int j = i + 1; j < 12; j++) {
            if (mask & (1 << j)) {
              drawLine(nodeX[i], nodeY[i], nodeX[j], nodeY[j], ink);
            }
          }
        }
      } else if (style == VIS_SPIROGRAPH) {
        // Spiro-Ink Lissajous Curves driven by played pitch & velocity
        int cx = 100, cy = 88;
        float freqA = (visualizer->getLastPitch() % 12) + 2.0f;
        float freqB = visualizer->getDensity() + 1.0f;
        float radius = 18.0f + (visualizer->getLastVel() / 127.0f) * 26.0f;
        int prevX = cx, prevY = cy;
        for (int step = 0; step <= 120; step++) {
          float t = step * 0.055f;
          int x = cx + (int)(sinf(freqA * t) * radius * 1.4f);
          int y = cy + (int)(cosf(freqB * t) * radius);
          if (step > 0) drawLine(prevX, prevY, x, y, ink);
          prevX = x; prevY = y;
        }
      } else if (style == VIS_CELLULAR) {
        // 16x16 Cellular Automata Matrix
        for (int ry = 0; ry < 16; ry++) {
          uint16_t rowBits = visualizer->getLifeRow(ry);
          for (int rx = 0; rx < 16; rx++) {
            int px = 52 + rx * 6;
            int py = 42 + ry * 6;
            if (rowBits & (1 << rx)) fillRect(px, py, 5, 5, ink);
            else                     drawPixel(px + 2, py + 2, ink);
          }
        }
      } else {
        // Mechanical Player-Piano Ribbon
        for (uint8_t i = 0; i < EngineVisualizer::HISTORY_LEN; i++) {
          const NoteHistoryEntry& e = visualizer->getHistoryEntry(i);
          if (e.velocity == 0) continue;
          int x = 180 - i * 10;
          int y = map(constrain(e.pitch, 36, 84), 36, 84, 130, 44);
          int w = map(e.velocity, 1, 127, 3, 9);
          fillRect(x, y, w, 4, ink);
        }
      }
      break;
    }
  }
}

void DisplayUI::drawFooterParameters(
  const MidiEngine* currentEngine,
  bool ink
) {
  drawLine(0, 142, EPD_WIDTH - 1, 142, ink);

  for (int i = 0; i < 4; i++) {
    int colX = i * 50;
    if (i > 0) {
      drawLine(colX, 142, colX, EPD_HEIGHT - 1, ink);
    }

    char pName[16] = {0};
    char pVal[16] = {0};
    currentEngine->getParamName(i, pName, sizeof(pName));
    currentEngine->getParamValueStr(i, pVal, sizeof(pVal));
    uint8_t pct = currentEngine->getParamValuePercent(i);

    // Truncate strings to 7 chars so each fits cleanly inside its 50px column
    pName[7] = '\0';
    pVal[7] = '\0';

    drawText(colX + 4, 148, pName, ink, 1);
    drawText(colX + 4, 162, pVal, ink, 1);

    // Progress Bar at bottom of column
    drawRect(colX + 4, 182, 42, 8, ink);
    int fillW = (pct * 40) / 100;
    if (fillW > 0) {
      fillRect(colX + 5, 183, fillW, 6, ink);
    }
  }
}

void DisplayUI::renderSerialHUD(
  const MidiEngine* currentEngine,
  uint8_t modeIndex,
  uint8_t activeLayersMask,
  const char* deviceName
) {
  if (!currentEngine) return;

  char pName[4][16];
  char pVal[4][16];
  uint8_t pPct[4];

  for (int i = 0; i < 4; i++) {
    currentEngine->getParamName(i, pName[i], sizeof(pName[i]));
    currentEngine->getParamValueStr(i, pVal[i], sizeof(pVal[i]));
    pPct[i] = currentEngine->getParamValuePercent(i);
  }

  Serial.println("\n╔═══════════════════════════════════════════════════════════════════╗");
  Serial.printf("║  SCREEN: %-19s │  LAYERS: [%c][%c][%c][%c][%c]  │ %-3dBPM ║\n",
                currentEngine->getName(),
                (activeLayersMask & 0x01) ? 'L' : '.',
                (activeLayersMask & 0x02) ? 'T' : '.',
                (activeLayersMask & 0x04) ? 'M' : '.',
                (activeLayersMask & 0x08) ? 'U' : '.',
                (activeLayersMask & 0x10) ? 'P' : '.',
                currentEngine->getBpm());
  Serial.println("╠═════════════════╦═════════════════╦═════════════════╦═════════════╣");
  Serial.printf("║ [1] %-11s ║ [2] %-11s ║ [3] %-11s ║ [4] %-7s ║\n",
                pName[0], pName[1], pName[2], pName[3]);
  Serial.printf("║     %-11s ║     %-11s ║     %-11s ║     %-7s ║\n",
                pVal[0], pVal[1], pVal[2], pVal[3]);
  Serial.printf("║ Bar:[%-3d%%]     ║ Bar:[%-3d%%]     ║ Bar:[%-3d%%]     ║ Bar:[%-3d%%] ║\n",
                pPct[0], pPct[1], pPct[2], pPct[3]);
  Serial.println("╚═════════════════╩═════════════════╩═════════════════╩═════════════╝");
}
