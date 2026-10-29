#pragma once
#include <Arduino.h>
#include "config.h"
#include "midi_io.h"

class StorageManager {
public:
  static void init();
  static void markDirty();
  static void update(uint8_t currentMode, uint8_t savedSoundMode);

  // Load saved state from ESP32 NVS Flash on boot
  static bool loadState(uint8_t& outMode, uint8_t& outSavedSoundMode);

  // Save immediately to NVS Flash
  static void saveNow(uint8_t currentMode, uint8_t savedSoundMode);

  // Wipe NVS Flash & restore factory defaults
  static void clearAllSettings();

private:
  static bool s_isDirty;
  static uint32_t s_lastDirtyTime;
};
