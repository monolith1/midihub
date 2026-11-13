#pragma once
#include <Arduino.h>

// ==============================================================================
// ESP32-S3 Universal MIDI Brain — Hardware & Pin Configuration
// ==============================================================================

// --- Feature Toggles ---
#define ENABLE_USB_HOST       1    // Native ESP32-S3 USB OTG Host (GPIO 19/20)
#define ENABLE_BLE_MIDI       1    // Wireless Bluetooth Low Energy MIDI
#define ENABLE_SD_CARD        1    // MicroSD Card support for MIDI recording, presets & Sysex
#define ENABLE_BTN_LADDER     0    // Set to 1 once 5-button resistor ladder + 10k pull-up is wired to GPIO 8
#define EPD_WIDTH             200  // 1.54" E-Paper width in pixels
#define EPD_HEIGHT            200  // 1.54" E-Paper height in pixels
#define EPD_3COLOR_MODE       0    // Set to 1 if using a 3-color (B/W/Red) 1.54" module

// --- TRS MIDI Output (Hardware UART 1) ---
#define PIN_MIDI_TX           17   // Serial TX connected to 3.5mm jack Tip via 33Ω
#define MIDI_BAUD_RATE        31250

// --- 1.54" E-Paper SPI Display (Waveshare SSD1681/SSD1608) ---
#define PIN_EPD_MOSI          11   // SPI DIN (Shared with SD Card)
#define PIN_EPD_SCK           12   // SPI CLK (Shared with SD Card)
#define PIN_EPD_CS            10   // E-Paper Chip Select (Dedicated)
#define PIN_EPD_DC            9    // Data / Command
#define PIN_EPD_BUSY          7    // Busy status
#define PIN_EPD_RST           16   // Hardware Reset Line (GPIO 16)

// --- MicroSD Card SPI Bus (Standard FAT32) ---
// Dedicated high-speed SPI bus on free GPIOs (Zero conflict with Dual I2C OLEDs)
#define PIN_SD_SCK            7    // SPI Clock (CLK)
#define PIN_SD_MOSI           16   // SPI MOSI (DI - Data In to SD card)
#define PIN_SD_MISO           21   // SPI MISO (DO - Data Out from SD card)
#define PIN_SD_CS             18   // MicroSD Chip Select (CS - Active Low)

// --- 4 Parameter Rotary Encoders (EC11 Standard with Push Switch) ---
#define PIN_ENC1_A            1
#define PIN_ENC1_B            2
#define PIN_ENC1_SW           3

// Encoder 2 (Column 2 Parameter)
#define PIN_ENC2_A            4
#define PIN_ENC2_B            5
#define PIN_ENC2_SW           6

// Encoder 3 (Column 3 Parameter)
#define PIN_ENC3_A            13
#define PIN_ENC3_B            14
#define PIN_ENC3_SW           15

// Encoder 4 (Column 4 Parameter)
// Note: Moved away from GPIO 16 (now used for EPD Hardware Reset)
#define PIN_ENC4_A            37
#define PIN_ENC4_B            35
#define PIN_ENC4_SW           36

// --- 5th Dedicated Navigation Rotary Encoder (Screen Selector) ---
#define PIN_ENC_NAV_A         38   // Turn to scroll all 8 screens
#define PIN_ENC_NAV_B         41
#define PIN_ENC_NAV_SW        0    // Click to quick-jump to/from Visualizer screen!

// --- 5-Button Analog Resistor Ladder (Reads all 5 buttons on 1 ADC pin!) ---
#define PIN_BTN_BUS_ADC       8    // ADC1 Channel 7 (Reads Buttons 1 to 5)

// --- 5 Dedicated Sound Layer Status LEDs (Directly Driven!) ---
#define PIN_LED_LAYER_1       47   // LED 1: Lo-Fi Cassette Active Status
#define PIN_LED_LAYER_2       48   // LED 2: Turing Co-Pilot Active Status
#define PIN_LED_LAYER_3       45   // LED 3: FM Timbre Morph Active Status
#define PIN_LED_LAYER_4       46   // LED 4: Microtonal Engine Active Status
#define PIN_LED_LAYER_5       42   // LED 5: Physics & Bounce Active Status

// --- 2 Physical Latching Toggle Switches (Wire switch LEDs in series to 3.3V!) ---
#define PIN_SW1               39   // Switch 1: Master Bypass (Flip ON = Bypass, Flip OFF = Active)
#define PIN_SW2               40   // Switch 2: Engine Latch / Freeze / Tape Stop / Clear Arm

// --- Auto-Save NVS Configuration ---
#define NVS_NAMESPACE         "midi_s3"
#define NVS_AUTOSAVE_DELAY_MS 3000 // Wait 3s after last adjustment before writing to Flash

// --- Engine Modes / Screens ---
enum EngineMode : uint8_t {
  MODE_BYPASS = 0,         // 0: Clean transparent pass-through, transpose, velocity curve
  MODE_PATCHBAY,           // 1: Global Device Patchbay, Channel Router & CC Learn Mapper
  MODE_LOFI,               // 2: Sound Layer 1 - Lo-Fi Cassette Tape Simulator
  MODE_TURING,             // 3: Sound Layer 2 - Generative Shift-Register Co-Pilot
  MODE_MORPH,              // 4: Sound Layer 3 - FM Timbre Morphing (DX7 / FM-1 vector)
  MODE_TUNING,             // 5: Sound Layer 4 - Dynamic Just Intonation & Microtonal
  MODE_PHYSICS,            // 6: Sound Layer 5 - Bouncing Ball Gravity Arp & Strum Flam
  MODE_VISUALIZER,         // 7: Full-Screen Reactive E-Paper Art Canvas
  NUM_MODES
};

// --- Patchbay Input & Output Port Identifiers ---
enum MidiInputPort : uint8_t {
  IN_PORT_ALL = 0,         // Merge all connected USB + BLE inputs
  IN_PORT_USB_ANY,         // Any connected USB MIDI device
  IN_PORT_USB_1,           // USB Device Slot 1
  IN_PORT_USB_2,           // USB Device Slot 2 (via Hub)
  IN_PORT_BLE,             // Wireless Bluetooth MIDI In
  NUM_IN_PORTS
};

enum MidiOutputPort : uint8_t {
  OUT_PORT_ALL = 0,        // Broadcast to TRS + USB Out + BLE Out simultaneously
  OUT_PORT_TRS_ONLY,       // 3.5mm Wired TRS MIDI Out only (lowest latency)
  OUT_PORT_USB_ONLY,       // USB MIDI Out only (to USB synth / FM-1 on hub)
  OUT_PORT_BLE_ONLY,       // Bluetooth BLE MIDI Out only
  OUT_PORT_TRS_AND_USB,    // Wired TRS + USB Out
  NUM_OUT_PORTS
};
