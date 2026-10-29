#pragma once
#include <Arduino.h>
#include "config.h"

struct EncoderState {
  uint8_t pinA;
  uint8_t pinB;
  uint8_t pinSW;
  uint8_t lastState;
  int delta;
  bool lastButtonState;
  bool buttonPressed;
  uint32_t lastDebounceTime;
};

class InputManager {
public:
  static const uint8_t NUM_ENCODERS = 5;    // 0..3 = Param 1..4, 4 = Navigation Encoder
  static const uint8_t ENC_NAV_INDEX = 4;
  static const uint8_t NUM_LAYER_BTNS = 5;  // 0..4 = Sound Layer 1..5 Toggle Buttons

  static void init();
  static void update();

  // --- Parameter & Navigation Encoder Queries (0 to 4) ---
  static int getEncoderDelta(uint8_t index);
  static bool isEncoderClicked(uint8_t index);

  // --- 5 Sound Layer Toggle Button Queries (0 to 4, read via Resistor Ladder) ---
  static bool isLayerButtonClicked(uint8_t btnIndex);

  // --- 5 Dedicated Sound Layer Status LEDs Control ---
  static void setLayerLed(uint8_t layerIndex, bool on);
  static void updateAllLayerLeds(uint8_t activeLayersMask);

  // --- 2 Latching Switch Queries ---
  static bool isSwitch1Active();
  static bool isSwitch2Active();
  static bool hasSwitch1Changed();
  static bool hasSwitch2Changed();

private:
  static EncoderState s_encoders[NUM_ENCODERS];
  static bool s_layerBtnClicked[NUM_LAYER_BTNS];
  static int8_t s_lastDetectedBtn;
  static uint32_t s_btnPressStartTime;
  static bool s_btnArmed;

  static bool s_sw1State;
  static bool s_sw2State;
  static bool s_sw1Changed;
  static bool s_sw2Changed;
  static uint32_t s_lastSwDebounce;

  static const uint8_t s_layerLedPins[NUM_LAYER_BTNS];
};
