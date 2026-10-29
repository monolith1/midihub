#include "inputs.h"

EncoderState InputManager::s_encoders[InputManager::NUM_ENCODERS] = {
  {PIN_ENC1_A,    PIN_ENC1_B,    PIN_ENC1_SW,    0, 0, HIGH, false, 0}, // Param 1
  {PIN_ENC2_A,    PIN_ENC2_B,    PIN_ENC2_SW,    0, 0, HIGH, false, 0}, // Param 2
  {PIN_ENC3_A,    PIN_ENC3_B,    PIN_ENC3_SW,    0, 0, HIGH, false, 0}, // Param 3
  {PIN_ENC4_A,    PIN_ENC4_B,    PIN_ENC4_SW,    0, 0, HIGH, false, 0}, // Param 4
  {PIN_ENC_NAV_A, PIN_ENC_NAV_B, PIN_ENC_NAV_SW, 0, 0, HIGH, false, 0}  // Dedicated Navigation
};

bool InputManager::s_layerBtnClicked[InputManager::NUM_LAYER_BTNS] = {false, false, false, false, false};
int8_t InputManager::s_lastDetectedBtn = -1;
uint32_t InputManager::s_btnPressStartTime = 0;
bool InputManager::s_btnArmed = false;

const uint8_t InputManager::s_layerLedPins[InputManager::NUM_LAYER_BTNS] = {
  PIN_LED_LAYER_1, // GPIO 47: Lo-Fi
  PIN_LED_LAYER_2, // GPIO 48: Turing
  PIN_LED_LAYER_3, // GPIO 45: Morph
  PIN_LED_LAYER_4, // GPIO 46: Microtonal
  PIN_LED_LAYER_5  // GPIO 42: Physics
};

bool InputManager::s_sw1State = false;
bool InputManager::s_sw2State = false;
bool InputManager::s_sw1Changed = false;
bool InputManager::s_sw2Changed = false;
uint32_t InputManager::s_lastSwDebounce = 0;

// Quadrature lookup table for rotary encoders
static const int8_t ENC_STATES[] = {
  0, -1,  1,  0,
  1,  0,  0, -1,
 -1,  0,  0,  1,
  0,  1, -1,  0
};

void InputManager::init() {
  // 1. Configure 5 Rotary Encoders with Internal Pull-Ups
  for (uint8_t i = 0; i < NUM_ENCODERS; i++) {
    pinMode(s_encoders[i].pinA, INPUT_PULLUP);
    pinMode(s_encoders[i].pinB, INPUT_PULLUP);
    pinMode(s_encoders[i].pinSW, INPUT_PULLUP);

    uint8_t a = digitalRead(s_encoders[i].pinA);
    uint8_t b = digitalRead(s_encoders[i].pinB);
    s_encoders[i].lastState = (a << 1) | b;
    s_encoders[i].lastButtonState = digitalRead(s_encoders[i].pinSW);
  }

  // 2. Configure Analog Resistor Ladder Button Pin (ADC1)
  pinMode(PIN_BTN_BUS_ADC, INPUT);
  analogReadResolution(12); // 0 .. 4095

  // 3. Configure 5 Dedicated Sound Layer Status LEDs
  for (uint8_t i = 0; i < NUM_LAYER_BTNS; i++) {
    pinMode(s_layerLedPins[i], OUTPUT);
    digitalWrite(s_layerLedPins[i], LOW);
  }

  // 4. Configure 2 Latching Toggle Switches with Internal Pull-Ups
  pinMode(PIN_SW1, INPUT_PULLUP);
  pinMode(PIN_SW2, INPUT_PULLUP);
  s_sw1State = (digitalRead(PIN_SW1) == LOW);
  s_sw2State = (digitalRead(PIN_SW2) == LOW);
}

void InputManager::update() {
  uint32_t now = millis();

  // 1. Poll 5 Rotary Encoders (Rotation & Push Switch)
  for (uint8_t i = 0; i < NUM_ENCODERS; i++) {
    uint8_t a = digitalRead(s_encoders[i].pinA);
    uint8_t b = digitalRead(s_encoders[i].pinB);
    uint8_t currentState = (a << 1) | b;

    if (currentState != s_encoders[i].lastState) {
      uint8_t index = (s_encoders[i].lastState << 2) | currentState;
      int8_t step = ENC_STATES[index & 0x0F];
      s_encoders[i].delta += step;
      s_encoders[i].lastState = currentState;
    }

    bool reading = digitalRead(s_encoders[i].pinSW);
    if (reading != s_encoders[i].lastButtonState) {
      if ((now - s_encoders[i].lastDebounceTime) > 35) {
        if (reading == LOW) {
          s_encoders[i].buttonPressed = true;
        }
        s_encoders[i].lastButtonState = reading;
        s_encoders[i].lastDebounceTime = now;
      }
    }
  }

  // 2. Poll 5-Button Resistor Ladder on PIN_BTN_BUS_ADC (GPIO 8)
  // Voltage brackets allow standard daisy-chain (1k resistors) or individual dividers:
  // - Button 1 (Lo-Fi):      ADC < 250         (~0.0V - 0.2V)
  // - Button 2 (Turing):     ADC 250 .. 600    (~0.3V - 0.5V)
  // - Button 3 (Morph):      ADC 600 .. 950    (~0.5V - 0.8V)
  // - Button 4 (Microtonal): ADC 950 .. 1400   (~0.8V - 1.1V)
  // - Button 5 (Physics):    ADC 1400 .. 2200  (~1.1V - 1.8V)
  // - No Button Pressed:     ADC > 3400        (~3.3V pulled up)
  int adcVal = analogRead(PIN_BTN_BUS_ADC);
  int8_t currentBtn = -1;

  if (adcVal < 250) {
    currentBtn = 0; // Button 1: Lo-Fi
  } else if (adcVal < 600) {
    currentBtn = 1; // Button 2: Turing
  } else if (adcVal < 950) {
    currentBtn = 2; // Button 3: Morph
  } else if (adcVal < 1400) {
    currentBtn = 3; // Button 4: Microtonal
  } else if (adcVal < 2400) {
    currentBtn = 4; // Button 5: Physics
  }

  if (currentBtn != -1) {
    if (currentBtn != s_lastDetectedBtn) {
      s_lastDetectedBtn = currentBtn;
      s_btnPressStartTime = now;
      s_btnArmed = true;
    } else if (s_btnArmed && (now - s_btnPressStartTime > 45)) { // 45ms debounce
      s_layerBtnClicked[currentBtn] = true;
      s_btnArmed = false; // Only trigger once per press
    }
  } else {
    s_lastDetectedBtn = -1;
    s_btnArmed = false;
  }

  // 3. Poll 2 Latching Switches
  s_sw1Changed = false;
  s_sw2Changed = false;

  if ((now - s_lastSwDebounce) > 50) {
    bool raw1 = (digitalRead(PIN_SW1) == LOW);
    bool raw2 = (digitalRead(PIN_SW2) == LOW);

    if (raw1 != s_sw1State) {
      s_sw1State = raw1;
      s_sw1Changed = true;
    }

    if (raw2 != s_sw2State) {
      s_sw2State = raw2;
      s_sw2Changed = true;
    }

    s_lastSwDebounce = now;
  }
}

int InputManager::getEncoderDelta(uint8_t index) {
  if (index >= NUM_ENCODERS) return 0;
  int steps = s_encoders[index].delta / 2;
  if (steps != 0) {
    s_encoders[index].delta %= 2;
  }
  return steps;
}

bool InputManager::isEncoderClicked(uint8_t index) {
  if (index >= NUM_ENCODERS) return false;
  if (s_encoders[index].buttonPressed) {
    s_encoders[index].buttonPressed = false;
    return true;
  }
  return false;
}

bool InputManager::isLayerButtonClicked(uint8_t btnIndex) {
  if (btnIndex >= NUM_LAYER_BTNS) return false;
  if (s_layerBtnClicked[btnIndex]) {
    s_layerBtnClicked[btnIndex] = false;
    return true;
  }
  return false;
}

void InputManager::setLayerLed(uint8_t layerIndex, bool on) {
  if (layerIndex < NUM_LAYER_BTNS) {
    digitalWrite(s_layerLedPins[layerIndex], on ? HIGH : LOW);
  }
}

void InputManager::updateAllLayerLeds(uint8_t activeLayersMask) {
  for (uint8_t i = 0; i < NUM_LAYER_BTNS; i++) {
    bool active = (activeLayersMask & (1 << i)) != 0;
    digitalWrite(s_layerLedPins[i], active ? HIGH : LOW);
  }
}

bool InputManager::isSwitch1Active() { return s_sw1State; }
bool InputManager::isSwitch2Active() { return s_sw2State; }
bool InputManager::hasSwitch1Changed() { return s_sw1Changed; }
bool InputManager::hasSwitch2Changed() { return s_sw2Changed; }
