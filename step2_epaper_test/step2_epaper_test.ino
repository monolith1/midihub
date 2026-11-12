/*
 * ==============================================================================
 * Step 2: 1.54" E-Paper Display Diagnostic & Wake-Up Test (SSD1681 Standard)
 * ==============================================================================
 */

#include <Arduino.h>
#include <SPI.h>

#define PIN_EPD_SCK   12
#define PIN_EPD_MOSI  11
#define PIN_EPD_CS    10
#define PIN_EPD_DC    9
#define PIN_EPD_BUSY  7
#define PIN_EPD_RST   16

static const SPISettings EPD_SPI_SETTINGS(4000000, MSBFIRST, SPI_MODE0);

void sendCommand(uint8_t cmd) {
  SPI.beginTransaction(EPD_SPI_SETTINGS);
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(cmd);
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.endTransaction();
}

void sendData(uint8_t data) {
  SPI.beginTransaction(EPD_SPI_SETTINGS);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(data);
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.endTransaction();
}

void waitBusy(const char* stepName) {
  Serial.printf("[EPD] Waiting for %s...", stepName);
  uint32_t start = millis();
  delay(20);
  while (digitalRead(PIN_EPD_BUSY) == HIGH) {
    delay(20);
    if (millis() - start > 8000) {
      Serial.println(" TIMEOUT!");
      return;
    }
  }
  Serial.printf(" DONE (%lums)\n", millis() - start);
}

void hardwareReset() {
  pinMode(PIN_EPD_RST, OUTPUT);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
  digitalWrite(PIN_EPD_RST, LOW);
  delay(20);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(20);
  Serial.println("[EPD] Hardware reset complete.");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=======================================================");
  Serial.println("       1.54\" E-Paper Diagnostic (Full Black & White)   ");
  Serial.println("=======================================================");

  pinMode(PIN_EPD_CS, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_BUSY, INPUT);

  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, HIGH);

  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);

  // 1. Hardware Reset
  hardwareReset();
  waitBusy("Reset Wakeup");

  // 2. Software Reset
  sendCommand(0x12);
  waitBusy("Software Reset");

  // 3. Driver Output Control (200x200)
  sendCommand(0x01);
  sendData(0xC7); // 199
  sendData(0x00);
  sendData(0x00);

  // 4. Data Entry Mode: X increment, Y increment
  sendCommand(0x11);
  sendData(0x03);

  // 5. Set RAM Area
  sendCommand(0x44); // RAM X: 0x00 .. 0x18 (25 bytes = 200 pixels)
  sendData(0x00);
  sendData(0x18);

  sendCommand(0x45); // RAM Y: 0x00 .. 0xC7 (199 lines = 200 pixels)
  sendData(0x00);
  sendData(0x00);
  sendData(0xC7);
  sendData(0x00);

  // 6. Border Waveform
  sendCommand(0x3C);
  sendData(0x05);

  // 7. Write to BOTH RAM buffers:
  //    SSD1681 requires 0x24 (current) AND 0x26 (previous/red) for a full clean refresh!
  
  // Set RAM counters
  sendCommand(0x4E); sendData(0x00);
  sendCommand(0x4F); sendData(0x00); sendData(0x00);

  // Frame Buffer 1 (0x24): Black square with white border
  sendCommand(0x24);
  SPI.beginTransaction(EPD_SPI_SETTINGS);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  for (int y = 0; y < 200; y++) {
    for (int x = 0; x < 25; x++) {
      // 0x00 = Black, 0xFF = White in SSD1681
      if (y < 10 || y >= 190 || x == 0 || x == 24) {
        SPI.transfer(0xFF); // White frame border
      } else {
        SPI.transfer(0x00); // Black center
      }
    }
  }
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.endTransaction();

  // Frame Buffer 2 (0x26): Set to 0x00
  sendCommand(0x4E); sendData(0x00);
  sendCommand(0x4F); sendData(0x00); sendData(0x00);
  sendCommand(0x26);
  SPI.beginTransaction(EPD_SPI_SETTINGS);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  for (int i = 0; i < 5000; i++) {
    SPI.transfer(0x00);
  }
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.endTransaction();

  // 8. Display Update Sequence (0xF7: Enable Clock ➔ Enable CP ➔ Load LUT ➔ Display ➔ Disable CP ➔ Disable Clock)
  Serial.println("[EPD] Triggering Full Refresh (Watch screen flash black/white now)...");
  sendCommand(0x22);
  sendData(0xF7);
  sendCommand(0x20); // Master Activation
  waitBusy("Full Display Refresh");

  Serial.println("[EPD] Refresh finished! You should see a crisp White Border with a Black Center.");
}

void loop() {
  // idle
}
