/**
 * @file TinyVaultMasterExample.ino
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
 *   - Pin 10 (Output): CS (Active-HIGH), make sure, there is a 10k pulldown resistor attached in CS line. 
 *   - Pin 11 (Output): MOSI
 *   - Pin 12 (Input):  MISO
 *   - Pin 13 (Output): SCK
 */

#include <TinyVault.h>

TinyVault tv;

void setup()
{
  Serial.begin(9600);
  tv.init(12, 11, 13, 10);
  Task();
}

void loop() {  }

void Task_RAM_Read()
{
  Serial.println("RAM READ ==================================");
  for(uint8_t i=0; i<32;i++)
  {
    uint8_t val = tv.readRAM(i);
    Serial.print("RAM Read: ");
    Serial.print(i);
    Serial.print(" : ");
    Serial.println(val);
  }
}

void Task_ROM_Read()
{
  Serial.println("ROM READ ==================================");
  for(uint8_t i=0; i<64;i++)
  {
    uint8_t val = tv.readROM(i);
    Serial.print("ROM Read: ");
    Serial.print(i);
    Serial.print(" : ");
    Serial.println(val);
  }
}

void Task_RAM_Write()
{
  Serial.println("RAM WRITE ==================================");
  for(uint8_t i=0; i<32;i++)
  {
    uint8_t val = i+100;
    tv.writeRAM(i, val);
    Serial.print("RAM Write: ");
    Serial.print(i);
    Serial.print(" : ");
    Serial.println(val);
  }
}

void Task_ROM_Write()
{
  Serial.println("ROM WRITE ==================================");
  for(uint8_t i=0; i<63;i++)
  {
    uint8_t val = i+100;
    tv.writeROM(i, val);
    Serial.print("ROM Write: ");
    Serial.print(i);
    Serial.print(" : ");
    Serial.print(val);
    Serial.print(" >> ");
    uint8_t a=0;
    val=0;
    tv.verifyROM(a, val);
    Serial.print(a);
    Serial.print(" - ");
    switch(val)
    {
      case 3: Serial.println("(3) Skipped."); break;
      case 2: Serial.println("(2) Blocked."); break;
      case 1: Serial.println("(1) Written."); break; 
    }
  }
}

void Task_WR_EN()
{
  Serial.print("ROM Read Only status: ");
  uint8_t w = tv.getReadOnlyState_ROM();
  Serial.println(w);
  if(w)
  {
    Serial.print("ROM Read Only status now: ");
    tv.writeEnROM(1);
    w = tv.getReadOnlyState_ROM();
    Serial.println(w);
  }
  Serial.print("RAM Read Only status: ");
  w = tv.getReadOnlyState_RAM();
  Serial.println(w);
}

void Task_signature()
{
  uint8_t x = tv.verifySignature();
  if(x){Serial.println("Signature varification Status: Verified.");}
  if(!x)
  {
    Serial.println("Signature varification Status: Invalid.");
    tv.writeSignature(0b00000101);
    Serial.println("Signature write: 111");
    delay(1000);
    x = tv.verifySignature();
    Serial.print("Signature varification Status now: ");
    Serial.println(x);
  }
}

void Task_ADC()
{
  Serial.print("Reading FLAG0 1: ");
  uint8_t x=tv.readFlag(0);
  Serial.println(x,BIN);

  x= tv.analogRead();
  Serial.print("Analog Read 1: ");
  Serial.println(x);

  tv.setPower_GPIO(1);
  tv.setPower_ADC(1);

  Serial.print("Reading FLAG0 2: ");
  x=tv.readFlag(0);
  Serial.println(x,BIN);
  x= tv.analogRead();
  Serial.print("Analog Read 2: ");
  Serial.println(x);
}
void Task()
{
  delay(2000);
  Task_RAM_Read();
  Task_RAM_Write();

  Task_ROM_Read();
  Task_ROM_Write();

  Task_WR_EN();
  Task_ROM_Write();
  Task_ROM_Read();

  Task_signature();
  Task_ADC();
}
