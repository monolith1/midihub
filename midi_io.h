#pragma once
#include <Arduino.h>
#include "config.h"

// Callback function signatures for incoming MIDI events
typedef void (*NoteOnCallback)(uint8_t channel, uint8_t pitch, uint8_t velocity);
typedef void (*NoteOffCallback)(uint8_t channel, uint8_t pitch);
typedef void (*ControlChangeCallback)(uint8_t channel, uint8_t control, uint8_t value);
typedef void (*PitchBendCallback)(uint8_t channel, int16_t value);

// Global Patchbay & CC Routing Configuration (persisted to NVS Flash)
struct PatchbayConfig {
  uint8_t inPort;        // MidiInputPort enum
  uint8_t inChannel;     // 0 = Omni (All channels), 1..16 = specific channel
  uint8_t outPort;       // MidiOutputPort enum
  uint8_t outChannel;    // 1..16 = target synth MIDI channel
  uint8_t ccMap[128];    // ccMap[srcCC] = dstCC (default: identity 0..127)
};

class MidiIO {
public:
  static void init();
  static void update();

  // --- Multi-Port MIDI Output Methods (routed via Patchbay Config) ---
  static void sendNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity);
  static void sendNoteOff(uint8_t channel, uint8_t pitch);
  static void sendControlChange(uint8_t channel, uint8_t control, uint8_t value);
  static void sendPitchBend(uint8_t channel, int16_t bend); // Range: -8192 to +8191 (0 = center)
  static void setTuningBendOffset(uint8_t channel, int16_t tuningBend);
  static void setLfoBendOffset(uint8_t channel, int16_t lfoBend);
  static void sendProgramChange(uint8_t channel, uint8_t program);
  static void sendSysEx(const uint8_t* data, size_t length);
  static void panic(); // All Notes Off on all channels & ports

  // --- Incoming MIDI Event Callbacks (to Active Sound Engine) ---
  static void setNoteOnCallback(NoteOnCallback cb);
  static void setNoteOffCallback(NoteOffCallback cb);
  static void setControlChangeCallback(ControlChangeCallback cb);
  static void setPitchBendCallback(PitchBendCallback cb);

  // --- Output Monitor Tap (so Visualizer reacts to generated notes too!) ---
  static void setOutputMonitorCallbacks(NoteOnCallback noteOnTap, NoteOffCallback noteOffTap);

  // --- Patchbay & CC Learn Management ---
  static PatchbayConfig& getPatchbayConfig();
  static void resetPatchbayDefaults();
  static uint8_t getLastLearnedCC();
  static uint8_t getLastActiveNote();
  static uint8_t getLastActiveVelocity();

  // --- Connected Device Discovery Metadata ---
  static const char* getConnectedDeviceName();
  static const char* getUsbDeviceName(uint8_t slot);
  static uint8_t getConnectedUsbCount();
  static bool isBleConnected();

  // --- Virtual Dispatch (for bench testing & internal routing) ---
  static void dispatchIncomingNoteOn(uint8_t srcPort, uint8_t channel, uint8_t pitch, uint8_t velocity);
  static void dispatchIncomingNoteOff(uint8_t srcPort, uint8_t channel, uint8_t pitch);
  static void dispatchIncomingCC(uint8_t srcPort, uint8_t channel, uint8_t control, uint8_t value);
  static void dispatchIncomingPitchBend(uint8_t srcPort, uint8_t channel, int16_t bend);

  static void triggerVirtualNoteOn(uint8_t pitch, uint8_t velocity);
  static void triggerVirtualNoteOff(uint8_t pitch);

private:
  static HardwareSerial s_midiSerial;
  static NoteOnCallback s_noteOnCb;
  static NoteOffCallback s_noteOffCb;
  static ControlChangeCallback s_ccCb;
  static PitchBendCallback s_pitchBendCb;

  static NoteOnCallback s_outTapNoteOnCb;
  static NoteOffCallback s_outTapNoteOffCb;

  static PatchbayConfig s_patchbay;
  static uint8_t s_lastLearnedCC;
  static uint8_t s_lastActiveNote;
  static uint8_t s_lastActiveVel;

  static bool shouldAcceptInput(uint8_t srcPort, uint8_t& channel);
  static uint8_t resolveOutputChannel(uint8_t requestedChannel);
  static void sendUsbMidiPacket(uint8_t cin, uint8_t b0, uint8_t b1, uint8_t b2);
  static void sendBleMidiPacket(const uint8_t* midiBytes, size_t len);
};
