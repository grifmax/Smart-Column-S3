#ifndef DS2482_100_H
#define DS2482_100_H

#include <Arduino.h>
#include <Wire.h>

class Ds2482_100 {
 public:
  bool begin(TwoWire& wire, uint8_t address);
  bool isOnline() const { return online_; }
  uint8_t address() const { return address_; }

  void resetSearch();
  bool search(uint8_t newAddress[8]);

  bool oneWireReset(bool* presence = nullptr);
  bool oneWireWriteByte(uint8_t value);
  bool oneWireReadByte(uint8_t& value);
  bool startConversionAll();
  bool startConversionByAddress(const uint8_t address[8]);
  bool readTemperatureC(const uint8_t address[8], float& valueC);

 private:
  static constexpr uint8_t PTR_STATUS = 0xF0;
  static constexpr uint8_t PTR_DATA = 0xE1;
  static constexpr uint8_t PTR_CONFIG = 0xC3;

  static constexpr uint8_t CFG_APU = 0x01;

  static constexpr uint8_t STATUS_BUSY = 0x01;
  static constexpr uint8_t STATUS_PPD = 0x02;
  static constexpr uint8_t STATUS_SBR = 0x20;
  static constexpr uint8_t STATUS_TSB = 0x40;
  static constexpr uint8_t STATUS_DIR = 0x80;

  static constexpr uint8_t CMD_RESET = 0xF0;
  static constexpr uint8_t CMD_SET_READ_PTR = 0xE1;
  static constexpr uint8_t CMD_WRITE_CONFIG = 0xD2;
  static constexpr uint8_t CMD_1WIRE_RESET = 0xB4;
  static constexpr uint8_t CMD_1WIRE_WRITE_BYTE = 0xA5;
  static constexpr uint8_t CMD_1WIRE_READ_BYTE = 0x96;
  static constexpr uint8_t CMD_1WIRE_TRIPLET = 0x78;

  static constexpr uint8_t ROM_SEARCH = 0xF0;
  static constexpr uint8_t ROM_SKIP = 0xCC;
  static constexpr uint8_t ROM_MATCH = 0x55;
  static constexpr uint8_t CMD_CONVERT_T = 0x44;
  static constexpr uint8_t CMD_READ_SCRATCHPAD = 0xBE;

  bool probeI2c();
  bool writeCommand(uint8_t command);
  bool writeCommand(uint8_t command, uint8_t arg);
  bool readCurrentRegister(uint8_t& value);
  bool setReadPointer(uint8_t pointer);
  bool readRegister(uint8_t pointer, uint8_t& value);
  bool waitUntilReady(uint16_t timeoutMs, bool setStatusPointer,
                      uint8_t* status = nullptr);
  bool deviceReset();
  bool writeConfig(uint8_t config);
  bool oneWireTriplet(bool direction, uint8_t& status);
  bool skipRom();
  bool matchRom(const uint8_t address[8]);
  bool readScratchpad(const uint8_t address[8], uint8_t scratchpad[9]);

  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  bool online_ = false;
  uint8_t searchAddress_[8] = {0};
  uint8_t lastDiscrepancy_ = 0;
  bool searchExhausted_ = false;
};

#endif  // DS2482_100_H
