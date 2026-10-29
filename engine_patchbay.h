#pragma once
#include "engine_base.h"
#include "storage.h"

class EnginePatchbay : public MidiEngine {
public:
  EnginePatchbay()
    : m_selectedSrcCC(74), m_clearArmed(false), m_wasClearedBanner(false) {}

  const char* getName() const override { return "PATCHBAY & CC MAP"; }

  void init() override {
    m_wasClearedBanner = false;
  }

  void update() override {}

  void onNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) override {
    MidiIO::sendNoteOn(channel, pitch, velocity);
  }

  void onNoteOff(uint8_t channel, uint8_t pitch) override {
    MidiIO::sendNoteOff(channel, pitch);
  }

  void onControlChange(uint8_t channel, uint8_t control, uint8_t value) override {
    // Pass mapped CC out so user can hear the remapped knob live
    MidiIO::sendControlChange(channel, control, value);
  }

  void onPitchBend(uint8_t channel, int16_t bend) override {
    MidiIO::sendPitchBend(channel, bend);
  }

  void onEncoderTurn(uint8_t encoderIndex, int delta) override {
    PatchbayConfig& pb = MidiIO::getPatchbayConfig();
    m_wasClearedBanner = false;

    switch (encoderIndex) {
      case 0: // Input Port Selection
        pb.inPort = (pb.inPort + delta + NUM_IN_PORTS) % NUM_IN_PORTS;
        StorageManager::markDirty();
        break;
      case 1: // Output Port Selection
        pb.outPort = (pb.outPort + delta + NUM_OUT_PORTS) % NUM_OUT_PORTS;
        StorageManager::markDirty();
        break;
      case 2: // Source CC Selection (0..127)
        m_selectedSrcCC = constrain((int)m_selectedSrcCC + delta, 0, 127);
        break;
      case 3: { // Target CC Assignment (0..127) for selected Source CC
        int curDst = pb.ccMap[m_selectedSrcCC];
        pb.ccMap[m_selectedSrcCC] = (uint8_t)constrain(curDst + delta, 0, 127);
        StorageManager::markDirty();
        break;
      }
    }
  }

  void onEncoderClick(uint8_t encoderIndex) override {
    PatchbayConfig& pb = MidiIO::getPatchbayConfig();

    switch (encoderIndex) {
      case 0: // Click Enc 1 (when in Param edit): Cycle Input Channel (0=Omni, 1..16)
        pb.inChannel = (pb.inChannel + 1) % 17;
        StorageManager::markDirty();
        break;
      case 1: // Click Enc 2: Cycle Output Channel (1..16)
        pb.outChannel = (pb.outChannel % 16) + 1;
        StorageManager::markDirty();
        break;
      case 2: // Click Enc 3: MIDI Learn! Snap to last received CC from controller
        m_selectedSrcCC = MidiIO::getLastLearnedCC();
        break;
      case 3: // Click Enc 4: If Switch 2 is ON -> Factory Reset / Clear NVS! Otherwise reset this CC to 1:1
        if (m_clearArmed) {
          StorageManager::clearAllSettings();
          m_wasClearedBanner = true;
        } else {
          pb.ccMap[m_selectedSrcCC] = m_selectedSrcCC;
          StorageManager::markDirty();
        }
        break;
    }
  }

  void onSwitch1(bool active) override {}

  void onSwitch2(bool active) override {
    // Switch 2 arms the "Clear All Settings" action on Encoder 4 click
    m_clearArmed = active;
    if (!active) m_wasClearedBanner = false;
  }

  void getParamName(uint8_t idx, char* buf, size_t maxLen) const override {
    if (idx == 3 && m_clearArmed) {
      strncpy(buf, "HOLD SW2", maxLen);
      return;
    }
    const char* names[] = {"IN SOURCE", "OUT DEST", "CC LEARN", "CC TARGET"};
    if (idx < 4) strncpy(buf, names[idx], maxLen);
  }

  void getParamValueStr(uint8_t idx, char* buf, size_t maxLen) const override {
    const PatchbayConfig& pb = MidiIO::getPatchbayConfig();

    switch (idx) {
      case 0: {
        const char* inNames[] = {"ALL IN", "USB ANY", "USB #1", "USB #2", "BLE IN"};
        if (pb.inChannel == 0) {
          snprintf(buf, maxLen, "%s:Omni", inNames[pb.inPort % NUM_IN_PORTS]);
        } else {
          snprintf(buf, maxLen, "%s:Ch%d", inNames[pb.inPort % NUM_IN_PORTS], pb.inChannel);
        }
        break;
      }
      case 1: {
        const char* outNames[] = {"ALL OUT", "TRS OUT", "USB OUT", "BLE OUT", "TRS+USB"};
        snprintf(buf, maxLen, "%s:C%d", outNames[pb.outPort % NUM_OUT_PORTS], pb.outChannel);
        break;
      }
      case 2:
        snprintf(buf, maxLen, "In CC#%d", m_selectedSrcCC);
        break;
      case 3: {
        if (m_wasClearedBanner) {
          strncpy(buf, "CLEARED!", maxLen);
        } else if (m_clearArmed) {
          strncpy(buf, "CLK=RESET", maxLen);
        } else {
          uint8_t dst = pb.ccMap[m_selectedSrcCC];
          snprintf(buf, maxLen, "->CC#%d", dst);
        }
        break;
      }
    }
  }

  uint8_t getParamValuePercent(uint8_t idx) const override {
    const PatchbayConfig& pb = MidiIO::getPatchbayConfig();
    switch (idx) {
      case 0: return map(pb.inPort, 0, NUM_IN_PORTS - 1, 0, 100);
      case 1: return map(pb.outPort, 0, NUM_OUT_PORTS - 1, 0, 100);
      case 2: return map(m_selectedSrcCC, 0, 127, 0, 100);
      case 3: return m_clearArmed ? 100 : map(pb.ccMap[m_selectedSrcCC], 0, 127, 0, 100);
      default: return 0;
    }
  }

  uint8_t getSelectedSrcCC() const { return m_selectedSrcCC; }
  bool isClearArmed() const { return m_clearArmed; }

private:
  uint8_t m_selectedSrcCC;
  bool m_clearArmed;
  bool m_wasClearedBanner;
};
