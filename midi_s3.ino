/*
 * ==============================================================================
 * ESP32-S3 Universal MIDI Brain — Main Firmware
 * ==============================================================================
 * 
 * Hardware:
 *   - ESP32-S3 Dual USB Dev Board
 *   - Native USB Host Port ──► USB MIDI Keyboard / Hub / USB Synths
 *   - UART TX (GPIO 17)    ──► 3.5mm TRS Type-A MIDI OUT (to M-VAVE FM-1)
 *   - BLE Wireless MIDI    ──► Wireless Pedals / iPad / Phones / Synths
 *   - 4 Rotary Encoders    ──► Mode Selection & 4-Column Parameter Control
 *   - 2 Toggle Switches    ──► SW1 (Master Bypass), SW2 (Engine Latch / Freeze)
 *   - 3 Status LEDs        ──► SW1 LED, SW2 LED, Power / Tempo Heartbeat
 *   - 1.54" E-Paper SPI    ──► Self-contained 200x200 1-Bit Graphics & HUD
 * 
 * Modes:
 *   0: Direct Bypass     (Transparent relay, transpose, velocity curves)
 *   1: Patchbay & CC Map (Global Input/Output router, MIDI Learn CC remapper, Clear NVS)
 *   2: Lo-Fi Cassette    (Dual-LFO wow/flutter, tape dropouts, Switch 2 tape brake)
 *   3: Turing Co-Pilot   (Generative 16-bit shift-register sequencer, Switch 2 freeze)
 *   4: FM Timbre Morph   (DX7 / FM-1 vector synthesis crossfader & auto-LFO)
 *   5: Microtonal Engine (Dynamic Just Intonation, Maqam, Bohlen-Pierce, 19-EDO)
 *   6: Physics & Bounce  (Gravitational bouncing ball arpeggiator & strum flam)
 *   7: Visual Canvas     (Full-screen reactive art while keeping active sound FX running!)
 */

#include <Arduino.h>
#include "config.h"
#include "midi_io.h"
#include "inputs.h"
#include "storage.h"
#include "display_ui.h"

// Engine Implementations
#include "engine_bypass.h"
#include "engine_patchbay.h"
#include "engine_lofi.h"
#include "engine_turing.h"
#include "engine_morph.h"
#include "engine_tuning.h"
#include "engine_physics.h"
#include "engine_visualizer.h"

// --- Global Engine Instances ---
static EngineBypass     g_engineBypass;
static EnginePatchbay   g_enginePatchbay;
static EngineLoFi       g_engineLoFi;
static EngineTuring     g_engineTuring;
static EngineMorph      g_engineMorph;
static EngineTuning     g_engineTuning;
static EnginePhysics    g_enginePhysics;
static EngineVisualizer g_engineVisualizer;

static MidiEngine* g_engines[NUM_MODES] = {
  &g_engineBypass,
  &g_enginePatchbay,
  &g_engineLoFi,
  &g_engineTuring,
  &g_engineMorph,
  &g_engineTuning,
  &g_enginePhysics,
  &g_engineVisualizer
};

static uint8_t g_currentMode       = MODE_BYPASS;
static uint8_t g_activeSoundMode   = MODE_LOFI;   // Displayed on Visualizer header badge
static uint8_t g_savedBeforeBypass = MODE_LOFI;
static uint8_t g_savedBeforeVis    = MODE_LOFI;

// --- Helper: Check if a mode is one of the 5 stackable Sound FX Modes ---
bool isSoundFxMode(uint8_t mode) {
  return (mode == MODE_LOFI || mode == MODE_TURING || mode == MODE_MORPH ||
          mode == MODE_TUNING || mode == MODE_PHYSICS);
}

// --- Compute 5-Bit Active Sound Layers Mask ([L][T][M][U][P]) ---
uint8_t getActiveLayersMask() {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 5; i++) {
    if (g_engines[MODE_LOFI + i]->isLayerActive()) {
      mask |= (1 << i);
    }
  }
  return mask;
}

// --- Toggle a Specific Sound Layer [ON] / [OFF] Without Switching Screens ---
void toggleSoundLayer(uint8_t layerIdx) {
  if (layerIdx >= 5) return;
  uint8_t mode = MODE_LOFI + layerIdx;
  g_engines[mode]->toggleLayerActive();

  if (g_engines[mode]->isLayerActive()) {
    g_activeSoundMode = mode;
    g_engineVisualizer.setBackgroundSoundEngine(g_engines[g_activeSoundMode]);
  } else {
    if (mode == MODE_LOFI)   MidiIO::setLfoBendOffset(1, 0);
    if (mode == MODE_TUNING) MidiIO::setTuningBendOffset(1, 0);
    MidiIO::panic();
  }

  // Update physical LEDs immediately
  InputManager::updateAllLayerLeds(InputManager::isSwitch1Active() ? 0 : getActiveLayersMask());

  StorageManager::markDirty();
  DisplayUI::markDirty();

  Serial.printf("[BUTTON %d] Sound Layer '%s' ➔ %s\n",
                layerIdx + 1,
                g_engines[mode]->getName(),
                g_engines[mode]->isLayerActive() ? "ON (ACTIVE)" : "OFF (BYPASSED)");
}

// --- MIDI Router Callbacks (Flows through the Layered Pedalboard Chain!) ---
void handleIncomingNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) {
  if (InputManager::isSwitch1Active() || g_currentMode == MODE_BYPASS) {
    // Master Bypass ON: only apply Bypass transpose/velocity curve and send directly out
    MidiIO::sendNoteOn(channel, pitch, velocity);
  } else {
    // Flow through all active layered Sound FX engines in series!
    g_engineBypass.onNoteOn(channel, pitch, velocity);
  }
  if (g_currentMode == MODE_VISUALIZER || g_currentMode == MODE_BYPASS) {
    DisplayUI::markDirty();
  }
}

void handleIncomingNoteOff(uint8_t channel, uint8_t pitch) {
  if (InputManager::isSwitch1Active() || g_currentMode == MODE_BYPASS) {
    MidiIO::sendNoteOff(channel, pitch);
  } else {
    g_engineBypass.onNoteOff(channel, pitch);
  }
}

void handleIncomingControlChange(uint8_t channel, uint8_t control, uint8_t value) {
  MidiIO::sendControlChange(channel, control, value);
  if (g_currentMode == MODE_PATCHBAY) {
    DisplayUI::markDirty(); // Show learned CC on screen
  }
}

void handleIncomingPitchBend(uint8_t channel, int16_t bend) {
  MidiIO::sendPitchBend(channel, bend);
}

// --- Post-Engine Output Monitor Tap (Drives Reactive Visualizer!) ---
void handleOutputTapNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) {
  g_engineVisualizer.recordOutputNoteOn(pitch, velocity);
  if (g_currentMode == MODE_VISUALIZER) {
    DisplayUI::markDirty();
  }
}

void handleOutputTapNoteOff(uint8_t channel, uint8_t pitch) {
  g_engineVisualizer.recordOutputNoteOff(pitch);
}

// --- Navigate Screens Cleanly (via Dedicated Navigation Encoder) ---
void switchMode(uint8_t newMode) {
  if (newMode >= NUM_MODES) newMode = 0;
  if (newMode == g_currentMode) return;

  if (isSoundFxMode(newMode)) {
    g_activeSoundMode = newMode;
  }

  g_currentMode = newMode;
  g_engineVisualizer.setBackgroundSoundEngine(g_engines[g_activeSoundMode]);

  // Sync Switch 2 state to the newly viewed screen
  g_engines[g_currentMode]->onSwitch2(InputManager::isSwitch2Active());

  StorageManager::markDirty();
  DisplayUI::markDirty();

  Serial.printf("\n[NAVIGATE SCREEN] ➔ %s\n", g_engines[g_currentMode]->getName());
}

void setup() {
  Serial.begin(115200);
  delay(400);

  Serial.println("\n=======================================================");
  Serial.println("  ESP32-S3 Universal MIDI Brain & FM-1 Controller Hub  ");
  Serial.println("=======================================================");

  // 1. Wire up the Multi-Engine Layered Pedalboard Chain:
  //    Bypass (Transpose/Vel) ➔ Turing ➔ Physics ➔ Microtonal ➔ Lo-Fi ➔ Morph ➔ Output
  g_engineBypass.setLayerActive(true);
  g_engineBypass.setNextInChain(&g_engineTuring);
  g_engineTuring.setNextInChain(&g_enginePhysics);
  g_enginePhysics.setNextInChain(&g_engineTuning);
  g_engineTuning.setNextInChain(&g_engineLoFi);
  g_engineLoFi.setNextInChain(&g_engineMorph);
  g_engineMorph.setNextInChain(nullptr);

  // Enable Lo-Fi layer by default on fresh boot
  g_engineLoFi.setLayerActive(true);

  // 2. Initialize Physical Inputs (5 Encoders, 5 Layer Buttons, 2 Latching Switches)
  InputManager::init();

  // 3. Initialize MIDI I/O (TRS UART + Native USB Host + BLE MIDI)
  MidiIO::init();
  MidiIO::setNoteOnCallback(handleIncomingNoteOn);
  MidiIO::setNoteOffCallback(handleIncomingNoteOff);
  MidiIO::setControlChangeCallback(handleIncomingControlChange);
  MidiIO::setPitchBendCallback(handleIncomingPitchBend);
  MidiIO::setOutputMonitorCallbacks(handleOutputTapNoteOn, handleOutputTapNoteOff);

  // 4. Initialize Non-Volatile Storage (NVS Flash)
  StorageManager::init();

  // Hardware Boot Shortcut: Hold Encoder 1 + Encoder 4 buttons on power-up to Factory Reset!
  if (digitalRead(PIN_ENC1_SW) == LOW && digitalRead(PIN_ENC4_SW) == LOW) {
    StorageManager::clearAllSettings();
  } else {
    StorageManager::loadState(g_currentMode, g_activeSoundMode);
  }

  // Respect hardware Switch 1 (Master Bypass) position on boot
  if (InputManager::isSwitch1Active()) {
    g_savedBeforeBypass = g_currentMode;
    g_currentMode = MODE_BYPASS;
  }

  for (uint8_t m = 0; m < NUM_MODES; m++) {
    g_engines[m]->init();
  }
  g_engineVisualizer.setBackgroundSoundEngine(g_engines[g_activeSoundMode]);
  g_engines[g_currentMode]->onSwitch2(InputManager::isSwitch2Active());
  InputManager::updateAllLayerLeds(InputManager::isSwitch1Active() ? 0 : getActiveLayersMask());

  // 5. Initialize 1.54" E-Paper Display & Framebuffer
  DisplayUI::init();
  DisplayUI::markDirty();

  Serial.println("\n[CONTROLS READY]");
  Serial.println("  • Navigation Encoder (Enc 5): Rotate to navigate all 8 screens (Click = Visualizer)");
  Serial.println("  • Parameter Encoders (1–4):   Adjust & click-reset the 4 on-screen parameters");
  Serial.println("  • 5 Layer Buttons + LEDs:     Toggle Lo-Fi, Turing, Morph, Microtonal, Physics [ON/OFF]");
  Serial.println("  • Latching Switch 1:          Master Bypass (ON = Clean Bypass, OFF = Layered Chain)");
  Serial.println("  • Latching Switch 2:          Freeze / Tape Brake / Infinite Latch / Arm NVS Clear");
}

// --- Optional Bench-Test Chord Sequencer (Toggled via Serial 'd' when on PC) ---
static bool g_demoActive = false;
static uint32_t g_lastDemoTick = 0;
static uint8_t g_demoStep = 0;
static const uint8_t DEMO_CHORDS[4][3] = {
  {60, 64, 67}, // C Major
  {57, 60, 64}, // A Minor
  {65, 69, 72}, // F Major
  {55, 59, 62}  // G Major
};

void updateVirtualDemo() {
  if (!g_demoActive) return;
  uint32_t now = millis();
  if (now - g_lastDemoTick < 1200) return;
  g_lastDemoTick = now;

  uint8_t prevStep = (g_demoStep + 3) % 4;
  for (int i = 0; i < 3; i++) {
    MidiIO::triggerVirtualNoteOff(DEMO_CHORDS[prevStep][i]);
  }
  for (int i = 0; i < 3; i++) {
    MidiIO::triggerVirtualNoteOn(DEMO_CHORDS[g_demoStep][i], 95);
  }
  g_demoStep = (g_demoStep + 1) % 4;
}

void loop() {
  // 1. Poll Hardware MIDI (USB Host & BLE) and Physical Controls
  MidiIO::update();
  InputManager::update();

  // 2. Latching Switch 1: Master Bypass (Flip Up/ON = Bypass, Flip Down/OFF = Restore Engine)
  if (InputManager::hasSwitch1Changed()) {
    if (InputManager::isSwitch1Active()) {
      g_savedBeforeBypass = g_currentMode;
      switchMode(MODE_BYPASS);
    } else {
      switchMode(g_savedBeforeBypass);
    }
    InputManager::updateAllLayerLeds(InputManager::isSwitch1Active() ? 0 : getActiveLayersMask());
  }

  // 3. Latching Switch 2: Engine-Specific Freeze / Tape Stop / Latch / Clear Arm
  if (InputManager::hasSwitch2Changed()) {
    g_engines[g_currentMode]->onSwitch2(InputManager::isSwitch2Active());
    DisplayUI::markDirty();
  }

  // 4. Process 5 Dedicated Sound Layer Toggle Buttons (Buttons 1–5)
  //    Toggles the sound layer [ON/OFF] in the audio chain WITHOUT switching the current screen!
  for (uint8_t b = 0; b < InputManager::NUM_LAYER_BTNS; b++) {
    if (InputManager::isLayerButtonClicked(b)) {
      toggleSoundLayer(b);
    }
  }

  // 5. Process 5th Dedicated Navigation Encoder (Screen Selector)
  int navDelta = InputManager::getEncoderDelta(InputManager::ENC_NAV_INDEX);
  if (navDelta != 0) {
    int nextMode = ((int)g_currentMode + navDelta + NUM_MODES) % NUM_MODES;
    switchMode((uint8_t)nextMode);
  }
  if (InputManager::isEncoderClicked(InputManager::ENC_NAV_INDEX)) {
    // Clicking Navigation Encoder jumps to Visualizer (or returns to previous screen)
    if (g_currentMode == MODE_VISUALIZER) {
      switchMode(g_savedBeforeVis);
    } else {
      g_savedBeforeVis = g_currentMode;
      switchMode(MODE_VISUALIZER);
    }
  }

  // 6. Process 4 Parameter Encoders (Encoders 1–4 strictly control Columns 1–4)
  for (uint8_t i = 0; i < 4; i++) {
    int delta = InputManager::getEncoderDelta(i);
    bool clicked = InputManager::isEncoderClicked(i);

    if (delta != 0) {
      g_engines[g_currentMode]->onEncoderTurn(i, delta);
      StorageManager::markDirty();
      DisplayUI::markDirty();
    }
    if (clicked) {
      g_engines[g_currentMode]->onEncoderClick(i);
      StorageManager::markDirty();
      DisplayUI::markDirty();
    }
  }

  // 7. Run All Active Layered Sound Engines Simultaneously!
  if (!InputManager::isSwitch1Active() && g_currentMode != MODE_BYPASS) {
    for (uint8_t m = MODE_LOFI; m <= MODE_PHYSICS; m++) {
      if (g_engines[m]->isLayerActive()) {
        g_engines[m]->update();
      }
    }
  }

  // 8. Auto-Save Settings to ESP32 Flash after 3s of inactivity
  StorageManager::update(g_currentMode, g_activeSoundMode);

  // 9. Refresh 1.54" E-Paper & Serial HUD (Non-blocking & Debounced)
  DisplayUI::update(
    g_engines[g_currentMode],
    g_currentMode,
    getActiveLayersMask(),
    MidiIO::getConnectedDeviceName(),
    &g_engineVisualizer,
    &g_engineTuring
  );

  // 9. Optional Bench-Test Sequencer & Serial Shortcuts
  updateVirtualDemo();

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'm' || c == 'M') {
      switchMode((g_currentMode + 1) % NUM_MODES);
    } else if (c == 'p' || c == 'P') {
      MidiIO::triggerVirtualNoteOn(60, 100);
      delay(200);
      MidiIO::triggerVirtualNoteOff(60);
    } else if (c == 'd' || c == 'D') {
      g_demoActive = !g_demoActive;
      Serial.printf("[DEMO] Virtual chord sequencer %s.\n", g_demoActive ? "ENABLED" : "DISABLED");
    } else if (c == 'c' || c == 'C') {
      StorageManager::clearAllSettings();
      DisplayUI::markDirty();
    } else if (c == '!') {
      MidiIO::panic();
      Serial.println("[PANIC] All Notes Off sent.");
    }
  }
}
