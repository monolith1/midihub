#pragma once
#include "engine_base.h"

class EngineLoFi : public MidiEngine {
public:
  EngineLoFi() 
    : m_tapeAge(35), m_flutterDepth(25), m_dropoutRate(15), m_filterDrive(60),
      m_outChannel(1), m_lastLfoUpdate(0), m_lfoPhaseWow(0.0f), m_lfoPhaseFlutter(0.0f),
      m_isTapeStopped(false), m_tapeBrakeProgress(0.0f) {}

  const char* getName() const override { return "LO-FI CASSETTE"; }

  void init() override {
    m_lastLfoUpdate = millis();
  }

  void update() override {
    uint32_t now = millis();
    float dt = (now - m_lastLfoUpdate) / 1000.0f;
    if (dt <= 0.005f) return; // Update at ~100Hz
    m_lastLfoUpdate = now;

    // Handle Tape Stop effect (triggered by Switch 2)
    if (m_isTapeStopped) {
      if (m_tapeBrakeProgress < 1.0f) {
        m_tapeBrakeProgress += dt * 1.5f; // Drops down over ~0.7s
        if (m_tapeBrakeProgress > 1.0f) m_tapeBrakeProgress = 1.0f;
        int16_t brakeBend = (int16_t)(-8192 * m_tapeBrakeProgress);
        MidiIO::setLfoBendOffset(m_outChannel, brakeBend);
      }
      return;
    } else if (m_tapeBrakeProgress > 0.0f) {
      m_tapeBrakeProgress -= dt * 3.0f; // Spin back up fast
      if (m_tapeBrakeProgress < 0.0f) m_tapeBrakeProgress = 0.0f;
    }

    // Dual-stage LFO: Slow Wow (0.7 Hz) + Fast Flutter (6.1 Hz)
    float wowFreq = 0.5f + (m_tapeAge / 100.0f) * 0.8f;
    float flutterFreq = 5.0f + (m_tapeAge / 100.0f) * 3.0f;

    m_lfoPhaseWow += wowFreq * dt * 2.0f * PI;
    if (m_lfoPhaseWow > 2.0f * PI) m_lfoPhaseWow -= 2.0f * PI;

    m_lfoPhaseFlutter += flutterFreq * dt * 2.0f * PI;
    if (m_lfoPhaseFlutter > 2.0f * PI) m_lfoPhaseFlutter -= 2.0f * PI;

    // Calculate micro-pitch bend drift (in 14-bit units)
    float wow = sinf(m_lfoPhaseWow);
    float flutter = sinf(m_lfoPhaseFlutter) * 0.4f + ((rand() % 100) / 100.0f - 0.5f) * 0.2f;

    float combined = (wow * 0.7f + flutter * 0.3f);
    // Depth scaling: 0..100 maps to approx +/- 10 to +/- 500 bend units (up to ~60 cents drift)
    int16_t bend = (int16_t)(combined * m_flutterDepth * 6.0f);
    
    MidiIO::setLfoBendOffset(m_outChannel, bend);
  }

  void onNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) override {
    // 1. Tape Dropout Simulation
    if (m_dropoutRate > 0 && (rand() % 100) < m_dropoutRate) {
      // 50% chance of complete dropout, 50% chance of ghostly whisper
      if ((rand() % 2) == 0) {
        return; // Drop note completely!
      } else {
        velocity = (uint8_t)(velocity * 0.25f); // Ghost note
      }
    }

    // 2. Velocity Squelch / Age reduction
    float ageScale = 1.0f - (m_tapeAge / 100.0f) * 0.35f;
    uint8_t finalVel = (uint8_t)(velocity * ageScale);
    if (finalVel < 1) finalVel = 1;

    emitNoteOn(m_outChannel, pitch, finalVel);
  }

  void onNoteOff(uint8_t channel, uint8_t pitch) override {
    emitNoteOff(m_outChannel, pitch);
  }

  void onControlChange(uint8_t channel, uint8_t control, uint8_t value) override {
    MidiIO::sendControlChange(m_outChannel, control, value);
  }

  void onPitchBend(uint8_t channel, int16_t bend) override {
    // User pitch bend from wheel passes through added to wow/flutter
  }

  void onEncoderTurn(uint8_t encoderIndex, int delta) override {
    switch (encoderIndex) {
      case 0: // Tape Age (0 - 100%)
        m_tapeAge = constrain(m_tapeAge + delta * 2, 0, 100);
        break;
      case 1: // Flutter Depth (0 - 100%)
        m_flutterDepth = constrain(m_flutterDepth + delta * 2, 0, 100);
        break;
      case 2: // Dropout Probability (0 - 50%)
        m_dropoutRate = constrain(m_dropoutRate + delta, 0, 50);
        break;
      case 3: // Filter Drive (FM-1 Cutoff CC 74)
        m_filterDrive = constrain(m_filterDrive + delta * 2, 0, 127);
        MidiIO::sendControlChange(m_outChannel, 74, m_filterDrive);
        break;
    }
  }

  void onEncoderClick(uint8_t encoderIndex) override {
    if (encoderIndex == 0) m_tapeAge = 25; // Reset to mild vintage tape
    if (encoderIndex == 2) m_dropoutRate = 0; // Disable dropouts
  }

  void onSwitch1(bool active) override {}

  void onSwitch2(bool active) override {
    // Switch 2 = Tape Brake / Stop effect!
    m_isTapeStopped = active;
    if (!active) {
      MidiIO::sendPitchBend(m_outChannel, 0); // Restore pitch
    }
  }

  void getParamName(uint8_t idx, char* buf, size_t maxLen) const override {
    const char* names[] = {"TAPE AGE", "FLUTTER", "DROPOUTS", "DRIVE/CUT"};
    if (idx < 4) strncpy(buf, names[idx], maxLen);
  }

  void getParamValueStr(uint8_t idx, char* buf, size_t maxLen) const override {
    switch (idx) {
      case 0: snprintf(buf, maxLen, "%d%%", m_tapeAge); break;
      case 1: snprintf(buf, maxLen, "%d%%", m_flutterDepth); break;
      case 2: snprintf(buf, maxLen, "%d%%", m_dropoutRate); break;
      case 3: snprintf(buf, maxLen, "%d", m_filterDrive); break;
    }
  }

  uint8_t getParamValuePercent(uint8_t idx) const override {
    switch (idx) {
      case 0: return m_tapeAge;
      case 1: return m_flutterDepth;
      case 2: return m_dropoutRate * 2;
      case 3: return map(m_filterDrive, 0, 127, 0, 100);
      default: return 0;
    }
  }

private:
  uint8_t m_tapeAge;
  uint8_t m_flutterDepth;
  uint8_t m_dropoutRate;
  uint8_t m_filterDrive;
  uint8_t m_outChannel;

  uint32_t m_lastLfoUpdate;
  float m_lfoPhaseWow;
  float m_lfoPhaseFlutter;
  bool m_isTapeStopped;
  float m_tapeBrakeProgress;
};
