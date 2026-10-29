#pragma once
#include "engine_base.h"

enum VisualStyle : uint8_t {
  VIS_CONSTELLATION = 0,  // Circle of Fifths 12-node chord geometry
  VIS_SPIROGRAPH,         // Algorithmic Lissajous / Spiro-Ink curves
  VIS_CELLULAR,           // Conway's Game of Life seeded by MIDI notes
  VIS_RIBBON,             // Scrolling mechanical player-piano ribbon
  NUM_VIS_STYLES
};

struct NoteHistoryEntry {
  uint8_t pitch;
  uint8_t velocity;
};

class EngineVisualizer : public MidiEngine {
public:
  EngineVisualizer()
    : m_style(VIS_CONSTELLATION), m_persistence(75), m_density(2),
      m_inverted(false), m_isFrozen(false), m_activeNotesMask(0),
      m_lastPitch(60), m_lastVel(100), m_historyHead(0),
      m_bgSoundEngine(nullptr) {
    memset(m_history, 0, sizeof(m_history));
    memset(m_lifeGrid, 0, sizeof(m_lifeGrid));
    // Seed initial cellular pattern
    m_lifeGrid[7] = 0x0700;
    m_lifeGrid[8] = 0x0100;
    m_lifeGrid[9] = 0x0200;
  }

  const char* getName() const override { return "VISUAL CANVAS"; }

  void setBackgroundSoundEngine(MidiEngine* soundEngine) {
    m_bgSoundEngine = soundEngine;
  }

  MidiEngine* getBackgroundSoundEngine() const {
    return m_bgSoundEngine;
  }

  void init() override {}

  void update() override {
    // Keep the active sound engine (Lo-Fi, Turing, Morph, Physics, etc.) running in the background!
    if (m_bgSoundEngine && m_bgSoundEngine != this) {
      m_bgSoundEngine->update();
    }
  }

  // Delegate incoming performance events to the background sound engine
  void onNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) override {
    if (m_bgSoundEngine && m_bgSoundEngine != this) {
      m_bgSoundEngine->onNoteOn(channel, pitch, velocity);
    } else {
      MidiIO::sendNoteOn(channel, pitch, velocity);
    }
  }

  void onNoteOff(uint8_t channel, uint8_t pitch) override {
    if (m_bgSoundEngine && m_bgSoundEngine != this) {
      m_bgSoundEngine->onNoteOff(channel, pitch);
    } else {
      MidiIO::sendNoteOff(channel, pitch);
    }
  }

  void onControlChange(uint8_t channel, uint8_t control, uint8_t value) override {
    if (m_bgSoundEngine && m_bgSoundEngine != this) {
      m_bgSoundEngine->onControlChange(channel, control, value);
    } else {
      MidiIO::sendControlChange(channel, control, value);
    }
  }

  void onPitchBend(uint8_t channel, int16_t bend) override {
    if (m_bgSoundEngine && m_bgSoundEngine != this) {
      m_bgSoundEngine->onPitchBend(channel, bend);
    } else {
      MidiIO::sendPitchBend(channel, bend);
    }
  }

  // Called automatically by MidiIO output tap whenever any note is emitted (even generative notes!)
  void recordOutputNoteOn(uint8_t pitch, uint8_t velocity) {
    if (m_isFrozen) return;

    m_lastPitch = pitch;
    m_lastVel = velocity;
    m_activeNotesMask |= (1 << (pitch % 12));

    // Append to ribbon / spirograph ring buffer
    m_history[m_historyHead].pitch = pitch;
    m_history[m_historyHead].velocity = velocity;
    m_historyHead = (m_historyHead + 1) % HISTORY_LEN;

    // Seed Cellular Automaton grid with note coordinates & step 1 generation
    uint8_t gx = (pitch * 3) % 16;
    uint8_t gy = (velocity / 8) % 16;
    m_lifeGrid[gy] |= (1 << gx);
    m_lifeGrid[(gy + 1) % 16] |= (1 << ((gx + 1) % 16));
    m_lifeGrid[(gy + 15) % 16] |= (1 << gx);
    stepGameOfLife();
  }

  void recordOutputNoteOff(uint8_t pitch) {
    if (m_isFrozen) return;
    m_activeNotesMask &= ~(1 << (pitch % 12));
  }

  void onEncoderTurn(uint8_t encoderIndex, int delta) override {
    switch (encoderIndex) {
      case 0: // Visual Art Style
        m_style = (m_style + delta + NUM_VIS_STYLES) % NUM_VIS_STYLES;
        break;
      case 1: // Trail / Decay Persistence (10 - 100%)
        m_persistence = constrain(m_persistence + delta * 5, 10, 100);
        break;
      case 2: // Line Density / Complexity (1 - 4)
        m_density = constrain(m_density + delta, 1, 4);
        break;
      case 3: // Screen Theme (Light vs Dark Ink)
        if (delta != 0) m_inverted = !m_inverted;
        break;
    }
  }

  void onEncoderClick(uint8_t encoderIndex) override {
    if (encoderIndex == 0) m_style = VIS_CONSTELLATION;
    if (encoderIndex == 1) {
      // Clear history / canvas on click
      memset(m_history, 0, sizeof(m_history));
      memset(m_lifeGrid, 0, sizeof(m_lifeGrid));
      m_activeNotesMask = 0;
    }
    if (encoderIndex == 3) m_inverted = !m_inverted;
  }

  void onSwitch1(bool active) override {}

  void onSwitch2(bool active) override {
    // Switch 2: Freeze & Etch Artwork on E-Paper
    m_isFrozen = active;
  }

  uint32_t getBpm() const override {
    if (m_bgSoundEngine && m_bgSoundEngine != this) {
      return m_bgSoundEngine->getBpm();
    }
    return 120;
  }

  void getParamName(uint8_t idx, char* buf, size_t maxLen) const override {
    const char* names[] = {"ART STYLE", "TRAIL/DECAY", "COMPLEXITY", "INK THEME"};
    if (idx < 4) strncpy(buf, names[idx], maxLen);
  }

  void getParamValueStr(uint8_t idx, char* buf, size_t maxLen) const override {
    switch (idx) {
      case 0: {
        const char* styles[] = {"Constellate", "Spiro-Ink", "Cellular", "Piano Roll"};
        strncpy(buf, styles[m_style], maxLen);
        break;
      }
      case 1:
        if (m_isFrozen) strncpy(buf, "FROZEN", maxLen);
        else snprintf(buf, maxLen, "%d%%", m_persistence);
        break;
      case 2: snprintf(buf, maxLen, "Lvl %d", m_density); break;
      case 3: strncpy(buf, m_inverted ? "Dark Ink" : "Light Ink", maxLen); break;
    }
  }

  uint8_t getParamValuePercent(uint8_t idx) const override {
    switch (idx) {
      case 0: return map(m_style, 0, NUM_VIS_STYLES - 1, 0, 100);
      case 1: return m_isFrozen ? 100 : m_persistence;
      case 2: return map(m_density, 1, 4, 0, 100);
      case 3: return m_inverted ? 100 : 0;
      default: return 0;
    }
  }

  // Getters for E-Paper Renderer
  uint8_t getStyle() const { return m_style; }
  uint8_t getPersistence() const { return m_persistence; }
  uint8_t getDensity() const { return m_density; }
  uint16_t getNotesMask() const { return m_activeNotesMask; }
  uint8_t getLastPitch() const { return m_lastPitch; }
  uint8_t getLastVel() const { return m_lastVel; }
  bool isInverted() const { return m_inverted; }
  bool isFrozen() const { return m_isFrozen; }

  static const uint8_t HISTORY_LEN = 16;
  const NoteHistoryEntry& getHistoryEntry(uint8_t age) const {
    uint8_t idx = (m_historyHead + HISTORY_LEN - 1 - (age % HISTORY_LEN)) % HISTORY_LEN;
    return m_history[idx];
  }
  uint16_t getLifeRow(uint8_t row) const { return m_lifeGrid[row & 0x0F]; }

private:
  uint8_t m_style;
  uint8_t m_persistence;
  uint8_t m_density;
  bool m_inverted;
  bool m_isFrozen;
  uint16_t m_activeNotesMask;
  uint8_t m_lastPitch;
  uint8_t m_lastVel;

  NoteHistoryEntry m_history[HISTORY_LEN];
  uint8_t m_historyHead;
  uint16_t m_lifeGrid[16];
  MidiEngine* m_bgSoundEngine;

  void stepGameOfLife() {
    uint16_t nextGrid[16] = {0};
    for (int y = 0; y < 16; y++) {
      for (int x = 0; x < 16; x++) {
        int neighbors = 0;
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int ny = (y + dy + 16) % 16;
            int nx = (x + dx + 16) % 16;
            if (m_lifeGrid[ny] & (1 << nx)) neighbors++;
          }
        }
        bool alive = (m_lifeGrid[y] & (1 << x)) != 0;
        if ((alive && (neighbors == 2 || neighbors == 3)) || (!alive && neighbors == 3)) {
          nextGrid[y] |= (1 << x);
        }
      }
    }
    memcpy(m_lifeGrid, nextGrid, sizeof(m_lifeGrid));
  }
};
