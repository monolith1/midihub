#include "storage_sd.h"

bool StorageSD::s_available = false;
File StorageSD::s_recordFile;
uint32_t StorageSD::s_recordStartTime = 0;
uint32_t StorageSD::s_eventCount = 0;

bool StorageSD::init() {
#if ENABLE_SD_CARD
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  // Initialize SPI bus with shared SCK, MISO, MOSI, and dedicated SD_CS
  // Uses the default ESP32 SPI bus configured in config.h
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  if (!SD.begin(PIN_SD_CS, SPI, 20000000)) { // 20MHz SPI clock for SD
    Serial.println("[SD CARD] Card Mount Failed or No Card Inserted (Running from internal NVS Flash).");
    s_available = false;
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD CARD] No SD card attached.");
    s_available = false;
    return false;
  }

  s_available = true;
  uint64_t cardSizeMB = SD.cardSize() / (1024 * 1024);
  Serial.printf("[SD CARD] Initialized Successfully! Type: %s, Size: %llu MB\n",
                (cardType == CARD_MMC) ? "MMC" :
                (cardType == CARD_SD)  ? "SDSC" :
                (cardType == CARD_SDHC) ? "SDHC" : "UNKNOWN",
                cardSizeMB);

  // Ensure default working directories exist
  if (!SD.exists("/midi")) SD.mkdir("/midi");
  if (!SD.exists("/presets")) SD.mkdir("/presets");
  if (!SD.exists("/scales")) SD.mkdir("/scales");

  return true;
#else
  s_available = false;
  return false;
#endif
}

bool StorageSD::isAvailable() {
  return s_available;
}

uint64_t StorageSD::getCardSizeBytes() {
#if ENABLE_SD_CARD
  if (!s_available) return 0;
  return SD.cardSize();
#else
  return 0;
#endif
}

uint64_t StorageSD::getCardUsedBytes() {
#if ENABLE_SD_CARD
  if (!s_available) return 0;
  return SD.usedBytes();
#else
  return 0;
#endif
}

bool StorageSD::fileExists(const char* path) {
#if ENABLE_SD_CARD
  if (!s_available) return false;
  return SD.exists(path);
#else
  return false;
#endif
}

bool StorageSD::createDirectory(const char* path) {
#if ENABLE_SD_CARD
  if (!s_available) return false;
  return SD.mkdir(path);
#else
  return false;
#endif
}

bool StorageSD::savePreset(const char* presetName, uint8_t mode, uint8_t layersMask) {
#if ENABLE_SD_CARD
  if (!s_available) return false;
  char path[64];
  snprintf(path, sizeof(path), "/presets/%s.prs", presetName);

  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;

  f.printf("MODE=%u\n", mode);
  f.printf("LAYERS=%u\n", layersMask);
  f.close();
  Serial.printf("[SD PRESET] Saved to %s\n", path);
  return true;
#else
  return false;
#endif
}

bool StorageSD::loadPreset(const char* presetName, uint8_t& outMode, uint8_t& outLayersMask) {
#if ENABLE_SD_CARD
  if (!s_available) return false;
  char path[64];
  snprintf(path, sizeof(path), "/presets/%s.prs", presetName);

  if (!SD.exists(path)) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("MODE=")) {
      outMode = line.substring(5).toInt();
    } else if (line.startsWith("LAYERS=")) {
      outLayersMask = line.substring(7).toInt();
    }
  }
  f.close();
  Serial.printf("[SD PRESET] Loaded from %s\n", path);
  return true;
#else
  return false;
#endif
}

bool StorageSD::startMidiRecording(const char* filename) {
#if ENABLE_SD_CARD
  if (!s_available) return false;
  char path[64];
  snprintf(path, sizeof(path), "/midi/%s.mid", filename);

  s_recordFile = SD.open(path, FILE_WRITE);
  if (!s_recordFile) return false;

  s_recordStartTime = millis();
  s_eventCount = 0;
  Serial.printf("[SD MIDI REC] Started recording: %s\n", path);
  return true;
#else
  return false;
#endif
}

void StorageSD::logMidiEvent(uint8_t status, uint8_t data1, uint8_t data2) {
#if ENABLE_SD_CARD
  if (!s_available || !s_recordFile) return;

  uint32_t relTimeMs = millis() - s_recordStartTime;
  // Write compact binary event record: [TimeMs 4 bytes][Status 1 byte][Data1 1 byte][Data2 1 byte]
  s_recordFile.write((const uint8_t*)&relTimeMs, 4);
  s_recordFile.write(status);
  s_recordFile.write(data1);
  s_recordFile.write(data2);
  s_eventCount++;
#endif
}

bool StorageSD::stopMidiRecording() {
#if ENABLE_SD_CARD
  if (!s_available || !s_recordFile) return false;
  s_recordFile.flush();
  s_recordFile.close();
  Serial.printf("[SD MIDI REC] Stopped recording. Saved %lu events.\n", (unsigned long)s_eventCount);
  return true;
#else
  return false;
#endif
}
