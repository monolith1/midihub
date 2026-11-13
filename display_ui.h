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

  // --- Framebuffer Drawing Primitives ---
  static void clearBuffer(uint8_t* fb);
  static void drawPixel(uint8_t* fb, int x, int y, bool on = true);
  static void drawLine(uint8_t* fb, int x0, int y0, int x1, int y1, bool on = true);
  static void drawRect(uint8_t* fb, int x, int y, int w, int h, bool on = true);
  static void fillRect(uint8_t* fb, int x, int y, int w, int h, bool on = true);
  static void drawCircle(uint8_t* fb, int cx, int cy, int r, bool on = true);
  static void fillCircle(uint8_t* fb, int cx, int cy, int r, bool on = true);
  static void drawChar(uint8_t* fb, int x, int y, char c, bool on = true, uint8_t scale = 1);
  static void drawText(uint8_t* fb, int x, int y, const char* str, bool on = true, uint8_t scale = 1);

private:
  static uint8_t s_fbLeft[1024];  // Screen 1: Parameters (128x64 / 8)
  static uint8_t s_fbRight[1024]; // Screen 2: Visualizer (128x64 / 8)
  static uint32_t s_lastRenderTime;
  static bool s_isDirty;

  static void composeScreenLeft(
    const MidiEngine* currentEngine,
    uint8_t modeIndex,
    uint8_t activeLayersMask,
    const char* deviceName
  );

  static void composeScreenRight(
    const MidiEngine* currentEngine,
    uint8_t modeIndex,
    const EngineVisualizer* visualizer,
    const EngineTuring* turing
  );
};
