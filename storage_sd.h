#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include "config.h"

class StorageSD {
public:
  static bool init();
  static bool isAvailable();
  
  // File System Helpers
  static uint64_t getCardSizeBytes();
  static uint64_t getCardUsedBytes();
  static bool fileExists(const char* path);
  static bool createDirectory(const char* path);

  // Quick Preset Export/Import (JSON / Key-Value)
  static bool savePreset(const char* presetName, uint8_t mode, uint8_t layersMask);
  static bool loadPreset(const char* presetName, uint8_t& outMode, uint8_t& outLayersMask);

  // Standard MIDI File (.MID) Recording Header / Logger
  static bool startMidiRecording(const char* filename);
  static void logMidiEvent(uint8_t status, uint8_t data1, uint8_t data2);
  static bool stopMidiRecording();

private:
  static bool s_available;
  static File s_recordFile;
  static uint32_t s_recordStartTime;
  static uint32_t s_eventCount;
};
