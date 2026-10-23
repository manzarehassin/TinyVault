#ifndef TINYVAULT_MASTER_H
#define TINYVAULT_MASTER_H

#include <Arduino.h>

/**
 * @file TinyVault.h
 * @author Manzar-E-Hassin (https://github.com/manzarehassin/TinyVault)
 * @brief TinyVault Master implementation for Arduino-compatible controllers.
 * @version 1.0.0
 * @date 23 Oct, 2026 @ 4:00 PM (UTC+6)
 * 
 * @copyright Copyright (c) 2026 Manzar-E-Hassin
 * @license MIT License (https://github.com/manzarehassin/TinyVault/blob/main/LICENSE)
 * 
 * @details
 * Implements the master-side software SPI protocol for communicating with the 
 * TinyVault ATtiny13A software-driven SPI slave. Features active-HIGH 
 * chip select (CS), idle-LOW SCK, MSB first transmission, and full-duplex software 
 * transfer management with zero heap allocation.
 * 
 ==========================================
  Master-to-Slave Pin Mapping Example (Arduino Uno):
 ==========================================
 *   - Pin 10 (Output): CS (Active-HIGH)
 *   - Pin 11 (Output): MOSI
 *   - Pin 12 (Input):  MISO
 *   - Pin 13 (Output): SCK
 */
 
class TinyVault 
{
  public:
    void init(uint8_t miso, uint8_t mosi, uint8_t sck, uint8_t cs) 
    {
      _miso = miso; _mosi = mosi; _sck = sck; _cs = cs;
      ::digitalWrite(_cs, LOW);
      ::digitalWrite(_sck, LOW);
      ::digitalWrite(_mosi, LOW);
      ::pinMode(_cs, OUTPUT);
      ::pinMode(_sck, OUTPUT);
      ::pinMode(_mosi, OUTPUT);
      ::pinMode(_miso, INPUT);
    }
    
    uint8_t readRAM(uint8_t address) { return address < 32 ? readCommand(0x30, address, false) : 0;}
    void writeRAM(uint8_t address, uint8_t value) { if (address < 32) command(uint8_t(0x80 | address), value, false);}
    uint8_t readROM(uint8_t address) { return address < 64 ? readCommand(0x33, address, true) : 0;}
    void writeROM(uint8_t address, uint8_t value) {if (address < 63) command(uint8_t(0xC0 | address), value, true);}

    void verifyROM(uint8_t &address, uint8_t &result) 
    {
      const uint8_t f = readFlag(1);
      address = uint8_t(f & 0x3F);
      result = uint8_t(f >> 6);
    }

    void writeEnRAM(bool en) { setFlagBit(0x80, en); }
    void writeEnROM(bool en) { setFlagBit(0x40, en); }
    bool getReadOnlyState_RAM() { return !(readFlag(0) & 0x80); }
    bool getReadOnlyState_ROM() { return !(readFlag(0) & 0x40); }
    bool getState_ADC() { return (readFlag(0) & 0x10) != 0; }
    bool getState_Sleep() { return (readFlag(0) & 0x08) != 0; }

    void setPower_ADC(bool en) 
    {
      uint8_t f = readFlag(0);
      if (en) f = uint8_t((f | 0x30) & ~0x08);
      else f = uint8_t(f & ~0x10);
      writeFlag(f);
    }

    void setPower_Sleep(bool en)
    {
      uint8_t f = readFlag(0);
      if (en) f = uint8_t((f | 0x08) & ~0x10);
      else f = uint8_t(f & ~0x08);
      writeFlag(f);
    }
  
    bool verifySignature()
    { 
      uint8_t s =  readFlag(0) & 0x07;
      if(s>0) {return 1;}
      return 0;
    }

    void writeSignature(uint8_t val) 
    {
      uint8_t f = readFlag(0);
      writeFlag(uint8_t((f & 0xF8) | (val & 0x07)));
    }

    void setPower_GPIO(bool en)
    {
      uint8_t f = readFlag(0);
      if (en) f |= 0x20;
      else f = uint8_t(f & ~0x30);
      writeFlag(f);
    }

    bool getState_GPIO() { return (readFlag(0) & 0x20) != 0; }

    bool digitalRead() { return (readFlag(2) & 0x01) != 0; }
    uint8_t analogRead() { return readFlag(2); }
    void writeFlag(uint8_t val) {command(0xA0, val, true); }
    uint8_t readFlag(uint8_t index) 
    {
      return index <= 2 ? readCommand(0x30, uint8_t(0x20 + index), false) : 0;
    }

  private:
    uint8_t _miso, _mosi, _sck, _cs;
    enum { HALF_US=100, SELECT_SETUP_US=1000, ORDINARY_US=1000, ROM_MS=20 };

    uint8_t transfer(uint8_t outgoing)
    {
      uint8_t incoming = 0;
      for (uint8_t i=0; i<8; ++i)
      {
        ::digitalWrite(_mosi, (outgoing & 0x80) ? HIGH : LOW);
        delayMicroseconds(HALF_US);
        ::digitalWrite(_sck, HIGH);
        delayMicroseconds(HALF_US);
        incoming = uint8_t((incoming << 1) | (::digitalRead(_miso) ? 1 : 0));
        ::digitalWrite(_sck, LOW);
        outgoing <<= 1;
      }
      return incoming;
    }

    void start() 
    {
      ::digitalWrite(_sck, LOW);
      ::digitalWrite(_cs, HIGH);
      delayMicroseconds(SELECT_SETUP_US);
    }
  
    void finish(bool ROMBusy)
    {
      delayMicroseconds(HALF_US);
      ::digitalWrite(_cs, LOW);
      delayMicroseconds(HALF_US);
      if (ROMBusy) delay(ROM_MS);
      else delayMicroseconds(ORDINARY_US);
    }

    void command(uint8_t opcode, uint8_t operand, bool ROMBusy)
    {
      start();
      (void)transfer(opcode);
      (void)transfer(operand);
      finish(ROMBusy);
    }

    uint8_t readCommand(uint8_t opcode, uint8_t address, bool ROMBusy)
    {
      command(opcode, address, ROMBusy);
      start();
      const uint8_t value = transfer(0x0A);
      finish(false);
      return value;
    }
    
    void setFlagBit(uint8_t mask, bool en) 
    {
      uint8_t f = readFlag(0);
      f = en ? uint8_t(f | mask) : uint8_t(f & ~mask);
      writeFlag(f);
    }
};
#endif
