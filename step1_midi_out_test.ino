/*
 * Step 1: ESP32-S3 TRS MIDI OUT Test
 * 
 * Hardware Connections:
 *   ESP32-S3 GPIO 17 ──[ 33Ω Resistor ]──► 3.5mm Jack TIP
 *   ESP32-S3 +3.3V   ──[ 33Ω Resistor ]──► 3.5mm Jack RING
 *   ESP32-S3 GND     ────────────────────► 3.5mm Jack SLEEVE
 * 
 * Target: M-VAVE FM-1 (or any TRS Type-A MIDI device)
 * Default Channel: 1 (0x90 = Note On, 0x80 = Note Off)
 */

#include <Arduino.h>

#define MIDI_TX_PIN 17
#define STATUS_LED_PIN 48 // Optional status LED / power LED

// Use hardware UART1 for MIDI (31250 baud, standard 8N1)
HardwareSerial MidiSerial(1);

void sendNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) {
  MidiSerial.write(0x90 | (channel - 1));
  MidiSerial.write(pitch);
  MidiSerial.write(velocity);
}

void sendNoteOff(uint8_t channel, uint8_t pitch) {
  MidiSerial.write(0x80 | (channel - 1));
  MidiSerial.write(pitch);
  MidiSerial.write(0);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32-S3 MIDI OUT Test Starting ---");

  pinMode(STATUS_LED_PIN, OUTPUT);

  // Initialize MIDI hardware serial: 31250 baud, RX disabled (-1), TX on GPIO 17
  MidiSerial.begin(31250, SERIAL_8N1, -1, MIDI_TX_PIN);
  Serial.printf("MIDI UART initialized on GPIO %d at 31250 baud.\n", MIDI_TX_PIN);
}

// Pentatonic scale pitches: C4, D4, E4, G4, A4, C5
const uint8_t testNotes[] = {60, 62, 64, 67, 69, 72};
const int numNotes = sizeof(testNotes) / sizeof(testNotes[0]);
int noteIndex = 0;

void loop() {
  uint8_t currentNote = testNotes[noteIndex];

  // Play Note
  Serial.printf("Playing MIDI note: %d (Channel 1)...\n", currentNote);
  digitalWrite(STATUS_LED_PIN, HIGH);
  sendNoteOn(1, currentNote, 100); // Note On, velocity 100
  delay(350);

  // Stop Note
  sendNoteOff(1, currentNote);
  digitalWrite(STATUS_LED_PIN, LOW);
  delay(150);

  // Advance to next note in scale
  noteIndex = (noteIndex + 1) % numNotes;
}
