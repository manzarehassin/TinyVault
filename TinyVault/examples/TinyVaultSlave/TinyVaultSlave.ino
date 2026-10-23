/**
 * @file TinyVaultSlave.ino
 * @author Manzar-E-Hassin (https://github.com/manzarehassin/TinyVault)
 * @brief ATtiny13(A) software-driven SPI slave implementation with cycle-accurate ISR.
 * @version 1.0.0
 * @date 23 Oct, 2026 @ 4:00 PM (UTC+6)
 * 
 * @copyright Copyright (c) 2026 Manzar-E-Hassin
 * @license MIT License (https://github.com/manzarehassin/TinyVault/blob/main/LICENSE)
 * 
 * @details
 * TinyVault provides an ultra-low-overhead SPI slave protocol designed for the 
 * ATtiny13/13A microcontrollers running at 1.2MHz (MicroCore). Features active-HIGH 
 * chip select (CS), SPI Mode 0, MSB first transmission, and complete transaction 
 * handling entirely within the Pin Change Interrupt (PCINT0) service routine.
 * 
 ==========================================
  Pin Mapping (MicroCore / ATtiny13A):
 ==========================================
 *   - PB0 (Pin 5): MOSI
 *   - PB1 (Pin 6): SCK
 *   - PB2 (Pin 7): MISO
 *   - PB4 (Pin 3): CS (Active-HIGH) [Use a 10k pulldown resistor here.]
 *   - PB3 (Pin 2): Dedicated GPIO / ADC3 sample input (Isolated from SPI engine)
 *
 =======================================================
   Arduino IDE Build & Upload Parameters (MicroCore):
 =======================================================
 *   - Board: "ATtiny13"
 *   - Override Clock Source: "Internal Oscillator 1.2 MHz"
 *   - Override Frequency: "1.2 MHz"
 *   - Processor Speed: "1.2MHz Internal Oscillator"
 *   - Brown-out Detection Level: "Disabled"
 *   - Use Bootloader: "No (ISP Programmer Upload)"
 *   - Millis, Tone Support: "No Millis, No Tone"
 *   - Link Time Optimisation: "LTO Enabled"
 *   - Millis Accuracy: "Better Or Equal 50% Error (Lowest Code Use)"
 *   - Print Support: "Bin, Hex, Dec Supported"
 *   - Serial Support: "Half Duplex, Read+Write"
 *   - Processor Version: "ATtiny13a"
 *   - Programmer: "DIY ATtiny: Arduino as ISP" or suitable programmer.
 */
 
#include "TinyVaultSlave.h"
void setup(){TinyVaultSlave::begin();}
void loop() {TinyVaultSlave::sleepIfIdle();} // Te enable sleep.
