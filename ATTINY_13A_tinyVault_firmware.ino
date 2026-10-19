/*
 -* @file: ATtiny13_spiSlave_tinyVault.ino
 * @project TinyVault
 * @brief Ultra-secure, memory-safe, power-optimized SPI slave vault firmware for ATtiny13A.
 * 
 * Features: Bit-banged SPI protocol, persistent RAM/EEPROM storage isolation, 
 *           software write-protect mechanisms,and 8-bit 
 *           ADC multiplexing via flag registers.
 * 
 * @author Manzar-E-Hassin (https://github.com/manzarehassin)
 * @date 19 Oct, 2026 @ 6:42 PM [UTC+6]
 * @target Microchip ATtiny13A (1.2 MHz internal RC oscillator, CKDIV8 disabled)
 * 
 * -----------------------------------------------------------------------------
  
  MIT License

  Copyright (c) 2026 Manzar-E-Hassin

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Source code: https://github.com/manzarehassin/TinyVault

*/


#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#define F_CPU 1200000UL
// ==========================================
// PIN DEFINITIONS & CONSTANTS (Spec v4.5)
// ==========================================
#define PB_MOSI   PB0             // Physical Pin 5: Master Out Slave In pin
#define PB_MISO   PB1             // Physical Pin 6: Master In Slave Out pin
#define PB_SCK    PB2             // Physical Pin 7: Serial Clock pin
#define PB_GPIO   PB3             // Physical Pin 2: Dual-mode GPIO / ADC3 pin
#define PB_CS     PB4             // Physical Pin 3: Chip Select wake-up pin

// ==========================================
// MEMORY & REGISTER MAP STORAGE
// ==========================================
#define RAM_SIZE 32
#define DIGITAL_PIN_READ_ENABLE 1

static volatile uint8_t user_ram[RAM_SIZE];      // RAM storage.
static volatile uint8_t flag0 = 0;      // register for device config.
static volatile uint8_t flag1 = 0;      // register for eeprom storage.
static volatile uint8_t flag2 = 0;      // register for adc storage.

// Deferred EEPROM Write Buffers
static volatile uint8_t pending_eeprom_addr = 0; // Initialize pending EEPROM target address buffer
static volatile uint8_t pending_eeprom_data = 0; // Initialize pending EEPROM payload data buffer

// ==========================================
// FUNCTION PROTOTYPES (Modular Architecture)
// ==========================================
void init_config(void); // initialize setup config
void power_down(void);            // Declare power-down sleep function
void adc_handling(void);          // Declare ADC conversion handling function
void gpio_handling(void);         // Declare dual-mode GPIO configuration function
uint8_t ram_management(uint8_t addr, uint8_t data, uint8_t is_write); // Declare RAM and register management handler
uint8_t eeprom_read(uint8_t addr);// Declare EEPROM byte read routine
void eeprom_write(uint8_t addr, uint8_t val); // Declare EEPROM byte write routine
void eeprom_write_handling(void); // Declare deferred EEPROM write and verification routine
uint8_t spi_transfer_byte(uint8_t out_byte); // Declare bit-banged SPI byte transfer routine
void spi_management(void);        // Declare SPI command decoding and management routine
void metadataRead(void);
void metadataWrite(void);

ISR(PCINT0_vect) // Define Pin Change Interrupt Service Routine for port B
{                
  if (!(PINB & (1 << PB_CS))) // Check if Chip Select line has gone LOW
  { 
    GIMSK &= ~(1 << PCIE);
    spi_management();
    GIMSK |= (1 << PCIE);
  }
}

void init_config(void)
{
  // Disable ADC by default for power conservation.
  ADCSRA &= ~(1 << ADEN);       // Turn off ADC peripheral to minimize standby current consumption
  // Configure Pin Directions: MISO as output, others as inputs
  DDRB = (1 << PB_MISO);        // Set data direction register to make MISO an output pin
  PORTB = (1 << PB_CS);         // Enable internal pull-up resistor on CS pin (PB4)
  // Enable Pin Change Interrupt on PB4 (`CS`) for hardware wake-up.
  GIMSK |= (1 << PCIE);         // Enable Pin Change Interrupt extension in general interrupt mask
  PCMSK |= (1 << PCINT4);       // Unmask pin change interrupt specifically for PB4 (CS)
  // Enable Global Interrupts
  sei();                        // Set global interrupt enable flag
  metadataRead();
}

void power_down(void)
{
  // Set Power-Down sleep mode (SM1 = 1, SM0 = 0) and enable sleep (SE = 1)
  MCUCR = (1 << SM1) | (1 << SE); // Configure MCU control register for power-down mode and sleep enable
  // Execute assembly sleep instruction
  __asm__ __volatile__("sleep" ::: "memory"); // Inline assembly instruction to enter sleep state
  // Clear sleep enable flag upon wake-up
  MCUCR &= ~(1 << SE);          // Clear sleep enable bit in MCUCR after waking up
}

void adc_handling(void) 
{
  // Active only when ADC is ON (FLAG0.bit4 = 1) and no EEPROM write is pending
  if ((flag0 & (1 << 4)) && (!(flag0 & (1 << 0))))  // Check if ADC control bit is set and no write is pending
  {
    // Select ADC3 channel (PB3) via MUX[1:0] = 11
    ADMUX = (1 << MUX1) | (1 << MUX0); // Configure multiplexer to select analog channel PB3
    // Enable ADC and start conversion
    ADCSRA = (1 << ADEN) | (1 << ADSC); // Enable ADC peripheral and trigger a single conversion
    while (ADCSRA & (1 << ADSC)); // Wait until conversion complete flag clears
    uint16_t buffer = ADC;
    flag2 = (buffer + 2) >> 2; // convert 10 bit to 8 bit
  } 
  else if (!(flag0 & (1 << 4)))  // Check if ADC control bit is explicitly cleared.
  {
    // Ensure ADC is fully disabled for power saving when FLAG0.bit4 = 0.
    ADCSRA &= ~(1 << ADEN);   // Disable ADC peripheral to conserve battery power
    if(DIGITAL_PIN_READ_ENABLE)
    {
      flag2=(PINB & (1 << PB_GPIO)) ? 0x01 : 0x00; // get digital read value.
    }
    else
    {
      flag2=0;
    }
  }
}

void gpio_handling(void)
{
  if (!(flag0 & (1 << 4)))     // Check if ADC is turned OFF.
  {
    // ADC OFF: PB3 functions as a floating digital input.
    DDRB &= ~(1 << PB_GPIO);  // Clear data direction bit to configure PB3 as input
  } 
  else // Otherwise, when ADC is active
  {
    // ADC ON: PB3 functions as an analog input channel.
    DDRB &= ~(1 << PB_GPIO);  // Ensure PB3 remains configured as input for analog sampling
  }
}

uint8_t ram_management(uint8_t addr, uint8_t data, uint8_t is_write) // Define RAM and register accessor routine
{ 
  if (addr < RAM_SIZE) 
  {
    if (is_write) 
    {
      // Check RAM Read-Only protection state (FLAG0.bit5).
      if (!(flag0 & (1 << 5))) // Verify RAM read-only protection bit is cleared
      { 
        user_ram[addr] = data; // Write data payload into target RAM address
      }
    }                         // End of write condition
  return user_ram[addr];    // Return current value stored in user RAM (Read)
  }

  else if (addr == RAM_SIZE) // FLAG0 register (Address 32).
  {
    if (is_write) 
    {
      // Allow writing to control bits (Bits 6–1), protect internal status bits
      flag0 = (flag0 & 0x81) | (data & 0x7E); // Update writable flag bits while preserving protected bits
      metadataWrite(); // write to eeprom.

    }
  return flag0;             // Return current FLAG0 value (READ)
  }

  else if (addr == RAM_SIZE+1) {return flag1;}  // FLAG1 register (Address 33).
  else if (addr == RAM_SIZE+2) {return flag2;}  // FLAG2 register (Address 34).
  return 0; // Return default zero byte for invalid addresses
}

void metadataWrite(void)
{
  uint8_t buf1 = (flag0 >> 5) & 0x03;
  uint8_t buf2 = eeprom_read(63);
  if (buf2 != buf1)
  {
    eeprom_write(63, buf1);
  }
}

void metadataRead(void)
{
  uint8_t buf = eeprom_read(63);
  flag0 = (flag0 & ~0x60) | ((buf << 5) & 0x60);
}

uint8_t eeprom_read(uint8_t addr) 
{
  if (addr > 63) return 0;      // Return maximum byte if address is out of bounds
  while (EECR & (1 << EEPE));   // Wait until any ongoing EEPROM write cycle completes
  EEAR = addr;                  // Load target EEPROM address into address register
  EECR |= (1 << EERE);          // Set EEPROM Read Enable control bit
  return EEDR;                  // Return data byte read from EEPROM data register
}

void eeprom_write(uint8_t addr, uint8_t val)
{
  while (EECR & (1 << EEPE)); // Wait for EEPROM controller to become idle
  EEAR = addr;                // Load destination address into EEAR
  EEDR = val;                 // Load data payload into EEDR
  // Atomic EEPROM write sequence
  EECR |= (1 << EEMPE);       // Set Master Write Enable control bit
  EECR |= (1 << EEPE);        // Set EEPROM Write Enable control bit to start programming       
  // Hardware EEPE Polling Guard (ensures charge pump finishes)
  while (EECR & (1 << EEPE)); // Block execution until hardware completes write cycle
}

void eeprom_write_handling(void) 
{
  // Executed post-CS HIGH when PENDING flag is set (FLAG0.bit0 == 1).
  if (flag0 & (1 << 0))        // Check if EEPROM pending write flag is set
  {
    uint8_t addr = pending_eeprom_addr; // Retrieve staged target address from buffer
    uint8_t payload = pending_eeprom_data; // Retrieve staged payload data from buffer  
    if (addr <= 62)          // Verify target address is within user ROM bounds (0-62).
    {
      // Pre-Write Internal Read to prevent unnecessary wear.
      uint8_t buffer_val = eeprom_read(addr); // Read current EEPROM value at target location
      flag1 = addr; // update address to flag1.
      if (buffer_val == payload)  // Check if existing value matches payload exactly
      {
        flag1 |= (1 << 7);  // Set write skip status flag in FLAG1.
        flag1 &= ~(1 << 6); // Clear verification success flag
      }
      else // Otherwise, proceed with physical write
      {              
        flag1 &= ~(1 << 7); // Clear write skip status flag
        eeprom_write(addr, payload); // rom write
        // Post-Write Verification Read.
        buffer_val = eeprom_read(addr); // Read back programmed address for verification
        if (buffer_val == payload) {flag1 |= (1 << 6);}  // Set verification success bit in FLAG1.
        else {flag1 &= ~(1 << 6); }     // Otherwise, verification failed, Clear verification success bit.
      }
    }
    flag0 &= ~(1 << 0);  // Clear EEPROM pending write flag in FLAG0  
  }
}

uint8_t spi_transfer_byte(uint8_t out_byte) 
{
  uint8_t in_byte = 0;
  for (uint8_t i = 0; i < 8; i++)
  { 
    // Shift out on MISO (PB1).
    if (out_byte & 0x80)        // Check if highest bit of output byte is set
    {
      PORTB |= (1 << PB_MISO);  // Drive MISO pin HIGH
    }
    else // Otherwise, drive MISO LOW
    {
      PORTB &= ~(1 << PB_MISO); // Drive MISO pin LOW
    }
    out_byte <<= 1;             // Shift output byte left to prepare next bit
    
    // Wait for SCK (PB2) rising edge.
    while (!(PINB & (1 << PB_SCK)))      // Wait until serial clock goes HIGH
    {
      if (PINB & (1 << PB_CS)) return 0; // Abort transfer if chip select goes HIGH prematurely
    }
    
    // Sample MOSI (PB0).
    in_byte <<= 1;              // Shift incoming data byte left to make room for new bit
    if (PINB & (1 << PB_MOSI))  // Check if master data input line is HIGH
    {
      in_byte |= 0x01;          // Set lowest bit of incoming byte
    }

    // Wait for SCK (PB2) falling edge.
    while (PINB & (1 << PB_SCK))         // Wait until serial clock goes LOW
    {
      if (PINB & (1 << PB_CS)) return 0; // Abort transfer if chip select goes HIGH prematurely
    }
  }
  return in_byte;    // Return fully assembled incoming byte
}

void spi_management(void)       // Define core SPI transaction and command handler
{
  // Check if system is busy; if so, reject with 0 error code.
  if (flag0 & (1 << 7))        // Check if busy status bit is set in FLAG0
  {
    while (!(PINB & (1 << PB_CS)))  // Loop while chip select remains active LOW
    {
      spi_transfer_byte(0x80); // Continuously transmit standard busy error code 0x80.
    }
  return;// EXIT
  }

  // Receive command header byte: [2-bit opcode][6-bit address/modifier].
  uint8_t cmd = spi_transfer_byte(0x00); // Read incoming command header byte from master
  uint8_t opcode = (cmd >> 6) & 0x03;    // Extract upper 2 bits as operation opcode
  uint8_t addr = cmd & 0x3F;         // Extract lower 6 bits as target address or modifier
  if (opcode == 0x00)                    // Check if opcode specifies RAM/Register Read (00)
  {
    uint8_t val = ram_management(addr, 0, 0); // Read target RAM or register value
    spi_transfer_byte(val);
  }
  else if (opcode == 0x01)     // Check if opcode specifies RAM/Register Write (01)
  {
    // RAM & Register Write.
    uint8_t payload = spi_transfer_byte(0x00);
    if (addr != RAM_SIZE+1 && addr != RAM_SIZE+2)  // Block direct writes to FLAG1 and FLAG2 registers.
    {
      ram_management(addr, payload, 1);        // Execute RAM or register write operation
    }
  }

  else if (opcode == 0x02 || opcode == 0x03)  // Check if opcode specifies ROM operations (10 or 11)
  {
    if (opcode == 0x02)      // Check if opcode is ROM Read (10)
    {
      // ROM Read.
      uint8_t rom_val = eeprom_read(addr);
      spi_transfer_byte(rom_val);
    }
    else // Otherwise, opcode is ROM Write (11)
    {
      // ROM Write (Deferred until CS goes HIGH).
      uint8_t rom_payload = spi_transfer_byte(0x00);
      if (addr <= 62 && !(flag0 & (1 << 6)))  // Verify address bounds and ROM read-only protection bit.
      {
        pending_eeprom_addr = addr; // Buffer target EEPROM address internally
        pending_eeprom_data = rom_payload; // Buffer payload byte internally
        flag0 |= (1 << 0);     // Set EEPROM pending write flag in FLAG0.
      }
    }
  }

  // Wait for framing boundary (CS goes HIGH).
  while (!(PINB & (1 << PB_CS))); // Block until master terminates transaction by pulling CS HIGH
  
  // Auto-Sleep Governor: Execute deferred write post-transaction if pending
  if (flag0 & (1 << 0))         // Check if an EEPROM write is pending completion
  {
    flag0 |= (1 << 7);        // Set busy status bit in FLAG0 during background write
    eeprom_write_handling();  // Execute deferred physical EEPROM write routine
    flag0 &= ~(1 << 7);       // Clear busy status bit after write completes
  }
}

int main(void) 
{
  init_config(); // Setup()
  while (1)      // Loop()
  {                   // Infinite runtime loop for background operations
    gpio_handling();          // Continuously evaluate dual-mode pin configuration
    adc_handling();           // Continuously manage active ADC sampling when enabled
    // Enter deep power-down sleep if no jobs are pending and ADC is OFF.
    if (!(flag0 & (1 << 0)) && !(flag0 & (1 << 4)))  // Check that no writes are pending and ADC is disabled
    {
      power_down();
    }
  }
  
  return 0; // Return standard exit code (unreachable in bare-metal embedded context)
}