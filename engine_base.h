#pragma once
#include <Arduino.h>
#include "midi_io.h"

class MidiEngine {
public:
  MidiEngine() : m_layerActive(false), m_nextInChain(nullptr) {}
  virtual ~MidiEngine() {}

  // Engine Identification
  virtual const char* getName() const = 0;
  virtual char getShortCode() const { return getName()[0]; }

  // Multi-Engine Pedalboard Layering State
  bool isLayerActive() const { return m_layerActive; }
  void setLayerActive(bool active) { m_layerActive = active; }
  void toggleLayerActive() { m_layerActive = !m_layerActive; }
  void setNextInChain(MidiEngine* next) { m_nextInChain = next; }
  MidiEngine* getNextInChain() const { return m_nextInChain; }

  // Emit Note Down the Layered Pedalboard Chain (or to MidiIO at end of chain)
  void emitNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) {
    MidiEngine* curr = m_nextInChain;
    while (curr) {
      if (curr->isLayerActive()) {
        curr->onNoteOn(channel, pitch, velocity);
        return;
      }
      curr = curr->getNextInChain();
    }
    MidiIO::sendNoteOn(channel, pitch, velocity);
  }

  void emitNoteOff(uint8_t channel, uint8_t pitch) {
    MidiEngine* curr = m_nextInChain;
    while (curr) {
      if (curr->isLayerActive()) {
        curr->onNoteOff(channel, pitch);
        return;
      }
      curr = curr->getNextInChain();
    }
    MidiIO::sendNoteOff(channel, pitch);
  }

  // Lifecycle
  virtual void init() = 0;
  virtual void update() = 0;

  // MIDI Event Processors
  virtual void onNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) = 0;
  virtual void onNoteOff(uint8_t channel, uint8_t pitch) = 0;
  virtual void onControlChange(uint8_t channel, uint8_t control, uint8_t value) = 0;
  virtual void onPitchBend(uint8_t channel, int16_t bend) = 0;

  // Physical Controls Handlers
  virtual void onEncoderTurn(uint8_t encoderIndex, int delta) = 0;
  virtual void onEncoderClick(uint8_t encoderIndex) = 0;
  virtual void onSwitch1(bool active) = 0;
  virtual void onSwitch2(bool active) = 0;

  // Display Introspection (for E-Paper parameter columns 0..3)
  virtual void getParamName(uint8_t encoderIndex, char* buf, size_t maxLen) const = 0;
  virtual void getParamValueStr(uint8_t encoderIndex, char* buf, size_t maxLen) const = 0;
  virtual uint8_t getParamValuePercent(uint8_t encoderIndex) const = 0; // 0 to 100%

  // Current Engine Tempo (BPM)
  virtual uint32_t getBpm() const { return 120; }

protected:
  bool m_layerActive;
  MidiEngine* m_nextInChain;
};
