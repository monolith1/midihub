#pragma once
#include <Arduino.h>
#include "config.h"
#include "engine_base.h"
#include "engine_visualizer.h"
#include "engine_turing.h"

class DisplayUI {
public:
  static void init();
  static void markDirty();
  static void update(
    const MidiEngine* currentEngine,
    uint8_t modeIndex,
    uint8_t activeLayersMask,
    const char* deviceName,
    const EngineVisualizer* visualizer,
    const EngineTuring* turing
  );
  static void renderSerialHUD(
    const MidiEngine* currentEngine,
    uint8_t modeIndex,
    uint8_t activeLayersMask,
    const char* deviceName
  );

  // --- Low-Level 200x200 1-Bit Framebuffer Primitives ---
  static void clearBuffer(bool blackBg = false);
  static void drawPixel(int x, int y, bool black = true);
  static void drawLine(int x0, int y0, int x1, int y1, bool black = true);
  static void drawRect(int x, int y, int w, int h, bool black = true);
  static void fillRect(int x, int y, int w, int h, bool black = true);
  static void drawCircle(int cx, int cy, int r, bool black = true);
  static void fillCircle(int cx, int cy, int r, bool black = true);
  static void drawChar(int x, int y, char c, bool black = true, uint8_t scale = 1);
  static void drawText(int x, int y, const char* str, bool black = true, uint8_t scale = 1);

private:
  static uint8_t s_frameBuffer[(EPD_WIDTH * EPD_HEIGHT) / 8];
  static uint32_t s_lastDirtyTime;
  static bool s_isDirty;
  static uint8_t s_lastRenderedMode;
  static uint8_t s_lastLayersMask;

  static void composeFrame(
    const MidiEngine* currentEngine,
    uint8_t modeIndex,
    uint8_t activeLayersMask,
    const char* deviceName,
    const EngineVisualizer* visualizer,
    const EngineTuring* turing
  );
  static void drawHeader(
    const char* modeName,
    uint32_t bpm,
    uint8_t activeLayersMask,
    const char* deviceName,
    bool inkColor
  );
  static void drawCenterGraphic(
    const MidiEngine* currentEngine,
    uint8_t modeIndex,
    const EngineVisualizer* visualizer,
    const EngineTuring* turing,
    bool inkColor
  );
  static void drawFooterParameters(
    const MidiEngine* currentEngine,
    bool inkColor
  );

  // Non-blocking E-Paper SPI hardware transmitter
  static void pushBufferToEPaper(bool fullRefresh);
  static void epdSendCommand(uint8_t cmd);
  static void epdSendData(uint8_t data);
};
