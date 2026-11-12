/*
 * ==============================================================================
 * MIDI Hub Light: Ultra-low latency USB Host & BLE Bridge to M-VAVE FM-1
 * ==============================================================================
 * 
 * Features:
 *   - Native USB Host (Left OTG port) connects USB MIDI keyboards/controllers
 *   - BLE Central connects wireless controllers (e.g. SMC-PAD Pocket)
 *   - Simultaneous Dual Output:
 *       1. Wired 3.5mm TRS Type-A (GPIO 17) directly to FM-1 MIDI IN
 *       2. BLE Peripheral ("S3 MIDI Hub") so apps/tablets/synths can listen wirelessly
 *   - Optional: BLE Central Out to connect wirelessly directly to FM-1!
 *   - Zero display overhead, zero complex UI, zero button ladders.
 * 
 * Hardware Pinout:
 *   ESP32-S3 GPIO 17 ──[ 33Ω or 47Ω ]──► 3.5mm Jack TIP
 *   ESP32-S3 +3.3V   ──[ 33Ω or 47Ω ]──► 3.5mm Jack RING
 *   ESP32-S3 GND     ──────────────────► 3.5mm Jack SLEEVE
 *   Native USB-C (Left Port) ──────────► USB MIDI Keyboard / Pads via OTG
 */

#include <Arduino.h>

#define ENABLE_USB_HOST 1
#define ENABLE_BLE_MIDI 1

#define PIN_MIDI_TX    17
#define MIDI_BAUD_RATE 31250

// Option: If true, connects to FM-1 via Bluetooth instead of wired cable!
// Default is false because you have the 3.5mm cable plugged in for 0 latency.
#define CONNECT_TO_FM1_OVER_BLE 0

// --- Hardware Serial TRS MIDI Out ---
#define MidiSerial Serial1

#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
#include "usb/usb_host.h"

#define USB_BUF_SIZE 64
static usb_host_client_handle_t s_clientHandle = nullptr;
static usb_device_handle_t s_devHandle = nullptr;
static uint8_t s_epInAddr = 0;
static uint8_t s_epOutAddr = 0;
static urb_t* s_inUrb = nullptr;
static bool s_devConnected = false;
static bool s_inBusy = false;
static uint8_t s_inBuf[USB_BUF_SIZE];
#endif

#if ENABLE_BLE_MIDI
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define BLE_MIDI_SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define BLE_MIDI_CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

static BLECharacteristic* s_pServerMidiChar = nullptr;
static bool s_serverConnected = false;

// Client (Central) connection to controller (e.g. SMC-PAD Pocket)
static BLEClient* s_pPadClient = nullptr;
static BLERemoteCharacteristic* s_pPadRemoteChar = nullptr;
static bool s_padConnected = false;
static bool s_padDoConnect = false;
static BLEAdvertisedDevice* s_pFoundPadDevice = nullptr;

// Client (Central) connection to FM-1 (if wireless output enabled)
#if CONNECT_TO_FM1_OVER_BLE
static BLEClient* s_pFm1Client = nullptr;
static BLERemoteCharacteristic* s_pFm1RemoteChar = nullptr;
static bool s_fm1BleConnected = false;
static bool s_fm1DoConnect = false;
static BLEAdvertisedDevice* s_pFoundFm1Device = nullptr;
#endif

static bool s_scanning = false;
static uint32_t s_lastScanTime = 0;
#endif

// --- Forward Declarations ---
void sendMidiOut(uint8_t status, uint8_t d1, uint8_t d2, bool hasD2 = true);

// ==============================================================================
// Core MIDI Output Dispatch (Sends out Wired TRS + BLE simultaneously)
// ==============================================================================
void sendMidiOut(uint8_t status, uint8_t d1, uint8_t d2, bool hasD2) {
  // 1. Send Wired 3.5mm TRS Output
  MidiSerial.write(status);
  MidiSerial.write(d1);
  if (hasD2) MidiSerial.write(d2);

#if ENABLE_BLE_MIDI
  // 2. Send over BLE Server (if any phone/tablet/computer is connected to S3)
  if (s_serverConnected && s_pServerMidiChar) {
    uint8_t packet[5];
    uint16_t ts = millis() & 0x1FFF;
    packet[0] = 0x80 | ((ts >> 7) & 0x3F);
    packet[1] = 0x80 | (ts & 0x7F);
    packet[2] = status;
    packet[3] = d1;
    if (hasD2) {
      packet[4] = d2;
      s_pServerMidiChar->setValue(packet, 5);
    } else {
      s_pServerMidiChar->setValue(packet, 4);
    }
    s_pServerMidiChar->notify();
  }

#if CONNECT_TO_FM1_OVER_BLE
  // 3. Send over BLE to FM-1 (if paired wirelessly as central)
  if (s_fm1BleConnected && s_pFm1RemoteChar) {
    uint8_t packet[5];
    uint16_t ts = millis() & 0x1FFF;
    packet[0] = 0x80 | ((ts >> 7) & 0x3F);
    packet[1] = 0x80 | (ts & 0x7F);
    packet[2] = status;
    packet[3] = d1;
    if (hasD2) {
      packet[4] = d2;
      s_pFm1RemoteChar->writeValue(packet, 5, false);
    } else {
      s_pFm1RemoteChar->writeValue(packet, 4, false);
    }
  }
#endif
#endif
}

// ==============================================================================
// BLE MIDI Handling
// ==============================================================================
#if ENABLE_BLE_MIDI
static void parseBleMidiStream(const uint8_t* data, size_t len) {
  if (!data || len < 3) return;
  for (size_t i = 2; i < len; i++) {
    uint8_t b = data[i];
    if ((b & 0x80) && b != 0xF7) {
      if (i + 1 < len && (data[i + 1] & 0x80) && data[i + 1] != 0xF7) continue;
      uint8_t status = b & 0xF0;
      if ((status == 0x90 || status == 0x80 || status == 0xB0 || status == 0xE0) && (i + 2 < len)) {
        sendMidiOut(b, data[i + 1] & 0x7F, data[i + 2] & 0x7F, true);
        i += 2;
      } else if ((status == 0xC0 || status == 0xD0) && (i + 1 < len)) {
        sendMidiOut(b, data[i + 1] & 0x7F, 0, false);
        i += 1;
      }
    }
  }
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    s_serverConnected = true;
    Serial.println("[BLE] Wireless Device connected to S3 Hub!");
  }
  void onDisconnect(BLEServer* pServer) override {
    s_serverConnected = false;
    Serial.println("[BLE] Device disconnected. Restarting advertising...");
    BLEDevice::startAdvertising();
  }
};

class ServerCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String v = pChar->getValue();
    parseBleMidiStream((const uint8_t*)v.c_str(), v.length());
  }
};

static void padNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify) {
  parseBleMidiStream(pData, len);
}

class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (!dev.haveServiceUUID() || !dev.isAdvertisingService(BLEUUID(BLE_MIDI_SERVICE_UUID))) return;
    String name = dev.getName();

#if CONNECT_TO_FM1_OVER_BLE
    if (!s_fm1BleConnected && !s_fm1DoConnect && (name.indexOf("FM-1") >= 0 || name.indexOf("FM1") >= 0)) {
      Serial.printf("[BLE] Discovered FM-1: %s\n", dev.getAddress().toString().c_str());
      if (s_pFoundFm1Device) delete s_pFoundFm1Device;
      s_pFoundFm1Device = new BLEAdvertisedDevice(dev);
      s_fm1DoConnect = true;
      return;
    }
#else
    if (name.indexOf("FM-1") >= 0 || name.indexOf("FM1") >= 0) return; // Skip FM-1 in favor of wired
#endif

    if (!s_padConnected && !s_padDoConnect) {
      Serial.printf("[BLE] Discovered Controller: %s (%s)\n", name.c_str(), dev.getAddress().toString().c_str());
      if (s_pFoundPadDevice) delete s_pFoundPadDevice;
      s_pFoundPadDevice = new BLEAdvertisedDevice(dev);
      s_padDoConnect = true;
    }
  }
};

static void scanCompleteCb(BLEScanResults res) {
  s_scanning = false;
}

static bool connectToPad() {
  if (!s_pFoundPadDevice) return false;
  if (!s_pPadClient) s_pPadClient = BLEDevice::createClient();

  Serial.printf("[BLE] Connecting to Controller %s...\n", s_pFoundPadDevice->getAddress().toString().c_str());
  if (!s_pPadClient->connect(s_pFoundPadDevice)) return false;

  BLERemoteService* pService = s_pPadClient->getService(BLEUUID(BLE_MIDI_SERVICE_UUID));
  if (!pService) { s_pPadClient->disconnect(); return false; }

  s_pPadRemoteChar = pService->getCharacteristic(BLEUUID(BLE_MIDI_CHARACTERISTIC_UUID));
  if (!s_pPadRemoteChar || !s_pPadRemoteChar->canNotify()) { s_pPadClient->disconnect(); return false; }

  s_pPadRemoteChar->registerForNotify(padNotifyCallback);
  s_padConnected = true;
  Serial.println("[BLE] CONNECTED to wireless controller!");
  return true;
}

#if CONNECT_TO_FM1_OVER_BLE
static bool connectToFm1() {
  if (!s_pFoundFm1Device) return false;
  if (!s_pFm1Client) s_pFm1Client = BLEDevice::createClient();

  Serial.printf("[BLE] Connecting to FM-1 %s...\n", s_pFoundFm1Device->getAddress().toString().c_str());
  if (!s_pFm1Client->connect(s_pFoundFm1Device)) return false;

  BLERemoteService* pService = s_pFm1Client->getService(BLEUUID(BLE_MIDI_SERVICE_UUID));
  if (!pService) { s_pFm1Client->disconnect(); return false; }

  s_pFm1RemoteChar = pService->getCharacteristic(BLEUUID(BLE_MIDI_CHARACTERISTIC_UUID));
  if (!s_pFm1RemoteChar) { s_pFm1Client->disconnect(); return false; }

  s_fm1BleConnected = true;
  Serial.println("[BLE] CONNECTED to FM-1 wirelessly!");
  return true;
}
#endif
#endif // ENABLE_BLE_MIDI

// ==============================================================================
// Native ESP32-S3 USB Host MIDI Handling
// ==============================================================================
#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
static void submitInTransfer();

static void inTransferCallback(urb_t* urb) {
  s_inBusy = false;
  if (urb->status == 0 && urb->actual_num_bytes >= 4) {
    for (int i = 0; i < urb->actual_num_bytes; i += 4) {
      uint8_t cin = urb->data_buffer[i] & 0x0F;
      uint8_t b1  = urb->data_buffer[i + 1];
      uint8_t b2  = urb->data_buffer[i + 2];
      uint8_t b3  = urb->data_buffer[i + 3];

      if (cin == 0x08 || cin == 0x09 || cin == 0x0B || cin == 0x0E) {
        sendMidiOut(b1, b2, b3, true);
      } else if (cin == 0x0C || cin == 0x0D) {
        sendMidiOut(b1, b2, 0, false);
      }
    }
  }
  if (s_devConnected && s_epInAddr != 0) {
    submitInTransfer();
  }
}

static void submitInTransfer() {
  if (s_inBusy || !s_devConnected || s_epInAddr == 0 || !s_inUrb) return;
  s_inUrb->device_handle = s_devHandle;
  s_inUrb->bEndpointAddress = s_epInAddr;
  s_inUrb->callback = inTransferCallback;
  s_inUrb->num_bytes = sizeof(s_inBuf);
  s_inBusy = true;
  if (usb_host_transfer_submit(s_inUrb) != ESP_OK) {
    s_inBusy = false;
  }
}

static void clientEventCb(const usb_host_client_event_msg_t* msg, void* arg) {
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    usb_host_device_open(s_clientHandle, msg->new_dev.address, &s_devHandle);
    const usb_config_desc_t* configDesc = nullptr;
    if (usb_host_get_active_config_descriptor(s_devHandle, &configDesc) == ESP_OK && configDesc) {
      const uint8_t* p = (const uint8_t*)configDesc;
      int totalLen = configDesc->wTotalLength;
      int offset = 0;
      while (offset < totalLen) {
        uint8_t bLen = p[offset];
        if (bLen == 0) break;
        uint8_t bType = p[offset + 1];
        if (bType == 0x05) { // Endpoint Descriptor
          const usb_ep_desc_t* ep = (const usb_ep_desc_t*)&p[offset];
          if (ep->bEndpointAddress & 0x80) {
            s_epInAddr = ep->bEndpointAddress;
          } else {
            s_epOutAddr = ep->bEndpointAddress;
          }
        }
        offset += bLen;
      }
      usb_host_interface_claim(s_clientHandle, s_devHandle, 1, 0);
      s_devConnected = true;
      Serial.printf("[USB] MIDI Keyboard connected! (EP In: 0x%02X)\n", s_epInAddr);
      submitInTransfer();
    }
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    s_devConnected = false;
    s_epInAddr = 0;
    Serial.println("[USB] MIDI Device disconnected.");
  }
}

static void usbHostTask(void* pvParameters) {
  while (1) {
    usb_host_lib_handle_events(portMAX_DELAY, nullptr);
  }
}
#endif

// ==============================================================================
// Arduino Setup & Loop
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n=======================================================");
  Serial.println("         MIDI Hub Light (Bridge to FM-1)               ");
  Serial.println("=======================================================");

  // 1. Initialize Wired 3.5mm TRS Output (GPIO 17 @ 31250 baud)
  MidiSerial.begin(MIDI_BAUD_RATE, SERIAL_8N1, -1, PIN_MIDI_TX);
  Serial.printf("[OK] Wired 3.5mm TRS Output ready on GPIO %d.\n", PIN_MIDI_TX);

  // 2. Initialize USB Host
#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
  usb_host_config_t hCfg = {};
  if (usb_host_install(&hCfg) == ESP_OK) {
    xTaskCreatePinnedToCore(usbHostTask, "usb_lib", 4096, nullptr, 5, nullptr, 0);
    usb_host_client_config_t cCfg = {};
    cCfg.async.client_event_callback = clientEventCb;
    if (usb_host_client_register(&cCfg, &s_clientHandle) == ESP_OK) {
      usb_host_transfer_alloc(USB_BUF_SIZE, 0, &s_inUrb);
      s_inUrb->data_buffer = s_inBuf;
      Serial.println("[OK] USB Host Stack ready on Left OTG port.");
    }
  }
#endif

  // 3. Initialize BLE
#if ENABLE_BLE_MIDI
  BLEDevice::init("S3 MIDI Hub");
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  BLEService* pService = pServer->createService(BLEUUID(BLE_MIDI_SERVICE_UUID));
  s_pServerMidiChar = pService->createCharacteristic(
    BLEUUID(BLE_MIDI_CHARACTERISTIC_UUID),
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE_NR
  );
  s_pServerMidiChar->addDescriptor(new BLE2902());
  s_pServerMidiChar->setCallbacks(new ServerCharCallbacks());
  pService->start();

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLEUUID(BLE_MIDI_SERVICE_UUID));
  pAdv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("[OK] BLE Peripheral advertising as 'S3 MIDI Hub'.");

  // BLE Central scan for controllers (and optionally FM-1)
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
  pScan->setInterval(1349);
  pScan->setWindow(449);
  pScan->setActiveScan(true);
  pScan->start(4, scanCompleteCb, false);
  s_scanning = true;
  s_lastScanTime = millis();
  Serial.println("[OK] BLE Central scanning for controllers...");
#endif

  Serial.println("\n[READY] Play your pads/keys — zero lag pass-through active!\n");
}

void loop() {
#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
  if (s_clientHandle) {
    usb_host_client_handle_events(s_clientHandle, 0);
  }
#endif

#if ENABLE_BLE_MIDI
  if (s_padDoConnect) {
    s_padDoConnect = false;
    connectToPad();
  }
#if CONNECT_TO_FM1_OVER_BLE
  if (s_fm1DoConnect) {
    s_fm1DoConnect = false;
    connectToFm1();
  }
#endif

  if (!s_padConnected && !s_scanning && (millis() - s_lastScanTime > 5000)) {
    BLEScan* pScan = BLEDevice::getScan();
    pScan->clearResults();
    pScan->start(4, scanCompleteCb, false);
    s_scanning = true;
    s_lastScanTime = millis();
  }
#endif
}
