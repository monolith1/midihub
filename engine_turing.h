#pragma once
#include "engine_base.h"

class EngineTuring : public MidiEngine {
public:
  EngineTuring() 
    : m_mutation(15), m_loopLength(16), m_bpm(115), m_octaves(2),
      m_shiftReg(0x94B2), m_stepIndex(0), m_lastTick(0),
      m_activeNote(0), m_noteOffScheduled(0), m_isFrozen(false),
      m_outChannel(1), m_numScaleNotes(5) {
    // Default to C Minor Pentatonic
    m_scaleNotes[0] = 48; // C3
    m_scaleNotes[1] = 51; // Eb3
    m_scaleNotes[2] = 53; // F3
    m_scaleNotes[3] = 55; // G3
    m_scaleNotes[4] = 58; // Bb3
  }

  const char* getName() const override { return "TURING CO-PILOT"; }

  void init() override {
    m_lastTick = millis();
    m_shiftReg = 0xA5C3;
  }

  void update() override {
    uint32_t now = millis();

    // Check if we need to turn off the current note (gate length)
    if (m_activeNote > 0 && now >= m_noteOffScheduled) {
      emitNoteOff(m_outChannel, m_activeNote);
      m_activeNote = 0;
    }

    // Step clock calculation (16th notes: interval = (60000 / BPM) / 4)
    uint32_t stepInterval = (60000 / m_bpm) / 4;
    if (now - m_lastTick < stepInterval) return;
    m_lastTick = now;

    // --- Turing Shift Register Logic ---
    // Extract the oldest bit based on loop length (2 to 16)
    uint8_t loopBit = (m_shiftReg >> (m_loopLength - 1)) & 0x01;

    // Check mutation (if frozen, mutation is strictly 0%)
    uint8_t effectiveMutation = m_isFrozen ? 0 : m_mutation;
    uint8_t newBit = loopBit;
    if (effectiveMutation > 0 && (rand() % 100) < effectiveMutation) {
      newBit = loopBit ^ 1; // Flip bit!
    }

    // Shift register left and insert new bit at position 0
    m_shiftReg = ((m_shiftReg << 1) & 0xFFFF) | newBit;
    m_stepIndex = (m_stepIndex + 1) % m_loopLength;

    // Pick note from harmonic pool based on bottom 4 bits
    uint8_t noteSeed = m_shiftReg & 0x0F;
    uint8_t noteIndex = noteSeed % m_numScaleNotes;
    uint8_t octaveOffset = (noteSeed / m_numScaleNotes) % m_octaves;
    uint8_t pitch = m_scaleNotes[noteIndex] + (octaveOffset * 12);
    if (pitch > 120) pitch = 120;

    // Trigger Note down the layered pedalboard chain
    uint8_t velocity = 85 + (newBit ? 25 : 0); // Subtle accent on flipped bits
    emitNoteOn(m_outChannel, pitch, velocity);
    m_activeNote = pitch;
    m_noteOffScheduled = now + (stepInterval * 65) / 100; // 65% gate length
  }

  void onNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) override {
    // When the user plays keys on their keyboard, seed the harmonic scale!
    if (m_numScaleNotes >= 8) m_numScaleNotes = 0;
    m_scaleNotes[m_numScaleNotes++] = pitch % 12 + 48; // Normalize to base octave
    if (m_numScaleNotes == 1) {
      // If first note, clear out other slots to follow user chord
      for (int i = 1; i < 8; i++) m_scaleNotes[i] = m_scaleNotes[0];
    }
  }

  void onNoteOff(uint8_t channel, uint8_t pitch) override {}
  void onControlChange(uint8_t channel, uint8_t control, uint8_t value) override {}
  void onPitchBend(uint8_t channel, int16_t bend) override {
    MidiIO::sendPitchBend(m_outChannel, bend);
  }

  void onEncoderTurn(uint8_t encoderIndex, int delta) override {
    switch (encoderIndex) {
      case 0: // Mutation Probability (0 - 100%)
        m_mutation = constrain(m_mutation + delta * 2, 0, 100);
        break;
      case 1: // Loop Length (2 - 16 steps)
        m_loopLength = constrain(m_loopLength + delta, 2, 16);
        break;
      case 2: // BPM (40 - 220)
        m_bpm = constrain(m_bpm + delta * 2, 40, 220);
        break;
      case 3: // Octave Spread (1 - 3)
        m_octaves = constrain(m_octaves + delta, 1, 3);
        break;
    }
  }

  void onEncoderClick(uint8_t encoderIndex) override {
    if (encoderIndex == 0) m_mutation = (m_mutation == 0) ? 15 : 0; // Quick toggle freeze
    if (encoderIndex == 1) m_loopLength = 16;                       // Reset to 16
    if (encoderIndex == 2) m_bpm = 120;                            // Reset tempo
  }

  void onSwitch1(bool active) override {}

  void onSwitch2(bool active) override {
    // Switch 2: Dedicated hardware FREEZE latch!
    m_isFrozen = active;
  }

  uint32_t getBpm() const override { return m_bpm; }

  void getParamName(uint8_t idx, char* buf, size_t maxLen) const override {
    const char* names[] = {"MUTATION", "LENGTH", "TEMPO", "OCTAVES"};
    if (idx < 4) strncpy(buf, names[idx], maxLen);
  }

  void getParamValueStr(uint8_t idx, char* buf, size_t maxLen) const override {
    switch (idx) {
      case 0: 
        if (m_isFrozen) snprintf(buf, maxLen, "LOCKED");
        else snprintf(buf, maxLen, "%d%%", m_mutation); 
        break;
      case 1: snprintf(buf, maxLen, "%d steps", m_loopLength); break;
      case 2: snprintf(buf, maxLen, "%d BPM", m_bpm); break;
      case 3: snprintf(buf, maxLen, "%d Oct", m_octaves); break;
    }
  }

  uint8_t getParamValuePercent(uint8_t idx) const override {
    switch (idx) {
      case 0: return m_isFrozen ? 0 : m_mutation;
      case 1: return map(m_loopLength, 2, 16, 0, 100);
      case 2: return map(m_bpm, 40, 220, 0, 100);
      case 3: return map(m_octaves, 1, 3, 0, 100);
      default: return 0;
    }
  }

  // Visualizer hook: returns the 16-step shift register state for E-Paper display
  uint16_t getShiftRegister() const { return m_shiftReg; }
  uint8_t getCurrentStep() const { return m_stepIndex; }

private:
  uint8_t m_mutation;
  uint8_t m_loopLength;
  uint32_t m_bpm;
  uint8_t m_octaves;
  uint16_t m_shiftReg;
  uint8_t m_stepIndex;
  uint32_t m_lastTick;
  uint8_t m_activeNote;
  uint32_t m_noteOffScheduled;
  bool m_isFrozen;
  uint8_t m_outChannel;

  uint8_t m_scaleNotes[8];
  uint8_t m_numScaleNotes;
};
