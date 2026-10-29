#pragma once
#include "engine_base.h"

struct BouncingBall {
  bool active;
  uint8_t pitch;
  uint8_t velocity;
  uint32_t nextBounceTime;
  uint32_t currentIntervalMs;
  uint32_t noteOffTime;
};

class EnginePhysics : public MidiEngine {
public:
  EnginePhysics() 
    : m_gravity(60), m_elasticity(75), m_strumSpread(25), m_bounceMode(0),
      m_infiniteLatch(false), m_outChannel(1) {
    for (int i = 0; i < MAX_BALLS; i++) {
      m_balls[i].active = false;
    }
  }

  const char* getName() const override { return "PHYSICS & BOUNCE"; }

  void init() override {
    for (int i = 0; i < MAX_BALLS; i++) {
      m_balls[i].active = false;
    }
  }

  void update() override {
    uint32_t now = millis();

    for (int i = 0; i < MAX_BALLS; i++) {
      if (!m_balls[i].active) continue;

      // Note Off handling
      if (m_balls[i].noteOffTime > 0 && now >= m_balls[i].noteOffTime) {
        emitNoteOff(m_outChannel, m_balls[i].pitch);
        m_balls[i].noteOffTime = 0;
      }

      // Next Bounce handling
      if (now >= m_balls[i].nextBounceTime) {
        // Trigger Note On down the layered pedalboard chain
        emitNoteOn(m_outChannel, m_balls[i].pitch, m_balls[i].velocity);
        uint32_t gateTime = (m_balls[i].currentIntervalMs * 60) / 100;
        if (gateTime < 20) gateTime = 20;
        m_balls[i].noteOffTime = now + gateTime;

        // Apply physical dissipation
        if (!m_infiniteLatch) {
          float elasticityFactor = m_elasticity / 100.0f;
          m_balls[i].velocity = (uint8_t)(m_balls[i].velocity * elasticityFactor);
          m_balls[i].currentIntervalMs = (uint32_t)(m_balls[i].currentIntervalMs * sqrtf(elasticityFactor));
        }

        // Check if ball has lost enough kinetic energy to stop
        if (m_balls[i].velocity < 15 || m_balls[i].currentIntervalMs < 15) {
          m_balls[i].active = false;
        } else {
          m_balls[i].nextBounceTime = now + m_balls[i].currentIntervalMs;
        }
      }
    }
  }

  void onNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) override {
    // Find an empty ball slot
    int slot = -1;
    for (int i = 0; i < MAX_BALLS; i++) {
      if (!m_balls[i].active) {
        slot = i;
        break;
      }
    }
    if (slot == -1) slot = 0; // Steal oldest ball

    // Calculate initial drop height & bounce interval from gravity
    uint32_t baseInterval = map(m_gravity, 1, 100, 700, 100);
    uint32_t now = millis();

    m_balls[slot].active = true;
    m_balls[slot].pitch = pitch;
    m_balls[slot].velocity = velocity;
    m_balls[slot].currentIntervalMs = baseInterval;
    m_balls[slot].nextBounceTime = now + (m_strumSpread * slot); // Strum stagger
    m_balls[slot].noteOffTime = 0;
  }

  void onNoteOff(uint8_t channel, uint8_t pitch) override {
    // If not in infinite latch, notes decay naturally by physics
  }

  void onControlChange(uint8_t channel, uint8_t control, uint8_t value) override {
    MidiIO::sendControlChange(m_outChannel, control, value);
  }

  void onPitchBend(uint8_t channel, int16_t bend) override {
    MidiIO::sendPitchBend(m_outChannel, bend);
  }

  void onEncoderTurn(uint8_t encoderIndex, int delta) override {
    switch (encoderIndex) {
      case 0: // Gravity (1 - 100%)
        m_gravity = constrain(m_gravity + delta * 2, 1, 100);
        break;
      case 1: // Elasticity / Bounciness (20 - 95%)
        m_elasticity = constrain(m_elasticity + delta * 2, 20, 95);
        break;
      case 2: // Strum Flam Spread (0 - 150 ms)
        m_strumSpread = constrain(m_strumSpread + delta * 5, 0, 150);
        break;
      case 3: // Mode (0: Standard, 1: Octave Cascade)
        m_bounceMode = (m_bounceMode + delta + 2) % 2;
        break;
    }
  }

  void onEncoderClick(uint8_t encoderIndex) override {
    if (encoderIndex == 0) m_gravity = 60;
    if (encoderIndex == 1) m_elasticity = 75;
    if (encoderIndex == 2) m_strumSpread = 0;
  }

  void onSwitch1(bool active) override {}

  void onSwitch2(bool active) override {
    // Switch 2: Infinite Ball Latch!
    m_infiniteLatch = active;
  }

  void getParamName(uint8_t idx, char* buf, size_t maxLen) const override {
    const char* names[] = {"GRAVITY", "ELASTICITY", "STRUM FLAM", "TRAJECTORY"};
    if (idx < 4) strncpy(buf, names[idx], maxLen);
  }

  void getParamValueStr(uint8_t idx, char* buf, size_t maxLen) const override {
    switch (idx) {
      case 0: snprintf(buf, maxLen, "%d%%", m_gravity); break;
      case 1: 
        if (m_infiniteLatch) snprintf(buf, maxLen, "LATCHED");
        else snprintf(buf, maxLen, "%d%%", m_elasticity);
        break;
      case 2: snprintf(buf, maxLen, "%d ms", m_strumSpread); break;
      case 3: snprintf(buf, maxLen, (m_bounceMode == 0) ? "Direct" : "Cascade"); break;
    }
  }

  uint8_t getParamValuePercent(uint8_t idx) const override {
    switch (idx) {
      case 0: return m_gravity;
      case 1: return m_infiniteLatch ? 100 : map(m_elasticity, 20, 95, 0, 100);
      case 2: return map(m_strumSpread, 0, 150, 0, 100);
      case 3: return m_bounceMode * 100;
      default: return 0;
    }
  }

private:
  static const int MAX_BALLS = 6;
  BouncingBall m_balls[MAX_BALLS];

  uint8_t m_gravity;
  uint8_t m_elasticity;
  uint8_t m_strumSpread;
  uint8_t m_bounceMode;
  bool m_infiniteLatch;
  uint8_t m_outChannel;
};
