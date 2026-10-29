#pragma once
#include "engine_base.h"

enum TuningSystem {
  TUNE_JUST_INTONATION = 0, // Mathematical pure harmonic ratios (zero beating in FM)
  TUNE_BOHLEN_PIERCE,       // 13-division tritave (futuristic alien FM)
  TUNE_19_EDO,              // 19 Equal Divisions of the Octave
  TUNE_ARABIC_MAQAM,        // Middle Eastern quarter-tones (Bayati / Rast)
  TUNE_PYTHAGOREAN,         // Pure stacked fifths
  NUM_TUNING_SYSTEMS
};

class EngineTuning : public MidiEngine {
public:
  EngineTuning() 
    : m_tuningSystem(TUNE_JUST_INTONATION), m_rootKey(0), m_autoRoot(true),
      m_depth(100), m_masterCents(0), m_outChannel(1), m_activeNotesMask(0) {}

  const char* getName() const override { return "MICROTONAL ENGINE"; }

  void init() override {}
  void update() override {}

  void onNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) override {
    uint8_t noteClass = pitch % 12;
    m_activeNotesMask |= (1 << noteClass);

    // Dynamic Root Detection: If auto-root is enabled, use lowest played note
    if (m_autoRoot) {
      m_rootKey = noteClass;
    }

    // Calculate microtonal pitch bend offset for this note
    int16_t bend = calculateBend(pitch);
    
    // Apply tuning bend offset (combines additively with Lo-Fi tape LFO if layered!)
    MidiIO::setTuningBendOffset(m_outChannel, bend);
    emitNoteOn(m_outChannel, pitch, velocity);
  }

  void onNoteOff(uint8_t channel, uint8_t pitch) override {
    uint8_t noteClass = pitch % 12;
    m_activeNotesMask &= ~(1 << noteClass);
    emitNoteOff(m_outChannel, pitch);
  }

  void onControlChange(uint8_t channel, uint8_t control, uint8_t value) override {
    MidiIO::sendControlChange(m_outChannel, control, value);
  }

  void onPitchBend(uint8_t channel, int16_t bend) override {
    // Adds to microtonal offset
  }

  void onEncoderTurn(uint8_t encoderIndex, int delta) override {
    switch (encoderIndex) {
      case 0: // Tuning System
        m_tuningSystem = (m_tuningSystem + delta + NUM_TUNING_SYSTEMS) % NUM_TUNING_SYSTEMS;
        break;
      case 1: // Root Key (0: Auto, 1-12: C, C#, D...)
        if (m_autoRoot && delta != 0) {
          m_autoRoot = false;
          m_rootKey = 0; // Lock to C
        } else {
          int next = (int)m_rootKey + delta;
          if (next < 0) { m_autoRoot = true; m_rootKey = 0; }
          else if (next > 11) { m_autoRoot = true; m_rootKey = 0; }
          else { m_rootKey = next; }
        }
        break;
      case 2: // Tuning Depth (0 - 100%)
        m_depth = constrain(m_depth + delta * 5, 0, 100);
        break;
      case 3: // Master Cents Offset (-50 to +50 cents)
        m_masterCents = constrain(m_masterCents + delta, -50, 50);
        break;
    }
  }

  void onEncoderClick(uint8_t encoderIndex) override {
    if (encoderIndex == 0) m_tuningSystem = TUNE_JUST_INTONATION;
    if (encoderIndex == 1) m_autoRoot = true;
    if (encoderIndex == 2) m_depth = 100;
    if (encoderIndex == 3) m_masterCents = 0;
  }

  void onSwitch1(bool active) override {}
  void onSwitch2(bool active) override {
    if (active) m_depth = 0; // Momentary bypass: snap to standard 12-TET
    else m_depth = 100;
  }

  void getParamName(uint8_t idx, char* buf, size_t maxLen) const override {
    const char* names[] = {"SCALE/SYS", "ROOT KEY", "STRENGTH", "OFFSET"};
    if (idx < 4) strncpy(buf, names[idx], maxLen);
  }

  void getParamValueStr(uint8_t idx, char* buf, size_t maxLen) const override {
    switch (idx) {
      case 0: {
        const char* sysNames[] = {"Just Inton", "Bohlen-P", "19-EDO", "Maqam Rast", "Pythagoras"};
        strncpy(buf, sysNames[m_tuningSystem], maxLen);
        break;
      }
      case 1: {
        if (m_autoRoot) {
          strncpy(buf, "Auto", maxLen);
        } else {
          const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
          strncpy(buf, noteNames[m_rootKey], maxLen);
        }
        break;
      }
      case 2: snprintf(buf, maxLen, "%d%%", m_depth); break;
      case 3: snprintf(buf, maxLen, "%+d ct", m_masterCents); break;
    }
  }

  uint8_t getParamValuePercent(uint8_t idx) const override {
    switch (idx) {
      case 0: return map(m_tuningSystem, 0, NUM_TUNING_SYSTEMS - 1, 0, 100);
      case 1: return m_autoRoot ? 0 : map(m_rootKey, 0, 11, 0, 100);
      case 2: return m_depth;
      case 3: return map(m_masterCents, -50, 50, 0, 100);
      default: return 0;
    }
  }

private:
  uint8_t m_tuningSystem;
  uint8_t m_rootKey;
  bool m_autoRoot;
  uint8_t m_depth;
  int8_t m_masterCents;
  uint8_t m_outChannel;
  uint16_t m_activeNotesMask;

  // Cents offsets relative to 12-TET for Just Intonation:
  // Root, m2, M2, m3, M3, P4, d5, P5, m6, M6, m7, M7
  // e.g. Major 3rd (5:4) is -13.7 cents; Perfect 5th (3:2) is +2.0 cents
  const float JUST_CENTS[12] = {
    0.0f,     // Root (1:1)
    +11.7f,   // minor 2nd (16:15)
    +3.9f,    // Major 2nd (9:8)
    +15.6f,   // minor 3rd (6:5)
    -13.7f,   // Major 3rd (5:4)
    -2.0f,    // Perfect 4th (4:3)
    -17.5f,   // Tritone (45:32)
    +2.0f,    // Perfect 5th (3:2)
    +13.7f,   // minor 6th (8:5)
    -15.6f,   // Major 6th (5:3)
    -3.9f,    // minor 7th (9:5)
    -11.7f    // Major 7th (15:8)
  };

  int16_t calculateBend(uint8_t pitch) const {
    uint8_t interval = (pitch + 12 - m_rootKey) % 12;
    float centsOffset = 0.0f;

    switch (m_tuningSystem) {
      case TUNE_JUST_INTONATION:
        centsOffset = JUST_CENTS[interval];
        break;
      case TUNE_ARABIC_MAQAM:
        // Rast quarter-tone: 3rd degree (E) and 7th degree (B) lowered by 50 cents
        if (interval == 4 || interval == 11) centsOffset = -50.0f;
        break;
      case TUNE_19_EDO:
        centsOffset = ((interval * 19.0f / 12.0f) - interval) * 100.0f;
        break;
      case TUNE_BOHLEN_PIERCE:
        centsOffset = (interval % 3 == 0) ? -28.0f : +35.0f;
        break;
      case TUNE_PYTHAGOREAN:
        centsOffset = (interval == 4 || interval == 9 || interval == 11) ? +8.0f : -4.0f;
        break;
    }

    // Apply depth and master cents
    float finalCents = (centsOffset * (m_depth / 100.0f)) + m_masterCents;

    // Convert cents to 14-bit pitch bend:
    // In standard 2-semitone pitch bend range, 1 semitone (100 cents) = 4096 units.
    // So 1 cent ≈ 40.96 units.
    int16_t bendUnits = (int16_t)(finalCents * 40.96f);
    return constrain(bendUnits, -8192, 8191);
  }
};
