#include "display_ui.h"
#include <Wire.h>

#define OLED_ADDR   0x3C
#define OLED_WIDTH  128
#define OLED_HEIGHT 64

// Screen 1: Bus 0 (Wire)
#define S1_SDA 11
#define S1_SCL 12

// Screen 2: Bus 1 (Wire1)
#define S2_SDA 10
#define S2_SCL 9

uint8_t DisplayUI::s_fbLeft[1024];
uint8_t DisplayUI::s_fbRight[1024];
uint32_t DisplayUI::s_lastRenderTime = 0;
bool DisplayUI::s_isDirty = true;

// Compact 5x7 ASCII Font (32 ' ' to 126 '~')
static const uint8_t FONT5x7[][5] PROGMEM = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44}
};

static void sendOledCommand(TwoWire& bus, uint8_t cmd) {
  bus.beginTransmission(OLED_ADDR);
  bus.write(0x00);
  bus.write(cmd);
  bus.endTransmission();
}

static void initOledBus(TwoWire& bus) {
  static const uint8_t cmds[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
    0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
  };
  for (size_t i = 0; i < sizeof(cmds); i++) {
    sendOledCommand(bus, cmds[i]);
  }
}

static void pushBuffer(TwoWire& bus, const uint8_t* buffer) {
  sendOledCommand(bus, 0x21); sendOledCommand(bus, 0); sendOledCommand(bus, 127);
  sendOledCommand(bus, 0x22); sendOledCommand(bus, 0); sendOledCommand(bus, 7);

  for (uint16_t i = 0; i < 1024; i += 16) {
    bus.beginTransmission(OLED_ADDR);
    bus.write(0x40);
    for (uint8_t j = 0; j < 16; j++) {
      bus.write(buffer[i + j]);
    }
    bus.endTransmission();
  }
}

void DisplayUI::init() {
  Wire.begin(S1_SDA, S1_SCL, 400000);
  initOledBus(Wire);

  Wire1.begin(S2_SDA, S2_SCL, 400000);
  initOledBus(Wire1);

  clearBuffer(s_fbLeft);
  clearBuffer(s_fbRight);
  s_isDirty = true;
  s_lastRenderTime = millis();
}

void DisplayUI::markDirty() {
  s_isDirty = true;
}

void DisplayUI::clearBuffer(uint8_t* fb) {
  memset(fb, 0, 1024);
}

void DisplayUI::drawPixel(uint8_t* fb, int x, int y, bool on) {
  if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
  uint16_t idx = x + (y / 8) * OLED_WIDTH;
  uint8_t bit = 1 << (y & 7);
  if (on) fb[idx] |= bit;
  else    fb[idx] &= ~bit;
}

void DisplayUI::drawLine(uint8_t* fb, int x0, int y0, int x1, int y1, bool on) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    drawPixel(fb, x0, y0, on);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void DisplayUI::drawRect(uint8_t* fb, int x, int y, int w, int h, bool on) {
  drawLine(fb, x, y, x + w - 1, y, on);
  drawLine(fb, x, y + h - 1, x + w - 1, y + h - 1, on);
  drawLine(fb, x, y, x, y + h - 1, on);
  drawLine(fb, x + w - 1, y, x + w - 1, y + h - 1, on);
}

void DisplayUI::fillRect(uint8_t* fb, int x, int y, int w, int h, bool on) {
  for (int i = 0; i < w; i++) {
    drawLine(fb, x + i, y, x + i, y + h - 1, on);
  }
}

void DisplayUI::drawCircle(uint8_t* fb, int cx, int cy, int r, bool on) {
  int f = 1 - r, ddF_x = 1, ddF_y = -2 * r;
  int x = 0, y = r;
  drawPixel(fb, cx, cy + r, on);
  drawPixel(fb, cx, cy - r, on);
  drawPixel(fb, cx + r, cy, on);
  drawPixel(fb, cx - r, cy, on);
  while (x < y) {
    if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
    x++; ddF_x += 2; f += ddF_x;
    drawPixel(fb, cx + x, cy + y, on);
    drawPixel(fb, cx - x, cy + y, on);
    drawPixel(fb, cx + x, cy - y, on);
    drawPixel(fb, cx - x, cy - y, on);
    drawPixel(fb, cx + y, cy + x, on);
    drawPixel(fb, cx - y, cy + x, on);
    drawPixel(fb, cx + y, cy - x, on);
    drawPixel(fb, cx - y, cy - x, on);
  }
}

void DisplayUI::fillCircle(uint8_t* fb, int cx, int cy, int r, bool on) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) {
        drawPixel(fb, cx + x, cy + y, on);
      }
    }
  }
}

void DisplayUI::drawChar(uint8_t* fb, int x, int y, char c, bool on, uint8_t scale) {
  if (c < 32 || c > 126) c = ' ';
  const uint8_t* glyph = FONT5x7[c - 32];
  for (int col = 0; col < 5; col++) {
    uint8_t b = pgm_read_byte(&glyph[col]);
    for (int row = 0; row < 7; row++) {
      if (b & (1 << row)) {
        if (scale == 1) {
          drawPixel(fb, x + col, y + row, on);
        } else {
          fillRect(fb, x + col * scale, y + row * scale, scale, scale, on);
        }
      }
    }
  }
}

void DisplayUI::drawText(uint8_t* fb, int x, int y, const char* str, bool on, uint8_t scale) {
  while (*str) {
    drawChar(fb, x, y, *str++, on, scale);
    x += 6 * scale;
  }
}

void DisplayUI::composeScreenLeft(
  const MidiEngine* currentEngine,
  uint8_t modeIndex,
  uint8_t activeLayersMask,
  const char* deviceName
) {
  clearBuffer(s_fbLeft);

  // 1. Yellow Header Zone (y = 0..15): Engine Title & Active Layer Badges
  char title[24];
  snprintf(title, sizeof(title), "%s", currentEngine->getName());
  title[14] = '\0'; // Truncate cleanly to fit
  drawText(s_fbLeft, 2, 3, title, true, 1);

  // Badges [L][T][M][U][P]
  const char badges[5] = {'L', 'T', 'M', 'U', 'P'};
  for (int b = 0; b < 5; b++) {
    int bx = 92 + (b * 7);
    if (activeLayersMask & (1 << b)) {
      fillRect(s_fbLeft, bx, 2, 6, 9, true);
      drawChar(s_fbLeft, bx + 1, 3, badges[b], false, 1); // Inverted text
    } else {
      drawChar(s_fbLeft, bx + 1, 3, '.', true, 1);
    }
  }

  // Divider line between Yellow and Blue zones
  drawLine(s_fbLeft, 0, 15, 127, 15, true);

  // 2. Blue Zone (y = 16..63): 4 Parameter Rows with Values & Progress Bars
  for (int p = 0; p < 4; p++) {
    int rowY = 17 + (p * 11);
    char pName[16] = {0};
    char pVal[16] = {0};
    currentEngine->getParamName(p, pName, sizeof(pName));
    currentEngine->getParamValueStr(p, pVal, sizeof(pVal));
    uint8_t pct = currentEngine->getParamValuePercent(p);

    pName[6] = '\0';
    pVal[7] = '\0';

    // Draw "[1] NAME: VAL"
    char lineBuf[24];
    snprintf(lineBuf, sizeof(lineBuf), "%d:%-6s %-7s", p + 1, pName, pVal);
    drawText(s_fbLeft, 2, rowY, lineBuf, true, 1);

    // Mini percentage bar on right
    drawRect(s_fbLeft, 98, rowY + 1, 28, 6, true);
    int fillW = (pct * 26) / 100;
    if (fillW > 0) {
      fillRect(s_fbLeft, 99, rowY + 2, fillW, 4, true);
    }
  }
}

static float s_animPhase = 0.0f;

void DisplayUI::composeScreenRight(
  const MidiEngine* currentEngine,
  uint8_t modeIndex,
  const EngineVisualizer* visualizer,
  const EngineTuring* turing
) {
  clearBuffer(s_fbRight);

  // Yellow Header Zone
  drawText(s_fbRight, 2, 3, "LIVE ENGINE SCOPE", true, 1);
  char bpmStr[8];
  snprintf(bpmStr, sizeof(bpmStr), "%dB", currentEngine->getBpm());
  drawText(s_fbRight, 100, 3, bpmStr, true, 1);
  drawLine(s_fbRight, 0, 15, 127, 15, true);

  // Blue Visualizer Zone: Reactive rendering based on current engine
  s_animPhase += 0.06f;

  if (modeIndex == MODE_TURING && turing) {
    // Turing algorithmic shift-register bits grid
    uint16_t reg = turing->getShiftRegister();
    for (int i = 0; i < 16; i++) {
      int bx = 8 + (i % 8) * 14;
      int by = 22 + (i / 8) * 18;
      drawRect(s_fbRight, bx, by, 10, 12, true);
      if (reg & (1 << i)) {
        fillRect(s_fbRight, bx + 2, by + 2, 6, 8, true);
      }
    }
  } else {
    // Dynamic organic fluid scope
    for (int x = 0; x < 128; x++) {
      float yf = 39.0f + 15.0f * sinf(s_animPhase + x * 0.09f) * cosf(s_animPhase * 0.6f);
      int y = (int)yf;
      drawLine(s_fbRight, x, 39, x, y, true);
    }
  }
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
  // 30 FPS smooth live screen updates
  if (now - s_lastRenderTime >= 33) {
    s_lastRenderTime = now;

    composeScreenLeft(currentEngine, modeIndex, activeLayersMask, deviceName);
    composeScreenRight(currentEngine, modeIndex, visualizer, turing);

    pushBuffer(Wire, s_fbLeft);
    pushBuffer(Wire1, s_fbRight);
  }
}

void DisplayUI::renderSerialHUD(
  const MidiEngine* currentEngine,
  uint8_t modeIndex,
  uint8_t activeLayersMask,
  const char* deviceName
) {
  // Optional Serial HUD mirror
}
