#include "midi_io.h"

#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
#include "usb/usb_host.h"
#endif

#if ENABLE_BLE_MIDI
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define BLE_MIDI_SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define BLE_MIDI_CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"
#endif

// --- Static Member Definitions ---
HardwareSerial MidiIO::s_midiSerial(1);
NoteOnCallback MidiIO::s_noteOnCb = nullptr;
NoteOffCallback MidiIO::s_noteOffCb = nullptr;
ControlChangeCallback MidiIO::s_ccCb = nullptr;
PitchBendCallback MidiIO::s_pitchBendCb = nullptr;

NoteOnCallback MidiIO::s_outTapNoteOnCb = nullptr;
NoteOffCallback MidiIO::s_outTapNoteOffCb = nullptr;

PatchbayConfig MidiIO::s_patchbay;
uint8_t MidiIO::s_lastLearnedCC = 74; // Default to Cutoff CC 74
uint8_t MidiIO::s_lastActiveNote = 60;
uint8_t MidiIO::s_lastActiveVel = 0;

// ==============================================================================
// USB Host Multi-Device Slot State (Supports Direct OTG or Multi-Port USB Hub)
// ==============================================================================
static const uint8_t MAX_USB_SLOTS = 4;

struct UsbDeviceSlot {
  bool connected;
  uint8_t devAddr;
  char productName[24];
#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
  usb_device_handle_t devHandle;
  int interfaceNum;
  uint8_t epInAddr;
  uint16_t epInMaxPacket;
  uint8_t epOutAddr;
  uint16_t epOutMaxPacket;
  usb_transfer_t* inTransfer;
  usb_transfer_t* outTransfer;
  bool outBusy;
#endif
};

static UsbDeviceSlot s_usbSlots[MAX_USB_SLOTS];

#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
static usb_host_client_handle_t s_usbClientHandle = nullptr;
static bool s_usbHostReady = false;

// Convert UTF-16LE USB String Descriptor to clean ASCII
static void utf16ToAscii(const usb_str_desc_t* strDesc, char* outBuf, size_t maxLen) {
  if (!strDesc || maxLen == 0) return;
  int charCount = (strDesc->bLength - 2) / 2;
  size_t outIdx = 0;
  for (int i = 0; i < charCount && outIdx < (maxLen - 1); i++) {
    uint16_t ch = strDesc->wData[i];
    if (ch >= 32 && ch <= 126) {
      outBuf[outIdx++] = (char)ch;
    }
  }
  outBuf[outIdx] = '\0';
}

// Callback when a USB MIDI Bulk IN transfer completes
static void usbInTransferCallback(usb_transfer_t* transfer) {
  if (!transfer) return;
  int slotIdx = (int)(intptr_t)transfer->context;
  if (slotIdx < 0 || slotIdx >= MAX_USB_SLOTS) return;
  UsbDeviceSlot& slot = s_usbSlots[slotIdx];
  if (!slot.connected) return;

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes >= 4) {
    // Parse 4-byte USB-MIDI Event Packets
    uint8_t srcPort = (slotIdx == 0) ? IN_PORT_USB_1 : IN_PORT_USB_2;
    for (int i = 0; i + 3 < transfer->actual_num_bytes; i += 4) {
      uint8_t header = transfer->data_buffer[i];
      uint8_t b0     = transfer->data_buffer[i + 1];
      uint8_t b1     = transfer->data_buffer[i + 2];
      uint8_t b2     = transfer->data_buffer[i + 3];

      uint8_t cin = header & 0x0F;
      if (cin == 0) continue;

      uint8_t status = b0 & 0xF0;
      uint8_t ch     = (b0 & 0x0F) + 1;

      switch (status) {
        case 0x90: // Note On
          if (b2 > 0) MidiIO::dispatchIncomingNoteOn(srcPort, ch, b1 & 0x7F, b2 & 0x7F);
          else        MidiIO::dispatchIncomingNoteOff(srcPort, ch, b1 & 0x7F);
          break;
        case 0x80: // Note Off
          MidiIO::dispatchIncomingNoteOff(srcPort, ch, b1 & 0x7F);
          break;
        case 0xB0: // Control Change
          MidiIO::dispatchIncomingCC(srcPort, ch, b1 & 0x7F, b2 & 0x7F);
          break;
        case 0xE0: { // Pitch Bend
          int16_t val14 = (int16_t)((b1 & 0x7F) | ((b2 & 0x7F) << 7)) - 8192;
          MidiIO::dispatchIncomingPitchBend(srcPort, ch, val14);
          break;
        }
        default:
          break;
      }
    }
  }

  // Re-submit Bulk IN transfer to continue listening
  if (slot.connected && slot.devHandle) {
    usb_host_transfer_submit(transfer);
  }
}

static void usbOutTransferCallback(usb_transfer_t* transfer) {
  if (!transfer) return;
  int slotIdx = (int)(intptr_t)transfer->context;
  if (slotIdx >= 0 && slotIdx < MAX_USB_SLOTS) {
    s_usbSlots[slotIdx].outBusy = false;
  }
}

static void openUsbMidiDevice(uint8_t devAddr) {
  int slotIdx = -1;
  for (int i = 0; i < MAX_USB_SLOTS; i++) {
    if (!s_usbSlots[i].connected) {
      slotIdx = i;
      break;
    }
  }
  if (slotIdx < 0) return;

  UsbDeviceSlot& slot = s_usbSlots[slotIdx];
  memset(&slot, 0, sizeof(UsbDeviceSlot));
  slot.interfaceNum = -1;

  if (usb_host_device_open(s_usbClientHandle, devAddr, &slot.devHandle) != ESP_OK) {
    return;
  }

  // Read device info & product name string
  usb_device_info_t devInfo;
  if (usb_host_device_info(slot.devHandle, &devInfo) == ESP_OK && devInfo.str_desc_product) {
    utf16ToAscii(devInfo.str_desc_product, slot.productName, sizeof(slot.productName));
  }
  if (slot.productName[0] == '\0') {
    snprintf(slot.productName, sizeof(slot.productName), "USB MIDI #%d", devAddr);
  }

  // Parse configuration descriptor for MIDI Streaming (Class 0x01, SubClass 0x03) or Bulk endpoints
  const usb_config_desc_t* configDesc = nullptr;
  if (usb_host_get_active_config_desc(slot.devHandle, &configDesc) == ESP_OK && configDesc) {
    const uint8_t* p = (const uint8_t*)configDesc;
    int totalLen = configDesc->wTotalLength;
    int offset = 0;
    int currentIf = -1;
    int currentAlt = 0;
    bool isMidiInterface = false;

    while (offset + 2 <= totalLen) {
      uint8_t bLength = p[offset];
      uint8_t bDescriptorType = p[offset + 1];
      if (bLength == 0) break;

      if (bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE && offset + 9 <= totalLen) {
        const usb_intf_desc_t* intf = (const usb_intf_desc_t*)(p + offset);
        currentIf = intf->bInterfaceNumber;
        currentAlt = intf->bAlternateSetting;
        // Audio Class (0x01) + MIDI Streaming SubClass (0x03), or Vendor Class with Bulk endpoints
        if ((intf->bInterfaceClass == 0x01 && intf->bInterfaceSubClass == 0x03) ||
            (intf->bInterfaceClass == 0xFF && intf->bNumEndpoints >= 1)) {
          isMidiInterface = true;
          if (slot.interfaceNum < 0) {
            if (usb_host_interface_claim(s_usbClientHandle, slot.devHandle, currentIf, currentAlt) == ESP_OK) {
              slot.interfaceNum = currentIf;
            }
          }
        } else {
          isMidiInterface = false;
        }
      } else if (bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && isMidiInterface && offset + 7 <= totalLen) {
        const usb_ep_desc_t* ep = (const usb_ep_desc_t*)(p + offset);
        uint8_t epType = ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK;
        if (epType == USB_BM_ATTRIBUTES_XFER_BULK || epType == USB_BM_ATTRIBUTES_XFER_INT) {
          if (ep->bEndpointAddress & 0x80) {
            if (slot.epInAddr == 0) {
              slot.epInAddr = ep->bEndpointAddress;
              slot.epInMaxPacket = ep->wMaxPacketSize & 0x7FF;
            }
          } else {
            if (slot.epOutAddr == 0) {
              slot.epOutAddr = ep->bEndpointAddress;
              slot.epOutMaxPacket = ep->wMaxPacketSize & 0x7FF;
            }
          }
        }
      }
      offset += bLength;
    }
  }

  slot.devAddr = devAddr;
  slot.connected = true;

  // Allocate & start Bulk IN transfer if device provides MIDI IN
  if (slot.epInAddr != 0 && slot.epInMaxPacket > 0) {
    if (usb_host_transfer_alloc(slot.epInMaxPacket, 0, &slot.inTransfer) == ESP_OK) {
      slot.inTransfer->device_handle = slot.devHandle;
      slot.inTransfer->bEndpointAddress = slot.epInAddr;
      slot.inTransfer->callback = usbInTransferCallback;
      slot.inTransfer->context = (void*)(intptr_t)slotIdx;
      slot.inTransfer->num_bytes = slot.epInMaxPacket;
      usb_host_transfer_submit(slot.inTransfer);
    }
  }

  // Allocate Bulk OUT transfer if device accepts MIDI OUT (e.g. USB Synth / FM-1)
  if (slot.epOutAddr != 0) {
    usb_host_transfer_alloc(64, 0, &slot.outTransfer);
  }

  Serial.printf("[USB HOST] Connected Slot %d: '%s' (Addr:%d, IN:0x%02X, OUT:0x%02X)\n",
                slotIdx + 1, slot.productName, devAddr, slot.epInAddr, slot.epOutAddr);
}

static void closeUsbMidiDevice(usb_device_handle_t devHandle) {
  for (int i = 0; i < MAX_USB_SLOTS; i++) {
    if (s_usbSlots[i].connected && s_usbSlots[i].devHandle == devHandle) {
      UsbDeviceSlot& slot = s_usbSlots[i];
      Serial.printf("[USB HOST] Disconnected Slot %d: '%s'\n", i + 1, slot.productName);
      slot.connected = false;
      if (slot.inTransfer) {
        usb_host_transfer_free(slot.inTransfer);
        slot.inTransfer = nullptr;
      }
      if (slot.outTransfer) {
        usb_host_transfer_free(slot.outTransfer);
        slot.outTransfer = nullptr;
      }
      if (slot.interfaceNum >= 0) {
        usb_host_interface_release(s_usbClientHandle, slot.devHandle, slot.interfaceNum);
      }
      usb_host_device_close(s_usbClientHandle, slot.devHandle);
      slot.devHandle = nullptr;
      break;
    }
  }
}

static void usbClientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg) {
  if (eventMsg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    openUsbMidiDevice(eventMsg->new_dev.address);
  } else if (eventMsg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    closeUsbMidiDevice(eventMsg->dev_gone.dev_hdl);
  }
}

// Background task handling ESP32-S3 USB Host library daemon events
static void usbHostLibTask(void* arg) {
  while (true) {
    uint32_t eventFlags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
  }
}
#endif // ENABLE_USB_HOST

// ==============================================================================
// Bluetooth Low Energy (BLE) MIDI Service Implementation
// ==============================================================================
static bool s_bleConnected = false;

#if ENABLE_BLE_MIDI
static BLECharacteristic* s_pBleMidiChar = nullptr;

class BleMidiServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    s_bleConnected = true;
    Serial.println("[BLE MIDI] Wireless device connected.");
  }
  void onDisconnect(BLEServer* pServer) override {
    s_bleConnected = false;
    Serial.println("[BLE MIDI] Wireless device disconnected. Restarting advertising...");
    BLEDevice::startAdvertising();
  }
};

class BleMidiCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String val = pChar->getValue();
    size_t len = val.length();
    if (len < 3) return;
    const uint8_t* data = (const uint8_t*)val.c_str();

    // Parse BLE-MIDI packet: [header][timestamp][status][data1][data2]...
    for (size_t i = 2; i < len; i++) {
      uint8_t b = data[i];
      if ((b & 0x80) && b != 0xF7) {
        uint8_t status = b & 0xF0;
        uint8_t ch = (b & 0x0F) + 1;
        if (status == 0x90 && i + 2 < len) {
          uint8_t note = data[i + 1] & 0x7F;
          uint8_t vel  = data[i + 2] & 0x7F;
          if (vel > 0) MidiIO::dispatchIncomingNoteOn(IN_PORT_BLE, ch, note, vel);
          else         MidiIO::dispatchIncomingNoteOff(IN_PORT_BLE, ch, note);
          i += 2;
        } else if (status == 0x80 && i + 2 < len) {
          MidiIO::dispatchIncomingNoteOff(IN_PORT_BLE, ch, data[i + 1] & 0x7F);
          i += 2;
        } else if (status == 0xB0 && i + 2 < len) {
          MidiIO::dispatchIncomingCC(IN_PORT_BLE, ch, data[i + 1] & 0x7F, data[i + 2] & 0x7F);
          i += 2;
        } else if (status == 0xE0 && i + 2 < len) {
          int16_t bend = (int16_t)((data[i + 1] & 0x7F) | ((data[i + 2] & 0x7F) << 7)) - 8192;
          MidiIO::dispatchIncomingPitchBend(IN_PORT_BLE, ch, bend);
          i += 2;
        }
      }
    }
  }
};
#endif // ENABLE_BLE_MIDI

// ==============================================================================
// MidiIO Core Methods
// ==============================================================================

void MidiIO::resetPatchbayDefaults() {
  s_patchbay.inPort = IN_PORT_ALL;
  s_patchbay.inChannel = 0; // 0 = Omni (all channels)
  s_patchbay.outPort = OUT_PORT_ALL;
  s_patchbay.outChannel = 1; // Channel 1 for FM-1
  for (int i = 0; i < 128; i++) {
    s_patchbay.ccMap[i] = i; // Identity 1:1 mapping by default
  }
}

void MidiIO::init() {
  resetPatchbayDefaults();
  memset(s_usbSlots, 0, sizeof(s_usbSlots));

  // 1. Initialize Hardware UART 1 for 31,250 baud TRS MIDI OUT (GPIO 17)
  s_midiSerial.begin(MIDI_BAUD_RATE, SERIAL_8N1, -1, PIN_MIDI_TX);

  // 2. Initialize Native ESP32-S3 USB Host Stack
#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
  usb_host_config_t hostConfig = {};
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;

  if (usb_host_install(&hostConfig) == ESP_OK) {
    xTaskCreatePinnedToCore(usbHostLibTask, "usb_lib", 4096, nullptr, 5, nullptr, 0);

    usb_host_client_config_t clientConfig = {};
    clientConfig.is_synchronous = false;
    clientConfig.max_num_event_msg = 8;
    clientConfig.async.client_event_callback = usbClientEventCallback;
    clientConfig.async.callback_arg = nullptr;

    if (usb_host_client_register(&clientConfig, &s_usbClientHandle) == ESP_OK) {
      s_usbHostReady = true;
      Serial.println("[OK] Native ESP32-S3 USB Host MIDI driver active.");
    }
  }
#endif

  // 3. Initialize Bluetooth Low Energy (BLE) MIDI Service
#if ENABLE_BLE_MIDI
  BLEDevice::init("S3 MIDI Brain");
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BleMidiServerCallbacks());

  BLEService* pService = pServer->createService(BLEUUID(BLE_MIDI_SERVICE_UUID));
  s_pBleMidiChar = pService->createCharacteristic(
    BLEUUID(BLE_MIDI_CHARACTERISTIC_UUID),
    BLECharacteristic::PROPERTY_READ   |
    BLECharacteristic::PROPERTY_WRITE  |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_WRITE_NR
  );
  s_pBleMidiChar->addDescriptor(new BLE2902());
  s_pBleMidiChar->setCallbacks(new BleMidiCharCallbacks());
  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLEUUID(BLE_MIDI_SERVICE_UUID));
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // Fast iOS/BLE connection interval
  BLEDevice::startAdvertising();
  Serial.println("[OK] BLE Wireless MIDI advertising as 'S3 MIDI Brain'.");
#endif
}

void MidiIO::update() {
#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
  if (s_usbHostReady && s_usbClientHandle) {
    usb_host_client_handle_events(s_usbClientHandle, 0);
  }
#endif
}

// --- Input Filtering & Dispatch ---

bool MidiIO::shouldAcceptInput(uint8_t srcPort, uint8_t& channel) {
  // Check input port filter
  if (s_patchbay.inPort != IN_PORT_ALL) {
    if (s_patchbay.inPort == IN_PORT_USB_ANY) {
      if (srcPort != IN_PORT_USB_1 && srcPort != IN_PORT_USB_2) return false;
    } else if (s_patchbay.inPort != srcPort) {
      return false;
    }
  }

  // Check input channel filter (0 = Omni)
  if (s_patchbay.inChannel != 0 && channel != s_patchbay.inChannel) {
    return false;
  }

  return true;
}

uint8_t MidiIO::resolveOutputChannel(uint8_t requestedChannel) {
  if (s_patchbay.outChannel >= 1 && s_patchbay.outChannel <= 16) {
    return s_patchbay.outChannel;
  }
  return constrain(requestedChannel, 1, 16);
}

void MidiIO::dispatchIncomingNoteOn(uint8_t srcPort, uint8_t channel, uint8_t pitch, uint8_t velocity) {
  if (!shouldAcceptInput(srcPort, channel)) return;
  s_lastActiveNote = pitch;
  s_lastActiveVel = velocity;
  if (s_noteOnCb) s_noteOnCb(channel, pitch, velocity);
}

void MidiIO::dispatchIncomingNoteOff(uint8_t srcPort, uint8_t channel, uint8_t pitch) {
  if (!shouldAcceptInput(srcPort, channel)) return;
  s_lastActiveVel = 0;
  if (s_noteOffCb) s_noteOffCb(channel, pitch);
}

void MidiIO::dispatchIncomingCC(uint8_t srcPort, uint8_t channel, uint8_t control, uint8_t value) {
  if (!shouldAcceptInput(srcPort, channel)) return;
  uint8_t cleanCC = control & 0x7F;
  s_lastLearnedCC = cleanCC; // Capture automatically for MIDI Learn!
  uint8_t mappedCC = s_patchbay.ccMap[cleanCC] & 0x7F;
  if (s_ccCb) s_ccCb(channel, mappedCC, value & 0x7F);
}

void MidiIO::dispatchIncomingPitchBend(uint8_t srcPort, uint8_t channel, int16_t bend) {
  if (!shouldAcceptInput(srcPort, channel)) return;
  if (s_pitchBendCb) s_pitchBendCb(channel, bend);
}

// --- Multi-Port MIDI Output Methods ---

void MidiIO::sendUsbMidiPacket(uint8_t cin, uint8_t b0, uint8_t b1, uint8_t b2) {
#if ENABLE_USB_HOST && defined(CONFIG_IDF_TARGET_ESP32S3)
  for (int i = 0; i < MAX_USB_SLOTS; i++) {
    UsbDeviceSlot& slot = s_usbSlots[i];
    if (slot.connected && slot.epOutAddr != 0 && slot.outTransfer && !slot.outBusy) {
      slot.outTransfer->device_handle = slot.devHandle;
      slot.outTransfer->bEndpointAddress = slot.epOutAddr;
      slot.outTransfer->callback = usbOutTransferCallback;
      slot.outTransfer->context = (void*)(intptr_t)i;
      slot.outTransfer->num_bytes = 4;
      slot.outTransfer->data_buffer[0] = cin & 0x0F;
      slot.outTransfer->data_buffer[1] = b0;
      slot.outTransfer->data_buffer[2] = b1;
      slot.outTransfer->data_buffer[3] = b2;
      slot.outBusy = true;
      if (usb_host_transfer_submit(slot.outTransfer) != ESP_OK) {
        slot.outBusy = false;
      }
    }
  }
#endif
}

void MidiIO::sendBleMidiPacket(const uint8_t* midiBytes, size_t len) {
#if ENABLE_BLE_MIDI
  if (!s_bleConnected || !s_pBleMidiChar || len == 0 || len > 16) return;
  uint8_t packet[20];
  uint16_t ts = millis() & 0x1FFF;
  packet[0] = 0x80 | ((ts >> 7) & 0x3F); // BLE MIDI Header
  packet[1] = 0x80 | (ts & 0x7F);        // BLE MIDI Timestamp
  memcpy(&packet[2], midiBytes, len);
  s_pBleMidiChar->setValue(packet, len + 2);
  s_pBleMidiChar->notify();
#endif
}

void MidiIO::sendNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity) {
  uint8_t ch = resolveOutputChannel(channel);
  uint8_t status = 0x90 | (ch - 1);
  uint8_t p = pitch & 0x7F;
  uint8_t v = velocity & 0x7F;

  // Notify Visualizer Output Tap
  if (s_outTapNoteOnCb) s_outTapNoteOnCb(ch, p, v);

  uint8_t outMode = s_patchbay.outPort;

  // 1. Wired 3.5mm TRS Serial Output
  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_TRS_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    s_midiSerial.write(status);
    s_midiSerial.write(p);
    s_midiSerial.write(v);
  }

  // 2. USB Host Output
  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_USB_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    sendUsbMidiPacket(0x09, status, p, v);
  }

  // 3. BLE Wireless Output
  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_BLE_ONLY) {
    uint8_t msg[3] = {status, p, v};
    sendBleMidiPacket(msg, 3);
  }
}

void MidiIO::sendNoteOff(uint8_t channel, uint8_t pitch) {
  uint8_t ch = resolveOutputChannel(channel);
  uint8_t status = 0x80 | (ch - 1);
  uint8_t p = pitch & 0x7F;

  // Notify Visualizer Output Tap
  if (s_outTapNoteOffCb) s_outTapNoteOffCb(ch, p);

  uint8_t outMode = s_patchbay.outPort;

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_TRS_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    s_midiSerial.write(status);
    s_midiSerial.write(p);
    s_midiSerial.write((uint8_t)0);
  }

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_USB_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    sendUsbMidiPacket(0x08, status, p, 0);
  }

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_BLE_ONLY) {
    uint8_t msg[3] = {status, p, 0};
    sendBleMidiPacket(msg, 3);
  }
}

void MidiIO::sendControlChange(uint8_t channel, uint8_t control, uint8_t value) {
  uint8_t ch = resolveOutputChannel(channel);
  uint8_t status = 0xB0 | (ch - 1);
  uint8_t c = control & 0x7F;
  uint8_t v = value & 0x7F;

  uint8_t outMode = s_patchbay.outPort;

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_TRS_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    s_midiSerial.write(status);
    s_midiSerial.write(c);
    s_midiSerial.write(v);
  }

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_USB_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    sendUsbMidiPacket(0x0B, status, c, v);
  }

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_BLE_ONLY) {
    uint8_t msg[3] = {status, c, v};
    sendBleMidiPacket(msg, 3);
  }
}

static int16_t s_tuningBendOffset = 0;
static int16_t s_lfoBendOffset = 0;

void MidiIO::setTuningBendOffset(uint8_t channel, int16_t tuningBend) {
  s_tuningBendOffset = tuningBend;
  sendPitchBend(channel, s_tuningBendOffset + s_lfoBendOffset);
}

void MidiIO::setLfoBendOffset(uint8_t channel, int16_t lfoBend) {
  s_lfoBendOffset = lfoBend;
  sendPitchBend(channel, s_tuningBendOffset + s_lfoBendOffset);
}

void MidiIO::sendPitchBend(uint8_t channel, int16_t bend) {
  uint8_t ch = resolveOutputChannel(channel);
  int16_t clamped = constrain(bend, -8192, 8191);
  uint16_t val14 = (uint16_t)(clamped + 8192);
  uint8_t lsb = val14 & 0x7F;
  uint8_t msb = (val14 >> 7) & 0x7F;
  uint8_t status = 0xE0 | (ch - 1);

  uint8_t outMode = s_patchbay.outPort;

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_TRS_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    s_midiSerial.write(status);
    s_midiSerial.write(lsb);
    s_midiSerial.write(msb);
  }

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_USB_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    sendUsbMidiPacket(0x0E, status, lsb, msb);
  }

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_BLE_ONLY) {
    uint8_t msg[3] = {status, lsb, msb};
    sendBleMidiPacket(msg, 3);
  }
}

void MidiIO::sendProgramChange(uint8_t channel, uint8_t program) {
  uint8_t ch = resolveOutputChannel(channel);
  uint8_t status = 0xC0 | (ch - 1);
  uint8_t pgm = program & 0x7F;

  uint8_t outMode = s_patchbay.outPort;

  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_TRS_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    s_midiSerial.write(status);
    s_midiSerial.write(pgm);
  }
  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_USB_ONLY || outMode == OUT_PORT_TRS_AND_USB) {
    sendUsbMidiPacket(0x0C, status, pgm, 0);
  }
  if (outMode == OUT_PORT_ALL || outMode == OUT_PORT_BLE_ONLY) {
    uint8_t msg[2] = {status, pgm};
    sendBleMidiPacket(msg, 2);
  }
}

void MidiIO::sendSysEx(const uint8_t* data, size_t length) {
  if (!data || length == 0) return;
  s_midiSerial.write(data, length);
}

void MidiIO::panic() {
  for (uint8_t ch = 1; ch <= 16; ch++) {
    uint8_t statusB = 0xB0 | (ch - 1);
    s_midiSerial.write(statusB); s_midiSerial.write(123); s_midiSerial.write((uint8_t)0);
    s_midiSerial.write(statusB); s_midiSerial.write(120); s_midiSerial.write((uint8_t)0);

    uint8_t statusE = 0xE0 | (ch - 1);
    s_midiSerial.write(statusE); s_midiSerial.write((uint8_t)0); s_midiSerial.write((uint8_t)64);
  }
}

void MidiIO::setNoteOnCallback(NoteOnCallback cb) { s_noteOnCb = cb; }
void MidiIO::setNoteOffCallback(NoteOffCallback cb) { s_noteOffCb = cb; }
void MidiIO::setControlChangeCallback(ControlChangeCallback cb) { s_ccCb = cb; }
void MidiIO::setPitchBendCallback(PitchBendCallback cb) { s_pitchBendCb = cb; }
void MidiIO::setOutputMonitorCallbacks(NoteOnCallback noteOnTap, NoteOffCallback noteOffTap) {
  s_outTapNoteOnCb = noteOnTap;
  s_outTapNoteOffCb = noteOffTap;
}

PatchbayConfig& MidiIO::getPatchbayConfig() { return s_patchbay; }
uint8_t MidiIO::getLastLearnedCC() { return s_lastLearnedCC; }
uint8_t MidiIO::getLastActiveNote() { return s_lastActiveNote; }
uint8_t MidiIO::getLastActiveVelocity() { return s_lastActiveVel; }

const char* MidiIO::getConnectedDeviceName() {
  for (int i = 0; i < MAX_USB_SLOTS; i++) {
    if (s_usbSlots[i].connected && s_usbSlots[i].productName[0] != '\0') {
      return s_usbSlots[i].productName;
    }
  }
  if (s_bleConnected) return "BLE MIDI Connected";
  return "Waiting USB/BLE";
}

const char* MidiIO::getUsbDeviceName(uint8_t slot) {
  if (slot < MAX_USB_SLOTS && s_usbSlots[slot].connected) {
    return s_usbSlots[slot].productName;
  }
  return "Empty";
}

uint8_t MidiIO::getConnectedUsbCount() {
  uint8_t count = 0;
  for (int i = 0; i < MAX_USB_SLOTS; i++) {
    if (s_usbSlots[i].connected) count++;
  }
  return count;
}

bool MidiIO::isBleConnected() { return s_bleConnected; }

void MidiIO::triggerVirtualNoteOn(uint8_t pitch, uint8_t velocity) {
  dispatchIncomingNoteOn(IN_PORT_ALL, 1, pitch, velocity);
}

void MidiIO::triggerVirtualNoteOff(uint8_t pitch) {
  dispatchIncomingNoteOff(IN_PORT_ALL, 1, pitch);
}
