# MIDI-S3: Universal Hardware MIDI Brain & Generative FX Pedalboard

A standalone, computer-free MIDI processor, generative sequencer hub, and multi-engine transformation pedalboard powered by the **ESP32-S3**.

Designed to sit between any class-compliant USB MIDI keyboard / controller and hardware synthesizers (such as the **M-VAVE FM-1**, Roland boutique units, Korg Volcas, or desktop modules), **MIDI-S3** bridges USB, BLE, and wired TRS Type-A MIDI while running a serial chain of generative, algorithmic, and acoustic transformation engines in real time.

---

## 🌟 Key Features

* **100% Computer-Free & Standalone:** Powered by a dual-core 240MHz ESP32-S3. No computer or DAW required for performance.
* **Triple-Transport MIDI Hub:**
  * **Native USB Host (OTG):** Plug any class-compliant USB MIDI keyboard or pad controller directly into the USB-C port via an OTG adapter.
  * **Wireless BLE MIDI:** Connect wireless keyboards, tablets, or phones over low-latency Bluetooth Low Energy MIDI.
  * **Wired TRS Type-A 3.5mm MIDI OUT:** Drives standard 3.3V hardware synths with sub-millisecond latency.
* **Stackable Multi-Engine Pedalboard Architecture:** Sound engines run simultaneously in a serial audio chain (`Bypass ➔ Turing ➔ Physics ➔ Microtonal ➔ Lo-Fi ➔ Morph ➔ Outputs & Visualizer`).
* **Tactile Hardware Interface:**
  * **4 Parameter Rotary Encoders (EC11):** 1:1 control of the 4 on-screen column parameters with click-to-reset.
  * **1 Dedicated Navigation Encoder:** Smoothly cycle all 8 screen views; click to jump directly to the Visual Canvas.
  * **5 Dedicated Sound Layer Buttons:** Toggle individual sound engines `[ON/OFF]` in the audio chain instantly without interrupting your current screen view.
  * **5 Dedicated Status LEDs:** Immediate visual feedback showing which audio layers are active.
  * **2 Latching Toggle Switches:** Hardware Master Bypass switch and Mode-Specific Performance Action switch (Freeze, Tape Brake, Infinite Bounce).
* **1.54" E-Paper HUD (Waveshare SSD1681/SSD1608):**
  * Sunlight-readable 200×200 1-bit crisp display.
  * Live status bar displaying active layers `[L][T][M][U][P]`, connected USB device name, and parameter readouts.
* **Auto-Save & Factory Reset (NVS Flash):** Auto-saves all parameter tweaks and routing to ESP32 Flash after 3 seconds of inactivity.
* **Zero External Library Dependencies:** Custom native ESP32-S3 USB Host MIDI driver, BLE MIDI GATT service, and 200×200 SPI framebuffer graphics engine are built directly into the codebase—nothing to install from library managers.

---

## 🎹 The 8 Modes & Transformation Engines

| Mode # | Engine Name | Type | Description |
| :---: | :--- | :---: | :--- |
| **0** | **Direct Bypass** | Utility | Clean pass-through, semitone transposition (-24..+24 st), 4 velocity response curves (Linear, Soft, Hard, Fixed), and master volume control. |
| **1** | **Patchbay & CC Mapper**| Utility | Live input/output port routing, MIDI channel remapping, and real-time **MIDI Learn CC Mapper** with NVS Flash persistence. |
| **2** | **Lo-Fi Cassette** | Sound Layer 1 | Dual-LFO wow and flutter tape pitch drift, random velocity aging, tape dropouts, and instant hardware **Tape Brake** stop effect. |
| **3** | **Turing Co-Pilot** | Sound Layer 2 | Algorithmic 16-bit shift-register sequencer seeded by chords played on your keyboard. Features variable mutation rate (0–100%), loop length (2–16 steps), and hardware **Loop Freeze**. |
| **4** | **FM Timbre Morph** | Sound Layer 3 | Vector synthesis macro-controller for 4-Op/6-Op FM synths (DX7/FM-1). Interpolates sound palettes and streams Cutoff, Resonance, Attack, Release, and Reverb CCs with an auto-morph LFO. |
| **5** | **Microtonal Engine** | Sound Layer 4 | Dynamic Just Intonation harmonic auto-retuner (analyzes chords on the fly to tune thirds and fifths to pure ratios), Arabic Maqam, Bohlen-Pierce, 19-EDO, and additive pitch-bend offsets. |
| **6** | **Physics & Bounce** | Sound Layer 5 | Gravitational particle bouncing echo engine. Repeated notes bounce with decaying elasticity, strum flam spreads, and hardware **Infinite Bounce Latch**. |
| **7** | **Visual Canvas** | Display / Art | 4 generative reactive art modes (*Constellation*, *Spiro-Ink*, *Cellular Life*, *Piano Ribbon*) that react in real time to notes passing through the engine stack. |

---

## ⚡ Master Hardware Pinout (ESP32-S3)

All inputs and peripherals have been mapped to avoid strapping pin conflicts:

| Peripheral | Sub-Signal | GPIO Pin | Notes |
| :--- | :--- | :---: | :--- |
| **USB Host** | D- / D+ | **GPIO 19 / 20** | Pre-wired internally to Native USB-C port |
| **TRS MIDI OUT** | UART1 TX | **GPIO 17** | To 3.5mm Tip via 33Ω resistor |
| **1.54" E-Paper** | MOSI / SCK / CS / DC / BUSY | **GPIO 11 / 12 / 10 / 9 / 7** | RST tied to `+3.3V / EN` |
| **Param Encoders 1–4**| CLK / DT / SW | **GPIO 1–6, 13–16, 18, 21** | 4 parameter columns |
| **Nav Encoder 5** | CLK / DT / SW | **GPIO 38 / 41 / 0** | Rotate: Navigate, Click: Visualizer |
| **5-Button Bus** | Analog Resistor Ladder | **GPIO 8** | Reads all 5 layer buttons on 1 ADC pin |
| **5 Layer LEDs** | Standard LEDs | **GPIO 47, 48, 45, 46, 42** | Lo-Fi, Turing, Morph, Tuning, Physics |
| **2 Latching Switches**| Master Bypass / Engine Latch | **GPIO 39 / 40** | LEDs wired across switches (0 extra GPIOs) |

---

## 🚀 Quick Start (Arduino IDE 2.x)

1. **Install Arduino IDE 2.x** and add the ESP32 board package URL in **Preferences**:
   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
2. Open **Boards Manager**, search for `esp32` by Espressif Systems, and click **Install**.
3. Plug your PC into the ESP32-S3's **UART / COM** USB-C port.
4. Select board: **Tools ➔ Board ➔ esp32 ➔ ESP32S3 Dev Module**.
   * Set **USB Mode:** `"Hardware CDC and JTAG"` (preserves the Native OTG port for USB Host MIDI).
5. Open [`midi_s3.ino`](midi_s3.ino) and click **Upload**.
6. Plug your USB keyboard into the native USB-C port, connect the 3.5mm TRS jack to your synthesizer, and start playing!

---

## 🛠️ Step 1 Hardware Diagnostic Sketch

Before wiring the full control panel, you can verify your 3.5mm TRS connection to your synthesizer in under 2 minutes:
* Open and flash [`step1_midi_out_test.ino`](step1_midi_out_test.ino).
* It will play an alternating octave C4/C5 test arpeggio on MIDI Channel 1 to verify audio output immediately.

---

## 📄 License

MIT License — Feel free to modify, build, and use in your own DIY music hardware projects!
