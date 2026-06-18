#include "ds2482_100.h"

#include <OneWire.h>

namespace {

bool isSupportedDs18Family(uint8_t familyCode) {
  return familyCode == 0x28 || familyCode == 0x10 || familyCode == 0x22;
}

float decodeDs18TemperatureC(uint8_t familyCode, const uint8_t scratchpad[9]) {
  int16_t raw = static_cast<int16_t>((scratchpad[1] << 8) | scratchpad[0]);
  if (familyCode == 0x10) {
    raw <<= 3;
    if (scratchpad[7] == 0x10) {
      raw = (raw & 0xFFF0) + 12 - scratchpad[6];
    }
  }
  return static_cast<float>(raw) / 16.0f;
}

}  // namespace

bool Ds2482_100::probeI2c() {
  if (!wire_) {
    return false;
  }
  wire_->beginTransmission(address_);
  return wire_->endTransmission() == 0;
}

bool Ds2482_100::writeCommand(uint8_t command) {
  if (!wire_) {
    return false;
  }
  wire_->beginTransmission(address_);
  wire_->write(command);
  return wire_->endTransmission() == 0;
}

bool Ds2482_100::writeCommand(uint8_t command, uint8_t arg) {
  if (!wire_) {
    return false;
  }
  wire_->beginTransmission(address_);
  wire_->write(command);
  wire_->write(arg);
  return wire_->endTransmission() == 0;
}

bool Ds2482_100::readCurrentRegister(uint8_t& value) {
  if (!wire_) {
    return false;
  }
  const int readCount = wire_->requestFrom(static_cast<int>(address_), 1);
  if (readCount != 1 || !wire_->available()) {
    return false;
  }
  value = wire_->read();
  return true;
}

bool Ds2482_100::setReadPointer(uint8_t pointer) {
  return writeCommand(CMD_SET_READ_PTR, pointer);
}

bool Ds2482_100::readRegister(uint8_t pointer, uint8_t& value) {
  if (!setReadPointer(pointer)) {
    return false;
  }
  return readCurrentRegister(value);
}

bool Ds2482_100::waitUntilReady(uint16_t timeoutMs, bool setStatusPointer,
                                uint8_t* status) {
  if (setStatusPointer && !setReadPointer(PTR_STATUS)) {
    return false;
  }

  const uint32_t startedAt = millis();
  uint8_t currentStatus = 0;
  while (millis() - startedAt <= timeoutMs) {
    if (!readCurrentRegister(currentStatus)) {
      return false;
    }
    if ((currentStatus & STATUS_BUSY) == 0) {
      if (status) {
        *status = currentStatus;
      }
      return true;
    }
    delay(1);
  }
  return false;
}

bool Ds2482_100::deviceReset() {
  if (!writeCommand(CMD_RESET)) {
    return false;
  }
  delay(2);
  uint8_t status = 0;
  if (!readRegister(PTR_STATUS, status)) {
    return false;
  }
  return true;
}

bool Ds2482_100::writeConfig(uint8_t config) {
  const uint8_t payload =
      static_cast<uint8_t>((config & 0x0F) | ((~config & 0x0F) << 4));
  if (!writeCommand(CMD_WRITE_CONFIG, payload)) {
    return false;
  }
  uint8_t readBack = 0;
  if (!readRegister(PTR_CONFIG, readBack)) {
    return false;
  }
  return (readBack & 0x0F) == (config & 0x0F);
}

bool Ds2482_100::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  online_ = false;
  resetSearch();

  if (!probeI2c()) {
    return false;
  }
  if (!deviceReset()) {
    return false;
  }
  if (!writeConfig(CFG_APU)) {
    return false;
  }

  online_ = true;
  return true;
}

void Ds2482_100::resetSearch() {
  memset(searchAddress_, 0, sizeof(searchAddress_));
  lastDiscrepancy_ = 0;
  searchExhausted_ = false;
}

bool Ds2482_100::oneWireReset(bool* presence) {
  if (!online_ || !writeCommand(CMD_1WIRE_RESET)) {
    online_ = false;
    return false;
  }

  uint8_t status = 0;
  if (!waitUntilReady(25, true, &status)) {
    online_ = false;
    return false;
  }

  const bool detected = (status & STATUS_PPD) != 0;
  if (presence) {
    *presence = detected;
  }
  return detected;
}

bool Ds2482_100::oneWireWriteByte(uint8_t value) {
  if (!online_ || !writeCommand(CMD_1WIRE_WRITE_BYTE, value)) {
    online_ = false;
    return false;
  }
  if (!waitUntilReady(25, true, nullptr)) {
    online_ = false;
    return false;
  }
  return true;
}

bool Ds2482_100::oneWireReadByte(uint8_t& value) {
  if (!online_ || !writeCommand(CMD_1WIRE_READ_BYTE)) {
    online_ = false;
    return false;
  }
  if (!waitUntilReady(25, true, nullptr)) {
    online_ = false;
    return false;
  }
  if (!readRegister(PTR_DATA, value)) {
    online_ = false;
    return false;
  }
  return true;
}

bool Ds2482_100::oneWireTriplet(bool direction, uint8_t& status) {
  if (!online_ ||
      !writeCommand(CMD_1WIRE_TRIPLET, direction ? 0x80 : 0x00)) {
    online_ = false;
    return false;
  }
  if (!waitUntilReady(25, true, &status)) {
    online_ = false;
    return false;
  }
  return true;
}

bool Ds2482_100::skipRom() { return oneWireWriteByte(ROM_SKIP); }

bool Ds2482_100::matchRom(const uint8_t address[8]) {
  if (!oneWireWriteByte(ROM_MATCH)) {
    return false;
  }
  for (uint8_t i = 0; i < 8; ++i) {
    if (!oneWireWriteByte(address[i])) {
      return false;
    }
  }
  return true;
}

bool Ds2482_100::startConversionAll() {
  bool presence = false;
  if (!oneWireReset(&presence) || !presence) {
    return false;
  }
  return skipRom() && oneWireWriteByte(CMD_CONVERT_T);
}

bool Ds2482_100::startConversionByAddress(const uint8_t address[8]) {
  bool presence = false;
  if (!oneWireReset(&presence) || !presence) {
    return false;
  }
  return matchRom(address) && oneWireWriteByte(CMD_CONVERT_T);
}

bool Ds2482_100::readScratchpad(const uint8_t address[8],
                                uint8_t scratchpad[9]) {
  bool presence = false;
  if (!oneWireReset(&presence) || !presence) {
    return false;
  }
  if (!matchRom(address) || !oneWireWriteByte(CMD_READ_SCRATCHPAD)) {
    return false;
  }
  for (uint8_t i = 0; i < 9; ++i) {
    if (!oneWireReadByte(scratchpad[i])) {
      return false;
    }
  }
  return OneWire::crc8(scratchpad, 8) == scratchpad[8];
}

bool Ds2482_100::readTemperatureC(const uint8_t address[8], float& valueC) {
  if (!isSupportedDs18Family(address[0])) {
    return false;
  }

  uint8_t scratchpad[9] = {0};
  if (!readScratchpad(address, scratchpad)) {
    return false;
  }

  valueC = decodeDs18TemperatureC(address[0], scratchpad);
  return isfinite(valueC);
}

bool Ds2482_100::search(uint8_t newAddress[8]) {
  if (searchExhausted_) {
    return false;
  }

  bool presence = false;
  if (!oneWireReset(&presence) || !presence) {
    resetSearch();
    return false;
  }
  if (!oneWireWriteByte(ROM_SEARCH)) {
    return false;
  }

  uint8_t lastZero = 0;
  for (uint8_t bitIndex = 1; bitIndex <= 64; ++bitIndex) {
    const uint8_t byteIndex = (bitIndex - 1) >> 3;
    const uint8_t bitMask = 1U << ((bitIndex - 1) & 0x07);

    bool direction = false;
    if (bitIndex < lastDiscrepancy_) {
      direction = (searchAddress_[byteIndex] & bitMask) != 0;
    } else {
      direction = bitIndex == lastDiscrepancy_;
    }

    uint8_t status = 0;
    if (!oneWireTriplet(direction, status)) {
      return false;
    }

    const bool idBit = (status & STATUS_SBR) != 0;
    const bool cmpIdBit = (status & STATUS_TSB) != 0;
    direction = (status & STATUS_DIR) != 0;

    if (idBit && cmpIdBit) {
      return false;
    }
    if (!idBit && !cmpIdBit && !direction) {
      lastZero = bitIndex;
    }

    if (direction) {
      searchAddress_[byteIndex] |= bitMask;
    } else {
      searchAddress_[byteIndex] &= static_cast<uint8_t>(~bitMask);
    }
  }

  if (OneWire::crc8(searchAddress_, 7) != searchAddress_[7]) {
    return false;
  }

  memcpy(newAddress, searchAddress_, 8);
  lastDiscrepancy_ = lastZero;
  if (lastZero == 0) {
    searchExhausted_ = true;
  }
  return true;
}
