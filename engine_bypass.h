#pragma once
#include "engine_base.h"

class EngineBypass : public MidiEngine {
public:
  EngineBypass() 
    : m_transpose(0), m_velCurve(0), m_outChannel(1), m_volume(100) {}

  const char* getName() const override { return "DIRECT BYPASS"; }

  void init() override {}

  void update() override {}

  void onNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) override {
    int transposed = (int)pitch + m_transpose;
    if (transposed < 0) transposed = 0;
    if (transposed > 127) transposed = 127;

    uint8_t scaledVel = applyVelocityCurve(velocity);
    emitNoteOn(m_outChannel, (uint8_t)transposed, scaledVel);
  }

  void onNoteOff(uint8_t channel, uint8_t pitch) override {
    int transposed = (int)pitch + m_transpose;
    if (transposed < 0) transposed = 0;
    if (transposed > 127) transposed = 127;

    emitNoteOff(m_outChannel, (uint8_t)transposed);
  }

  void onControlChange(uint8_t channel, uint8_t control, uint8_t value) override {
    MidiIO::sendControlChange(m_outChannel, control, value);
  }

  void onPitchBend(uint8_t channel, int16_t bend) override {
    MidiIO::sendPitchBend(m_outChannel, bend);
  }

  void onEncoderTurn(uint8_t encoderIndex, int delta) override {
    switch (encoderIndex) {
      case 0: // Transpose
        m_transpose = constrain(m_transpose + delta, -24, 24);
        break;
      case 1: // Velocity Curve (0: Linear, 1: Soft, 2: Hard, 3: Fixed 127)
        m_velCurve = (m_velCurve + delta + 4) % 4;
        break;
      case 2: // Output Channel (1 - 16)
        m_outChannel = constrain(m_outChannel + delta, 1, 16);
        break;
      case 3: // Master Volume CC 7
        m_volume = constrain(m_volume + delta * 2, 0, 127);
        MidiIO::sendControlChange(m_outChannel, 7, m_volume);
        break;
    }
  }

  void onEncoderClick(uint8_t encoderIndex) override {
    if (encoderIndex == 0) m_transpose = 0; // Reset transpose to 0
    if (encoderIndex == 3) {
      m_volume = 100;
      MidiIO::sendControlChange(m_outChannel, 7, m_volume);
    }
  }

  void onSwitch1(bool active) override {}
  void onSwitch2(bool active) override {
    if (active) MidiIO::panic(); // Panic / All notes off on click
  }

  void getParamName(uint8_t idx, char* buf, size_t maxLen) const override {
    const char* names[] = {"TRANSPOSE", "VEL CURVE", "OUT CHAN", "VOLUME"};
    if (idx < 4) strncpy(buf, names[idx], maxLen);
  }

  void getParamValueStr(uint8_t idx, char* buf, size_t maxLen) const override {
    switch (idx) {
      case 0: snprintf(buf, maxLen, "%+d st", m_transpose); break;
      case 1: {
        const char* curves[] = {"Linear", "Soft", "Hard", "Fixed"};
        strncpy(buf, curves[m_velCurve], maxLen);
        break;
      }
      case 2: snprintf(buf, maxLen, "Ch %d", m_outChannel); break;
      case 3: snprintf(buf, maxLen, "%d", m_volume); break;
    }
  }

  uint8_t getParamValuePercent(uint8_t idx) const override {
    switch (idx) {
      case 0: return map(m_transpose, -24, 24, 0, 100);
      case 1: return m_velCurve * 33;
      case 2: return map(m_outChannel, 1, 16, 0, 100);
      case 3: return map(m_volume, 0, 127, 0, 100);
      default: return 0;
    }
  }

private:
  int8_t m_transpose;
  uint8_t m_velCurve;
  uint8_t m_outChannel;
  uint8_t m_volume;

  uint8_t applyVelocityCurve(uint8_t raw) const {
    if (raw == 0) return 0;
    switch (m_velCurve) {
      case 1: { // Soft (boosts lower velocities)
        float normalized = raw / 127.0f;
        return (uint8_t)(sqrtf(normalized) * 127.0f);
      }
      case 2: { // Hard (requires hitting harder)
        float normalized = raw / 127.0f;
        return (uint8_t)(normalized * normalized * 127.0f);
      }
      case 3: // Fixed 127
        return 127;
      default: // Linear
        return raw;
    }
  }
};
