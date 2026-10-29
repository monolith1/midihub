#include "storage.h"
#include <Preferences.h>

bool StorageManager::s_isDirty = false;
uint32_t StorageManager::s_lastDirtyTime = 0;

static Preferences s_prefs;

void StorageManager::init() {
  s_isDirty = false;
  s_lastDirtyTime = 0;
}

void StorageManager::markDirty() {
  s_isDirty = true;
  s_lastDirtyTime = millis();
}

void StorageManager::update(uint8_t currentMode, uint8_t savedSoundMode) {
  if (!s_isDirty) return;
  if (millis() - s_lastDirtyTime >= NVS_AUTOSAVE_DELAY_MS) {
    saveNow(currentMode, savedSoundMode);
    s_isDirty = false;
  }
}

bool StorageManager::loadState(uint8_t& outMode, uint8_t& outSavedSoundMode) {
  if (!s_prefs.begin(NVS_NAMESPACE, true)) { // Read-only mode
    return false;
  }

  bool hasSaved = s_prefs.getBool("initialized", false);
  if (!hasSaved) {
    s_prefs.end();
    return false;
  }

  outMode = s_prefs.getUChar("curMode", MODE_BYPASS);
  if (outMode >= NUM_MODES) outMode = MODE_BYPASS;

  outSavedSoundMode = s_prefs.getUChar("sndMode", MODE_LOFI);
  if (outSavedSoundMode >= MODE_VISUALIZER) outSavedSoundMode = MODE_LOFI;

  PatchbayConfig& pb = MidiIO::getPatchbayConfig();
  pb.inPort     = s_prefs.getUChar("inPort", IN_PORT_ALL);
  pb.inChannel  = s_prefs.getUChar("inChan", 0);
  pb.outPort    = s_prefs.getUChar("outPort", OUT_PORT_ALL);
  pb.outChannel = s_prefs.getUChar("outChan", 1);

  if (s_prefs.getBytesLength("ccMap") == 128) {
    s_prefs.getBytes("ccMap", pb.ccMap, 128);
  }

  s_prefs.end();
  Serial.printf("[NVS] Restored saved settings (Mode: %d, InPort: %d, OutPort: %d, OutCh: %d)\n",
                outMode, pb.inPort, pb.outPort, pb.outChannel);
  return true;
}

void StorageManager::saveNow(uint8_t currentMode, uint8_t savedSoundMode) {
  if (!s_prefs.begin(NVS_NAMESPACE, false)) { // Read-write mode
    return;
  }

  s_prefs.putBool("initialized", true);
  s_prefs.putUChar("curMode", currentMode);
  s_prefs.putUChar("sndMode", savedSoundMode);

  PatchbayConfig& pb = MidiIO::getPatchbayConfig();
  s_prefs.putUChar("inPort", pb.inPort);
  s_prefs.putUChar("inChan", pb.inChannel);
  s_prefs.putUChar("outPort", pb.outPort);
  s_prefs.putUChar("outChan", pb.outChannel);
  s_prefs.putBytes("ccMap", pb.ccMap, 128);

  s_prefs.end();
  Serial.println("[NVS] Auto-saved Patchbay, CC Map & Mode state to Flash.");
}

void StorageManager::clearAllSettings() {
  if (s_prefs.begin(NVS_NAMESPACE, false)) {
    s_prefs.clear();
    s_prefs.end();
  }
  MidiIO::resetPatchbayDefaults();
  s_isDirty = false;
  Serial.println("[NVS] FACTORY RESET: Cleared all saved settings from Flash.");
}
