#pragma once
#include "engine_base.h"

struct FMSoundState {
  uint8_t cutoff;     // CC 74 (Brightness / Modulator output)
  uint8_t resonance;  // CC 71 (Harmonic intensity)
  uint8_t attack;     // CC 73 (Attack speed)
  uint8_t release;    // CC 72 (Release time)
  uint8_t reverb;     // CC 91 (Space / Wet mix)
};

class EngineMorph : public MidiEngine {
public:
  EngineMorph() 
    : m_morphPos(0), m_autoLfoSpeed(0), m_lfoPhase(0.0f), m_lastUpdate(0),
      m_outChannel(1) {
    // Sound State A: Warm, mellow electric piano / bell
    m_stateA = {45, 30, 20, 65, 40};
    // Sound State B: Bright, aggressive, metallic FM lead
    m_stateB = {115, 95, 5, 25, 80};
  }

  const char* getName() const override { return "FM TIMBRE MORPH"; }

  void init() override {
    m_lastUpdate = millis();
    applyMorph();
  }

  void update() override {
    uint32_t now = millis();
    float dt = (now - m_lastUpdate) / 1000.0f;
    if (dt < 0.02f) return; // 50 Hz control rate
    m_lastUpdate = now;

    // Auto-Morph LFO sweep
    if (m_autoLfoSpeed > 0) {
      float freq = (m_autoLfoSpeed / 100.0f) * 1.5f; // 0 to 1.5 Hz
      m_lfoPhase += freq * dt * 2.0f * PI;
      if (m_lfoPhase > 2.0f * PI) m_lfoPhase -= 2.0f * PI;

      // Triangle wave oscillation 0..100
      float tri = fabsf(fmodf(m_lfoPhase, 2.0f * PI) / PI - 1.0f);
      m_morphPos = (uint8_t)(tri * 100.0f);
      applyMorph();
    }
  }

  void onNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) override {
    emitNoteOn(m_outChannel, pitch, velocity);
  }

  void onNoteOff(uint8_t channel, uint8_t pitch) override {
    emitNoteOff(m_outChannel, pitch);
  }

  void onControlChange(uint8_t channel, uint8_t control, uint8_t value) override {
    // Pass external mod wheel or volume
    if (control == 1 || control == 7) {
      MidiIO::sendControlChange(m_outChannel, control, value);
    }
  }

  void onPitchBend(uint8_t channel, int16_t bend) override {
    MidiIO::sendPitchBend(m_outChannel, bend);
  }

  void onEncoderTurn(uint8_t encoderIndex, int delta) override {
    switch (encoderIndex) {
      case 0: // Manual Morph Position (0% = Sound A, 100% = Sound B)
        m_autoLfoSpeed = 0; // Disable auto-lfo on manual touch
        m_morphPos = constrain(m_morphPos + delta * 2, 0, 100);
        applyMorph();
        break;
      case 1: // Auto-Morph LFO Rate (0 - 100%)
        m_autoLfoSpeed = constrain(m_autoLfoSpeed + delta * 2, 0, 100);
        break;
      case 2: // Modulator Boost (Brightness scaling)
        m_stateB.cutoff = constrain(m_stateB.cutoff + delta * 2, 20, 127);
        applyMorph();
        break;
      case 3: // Reverb Mix
        m_stateA.reverb = constrain(m_stateA.reverb + delta * 2, 0, 127);
        m_stateB.reverb = m_stateA.reverb;
        applyMorph();
        break;
    }
  }

  void onEncoderClick(uint8_t encoderIndex) override {
    if (encoderIndex == 0) {
      m_morphPos = (m_morphPos < 50) ? 100 : 0; // Flip between A and B
      applyMorph();
    }
    if (encoderIndex == 1) m_autoLfoSpeed = 0; // Stop LFO
  }

  void onSwitch1(bool active) override {}

  void onSwitch2(bool active) override {
    // Switch 2: Snap to 100% Sound B while held/active
    m_morphPos = active ? 100 : 0;
    applyMorph();
  }

  void getParamName(uint8_t idx, char* buf, size_t maxLen) const override {
    const char* names[] = {"MORPH A-B", "AUTO LFO", "MOD BOOST", "REVERB"};
    if (idx < 4) strncpy(buf, names[idx], maxLen);
  }

  void getParamValueStr(uint8_t idx, char* buf, size_t maxLen) const override {
    switch (idx) {
      case 0: snprintf(buf, maxLen, "%d%% B", m_morphPos); break;
      case 1: 
        if (m_autoLfoSpeed == 0) snprintf(buf, maxLen, "Manual");
        else snprintf(buf, maxLen, "%d%%", m_autoLfoSpeed);
        break;
      case 2: snprintf(buf, maxLen, "%d", m_stateB.cutoff); break;
      case 3: snprintf(buf, maxLen, "%d", m_stateA.reverb); break;
    }
  }

  uint8_t getParamValuePercent(uint8_t idx) const override {
    switch (idx) {
      case 0: return m_morphPos;
      case 1: return m_autoLfoSpeed;
      case 2: return map(m_stateB.cutoff, 20, 127, 0, 100);
      case 3: return map(m_stateA.reverb, 0, 127, 0, 100);
      default: return 0;
    }
  }

private:
  uint8_t m_morphPos;
  uint8_t m_autoLfoSpeed;
  float m_lfoPhase;
  uint32_t m_lastUpdate;
  uint8_t m_outChannel;

  FMSoundState m_stateA;
  FMSoundState m_stateB;

  void applyMorph() {
    float t = m_morphPos / 100.0f;
    uint8_t curCutoff   = (uint8_t)(m_stateA.cutoff   + t * (m_stateB.cutoff   - m_stateA.cutoff));
    uint8_t curRes      = (uint8_t)(m_stateA.resonance+ t * (m_stateB.resonance- m_stateA.resonance));
    uint8_t curAttack   = (uint8_t)(m_stateA.attack   + t * (m_stateB.attack   - m_stateA.attack));
    uint8_t curRelease  = (uint8_t)(m_stateA.release  + t * (m_stateB.release  - m_stateA.release));
    uint8_t curReverb   = (uint8_t)(m_stateA.reverb   + t * (m_stateB.reverb   - m_stateA.reverb));

    MidiIO::sendControlChange(m_outChannel, 74, curCutoff);
    MidiIO::sendControlChange(m_outChannel, 71, curRes);
    MidiIO::sendControlChange(m_outChannel, 73, curAttack);
    MidiIO::sendControlChange(m_outChannel, 72, curRelease);
    MidiIO::sendControlChange(m_outChannel, 91, curReverb);
  }
};
