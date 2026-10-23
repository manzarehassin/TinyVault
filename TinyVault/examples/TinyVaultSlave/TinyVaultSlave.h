/**
 * @file TinyVaultSlave.h
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

#ifndef TINYVAULT_SLAVE_H
#define TINYVAULT_SLAVE_H
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#if !defined(__AVR_ATtiny13A__) && !defined(__AVR_ATtiny13__)
#error "TinyVaultSlave requires ATtiny13 or ATtiny13A"
#endif
#ifndef F_CPU
#error "Select 1.2MHz clock in MicroCore"
#endif
#if F_CPU != 1200000UL
#error "TinyVault validation profile requires 1.2MHz CPU"
#endif

/* RAM cost: 32 retained bytes + 2 flags + 2 RX + TX + state = 38 bytes.
 Stack is separate but must fit the REMAINING 26 bytes: measure the linked
 image and worst-case ISR stack before claiming target compatibility. */
volatile uint8_t tv_rx[2] __asm__("tv_rx");
volatile uint8_t tv_tx __asm__("tv_tx");
volatile uint8_t tv_flag0 __asm__("tv_flag0");
volatile uint8_t tv_flag1 __asm__("tv_flag1");
volatile uint8_t tv_state __asm__("tv_state");
volatile uint8_t tv_ram[32] __attribute__((section(".noinit")));

class TinyVaultSlave 
{
  enum { ARMED=1, PENDING=2, BADFRAME=4, BUSY_EDGE=8 };
  static uint8_t eeRead(uint8_t addr) 
  {
    while (EECR & _BV(EEPE)) {}
    EEARL = addr;
    EECR |= _BV(EERE);
    return EEDR;
  }

  /* Return: 0=write failed, 1=written and verified, 2=already matched. */
  static uint8_t eeWrite(uint8_t addr, uint8_t value) 
  {
    if (eeRead(addr) == value) return 2;
    EECR = 0;                   // atomic erase-and-write mode
    EEARL = addr;
    EEDR = value;
    // We are already inside a non-nesting ISR when serving SPI requests.
    // Both EEMPE and EEPE MUST be emitted consecutively within four cycles.
    asm volatile("sbi 0x1c,2\n\tsbi 0x1c,1\n\t" ::: "memory");
    while (EECR & _BV(EEPE)) {} // Do not let SPI borrow EEARL/EEDR yet
    return (eeRead(addr) == value) ? 1 : 0;
  }

  static uint8_t normalizeFlag0(uint8_t f) {if ((f & 0x28) != 0x20) f &= (uint8_t)~0x10;return f;}
  static uint8_t metadata(uint8_t f) {return (uint8_t)(0xb0 | ((f >> 3) & 1) | ((f >> 6) & 2) | ((f >> 4) & 4));}
  
  static uint8_t gpioSample()
  {
    // PB3/pin2 is accessed here ONLY, never from the SPI clock engine.
    if (!(tv_flag0 & 0x20)) return 0;
    DDRB &= (uint8_t)~_BV(PB3);
    PORTB &= (uint8_t)~_BV(PB3);
    if (!(tv_flag0 & 0x10)) return (PINB & _BV(PB3)) ? 1 : 0;
    PRR &= (uint8_t)~_BV(PRADC);
    DIDR0 |= _BV(ADC3D);
    ADMUX = _BV(ADLAR) | _BV(MUX1) | _BV(MUX0); // ADC3, VCC ref
    ADCSRA = _BV(ADEN) | _BV(ADPS1) | _BV(ADPS0); // ADC clk = F_CPU/8
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC)) {}
    uint8_t sample = ADCH;     // top eight bits of left-adjusted 10-bit ADC
    ADCSRA = 0;
    DIDR0 &= (uint8_t)~_BV(ADC3D);
    PRR |= _BV(PRADC);
    return sample;
  }

public:

  // ISR-only command execution; called after CS LOW, never from loop().
  static __attribute__((always_inline)) inline void dispatch(uint8_t first, uint8_t second) 
  {
    uint8_t value;
    if (first == 0x30 && second <= 0x23)
    {
      if (second < 32) value = tv_ram[second];        // Read ram
      else if (second == 0x20) value = tv_flag0;      // flag0 read
      else if (second == 0x21) value = tv_flag1;      // flag1 read
	  else if (second == 0x22) value = gpioSample();  // flag2 read
      else value = diagnostics();                     // spi error bits read
      tv_tx = value;
      tv_state |= PENDING;
    }
    else if (first == 0x33 && second < 64)
    {
      value = eeRead(second);
      tv_tx = value;
      tv_state |= PENDING;
    }
    else if (first >= 0x80 && first <= 0x9f)
    {
      if (tv_flag0 & 0x80) tv_ram[first & 31] = second;
    }
    else if (first == 0xa0)
    {
      value = normalizeFlag0(second);
      // Never write unnecessary metadata. Do not acknowledge effective FLAG0
      // change if nonvolatile metadata persistence could not be verified.
      if (eeWrite(63, metadata(value))) tv_flag0 = value;
    }
    else if (first >= 0xc0 && first <= 0xfe) 
    {
      uint8_t result = 0;
      if (tv_flag0 & 0x40) result = eeWrite(first & 63, second);
      tv_flag1 = (uint8_t)(first & 63); // last requested EEPROM WRITE
      if (result == 2) tv_flag1 |= 0xc0;     // identical, read verified
      else if (result == 1) tv_flag1 |= 0x40; // written, verified
      else if (!(tv_flag0 & 0x40)) tv_flag1 |= 0x80; // blocked, not verified
    }
    // Unknown opcode/address is a no-op. No out-of-bounds addressing.
  }

public:

  static void begin()
  {
    cli();
    GIMSK &= (uint8_t)~_BV(PCIE);
    asm volatile("wdr" ::: "memory");
    MCUSR &= (uint8_t)~_BV(WDRF);
    WDTCR |= _BV(WDCE) | _BV(WDE);
    WDTCR = 0;
    DDRB &= (uint8_t)~(_BV(PB0)|_BV(PB1)|_BV(PB2)|_BV(PB4));
    PORTB &= (uint8_t)~(_BV(PB0)|_BV(PB1)|_BV(PB2)|_BV(PB4));
    PRR |= _BV(PRADC) | _BV(PRTIM0);
    ACSR |= _BV(ACD);
    ADCSRA = 0;
    tv_flag0 = 0x88;  // write SRAM + allow sleep, write EEPROM disabled
    tv_flag1 = 0x3f;  // no EEPROM write request yet
    tv_tx = 0xaa;
    uint8_t m = eeRead(63);
    if ((m & 0xf8) == 0xb0) { tv_flag0 = (uint8_t)((((m >> 1)&1)<<7) | (((m >> 2)&1)<<6) | ((m&1)<<3));}
    
    EEDR = 0; EEARL = 0; // SPI owns scratch AFTER boot EEPROM read
    tv_state = (PINB & _BV(PB4)) ? 0 : ARMED;
    PCMSK = _BV(PCINT4); // PB3 is NOT a pin-change SPI input
    GIFR = _BV(PCIF);
    GIMSK |= _BV(PCIE);
    sei();
  }

  // SPI diagnostics, independent of the user's loop. Bits 2=bad frame,
  // 3=CS observed high at end of busy processing (ignored selection).
  static uint8_t diagnostics() { return  tv_state & (BADFRAME | BUSY_EDGE); }
  static void clearDiagnostics() { uint8_t s=SREG; cli(); tv_state &= (uint8_t)~(BADFRAME|BUSY_EDGE); SREG=s; }
  // OPTIONAL power-management hook, NOT required for SPI timing or processing.
  // Call only when user application has no work. Busy user loop may stay awake.
  static void sleepIfIdle() 
  {
    uint8_t s=SREG;
    cli();
    if ((tv_flag0 & 0x08) && !(PINB & _BV(PB4))) 
    {
      MCUCR = (uint8_t)((MCUCR & (uint8_t)~_BV(SM0)) | _BV(SM1) | _BV(SE));
      asm volatile("sei\n\tsleep" ::: "memory");
      MCUCR &= (uint8_t)~_BV(SE);
    }
    SREG = s;
  }

  static uint8_t flag0() { return tv_flag0; }
  static uint8_t flag1() { return tv_flag1; }
  static uint8_t readRetained(uint8_t address) { return address < 32 ? tv_ram[address] : 0; }
}; // end of class 

/* Pin-change ISR: ISR is the ENTIRE SPI transaction and command executor.
 Automatic compiler ISR prologue saves clobbered CPU regs; no foreground
 SPI polling, command dispatch, or byte shifts. All clock hot-path work is
 assembly; r16-r20 and EEPROM scratch regs, not C/local SRAM variables. */
ISR(PCINT0_vect) 
{
  if (!(PINB & _BV(PB4)))  // CS fell while outside capture
  {
    tv_state |= 1; return;
  }
  if (!(tv_state & 1)) return; // high pulse while busy: never retroactively start
  tv_state &= (uint8_t)~1;
  // Before MISO drives, verify clock has Mode-0 idle polarity.
  asm volatile(
    "clr  r16                       \n\t"
    "clr  r17                       \n\t"
    "clr  r19                       \n\t" // 0..16 completed bit cycles
    "out  0x1e,r19                  \n\t" // EEARL checkpoint
    "out  0x1d,r19                  \n\t" // EEDR status scratch
    "sbic 0x16,1                    \n\t" // require SCK LOW at CS assertion
    "rjmp .Ltv_early_%=             \n\t"
    "lds  r18,tv_tx                 \n\t"
    "cbi  0x18,2                    \n\t"
    "sbrc r18,7                     \n\t"
    "sbi  0x18,2                    \n\t"
    "sbi  0x17,2                    \n\t" // PB2 drives only while CS=HIGH
    ".Ltv_rise_%=:                 \n\t"
    "in   r20,0x16                  \n\t"
    "sbrs r20,4                     \n\t"
    "rjmp .Ltv_end_%=               \n\t"
    "sbrs r20,1                     \n\t"
    "rjmp .Ltv_rise_%=              \n\t"
    "lsl  r17                       \n\t"
    "rol  r16                       \n\t"
    "sbrc r20,0                     \n\t"
    "ori  r17,1                     \n\t"
    // EEARL bit5 marks a STARTED but not COMPLETED SCK cycle. If CS falls
    // before the matching falling edge, even a zero-complete-bit frame fails.
    "in   r20,0x1e                  \n\t"
    "ori  r20,0x20                  \n\t"
    "out  0x1e,r20                  \n\t"
    ".Ltv_fall_%=:                 \n\t"
    "in   r20,0x16                  \n\t"
    "sbrs r20,4                     \n\t"
    "rjmp .Ltv_end_%=               \n\t"
    "sbrc r20,1                     \n\t"
    "rjmp .Ltv_fall_%=              \n\t"
    "inc  r19                       \n\t"
    "out  0x1e,r19                  \n\t" // six-bit SPI bit progress
    "lsl  r18                       \n\t"
    "cpi  r19,8                     \n\t"
    "brne .Ltv_txnext_%=            \n\t"
    "ldi  r18,0xaa                  \n\t" // second byte ACK = AA
    ".Ltv_txnext_%=:               \n\t"
    "cbi  0x18,2                    \n\t"
    "sbrc r18,7                     \n\t"
    "sbi  0x18,2                    \n\t"
    "cpi  r19,16                    \n\t"
    "brne .Ltv_rise_%=              \n\t"
    ".Ltv_full_%=:                 \n\t"
    "in   r20,0x16                  \n\t"
    "sbrs r20,4                     \n\t"
    "rjmp .Ltv_end_%=               \n\t"
    "sbrs r20,1                     \n\t"
    "rjmp .Ltv_full_%=              \n\t"
    "ldi  r20,0x30                  \n\t" // mark bit5 malformed + count16
    "out  0x1e,r20                  \n\t"
    "ldi  r20,0x80                  \n\t" // >16 bits: reject entire frame
    "out  0x1d,r20                  \n\t"
    ".Ltv_drop_%=:                 \n\t"
    "sbic 0x16,4                    \n\t"
    "rjmp .Ltv_drop_%=              \n\t"
    "rjmp .Ltv_end_%=               \n\t"
    ".Ltv_early_%=:                \n\t"
    "ldi  r20,0x20                  \n\t" // EEARL bit5 marks invalid start
    "out  0x1e,r20                  \n\t"
    "ldi  r20,0x40                  \n\t" // SCK not idle at selection
    "out  0x1d,r20                  \n\t"
    ".Ltv_earlywait_%=:            \n\t"
    "sbic 0x16,4                    \n\t"
    "rjmp .Ltv_earlywait_%=         \n\t"
    ".Ltv_end_%=:                  \n\t"
    "cbi  0x17,2                    \n\t" // MISO high impedance on CS low
    "cbi  0x18,2                    \n\t"
    "sts  tv_rx,r16                 \n\t"
    "sts  tv_rx+1,r17               \n\t"
    ::: "r16","r17","r18","r19","r20","memory","cc"
  );
  uint8_t n=EEARL; // EEARL owned by SPI unless dispatch borrows it
  uint8_t err=EEDR;
  if ((err & 0xc0) || (n && n != 8 && n != 16)) 
  {
    tv_state = (uint8_t)((tv_state & (uint8_t)~2) | 4);
    tv_tx = 0xaa;
  }
  else if (n == 8)
  {
    // A FETCH has 8 clocks; response was sent as MOSI sent opcode 0x0A.
    // A short/stray read cannot silently consume an earlier result.
    if (tv_rx[1] == 0x0a && (tv_state & 2))
    {
      tv_state &= (uint8_t)~2;
    }
    else 
    {
      tv_state = (uint8_t)((tv_state & (uint8_t)~2) | 4);
    }
    tv_tx = 0xaa;
  }
  else if (n == 16)
  {
    if (tv_state & 2) 
    {
      // Master skipped FETCH; discard the pending response, do not execute.
      tv_state = (uint8_t)((tv_state & (uint8_t)~2) | 4);
    }
    else
    {
      TinyVaultSlave::dispatch(tv_rx[0],tv_rx[1]);
    }
    if (!(tv_state & 2)) tv_tx = 0xaa;
  }
  // n=0 and no error: zero-clock CS pulse does NOT consume pending reply.
  // A high CS present here may have started while EEPROM was busy. Ignore it
  // until a LOW pin-change event rearms us. No replay of a missed frame.
  if (PINB & _BV(PB4))
  {
    tv_state |= 8;
    tv_state &= (uint8_t)~1;
  }
  else
  {
    tv_state |= 1;
  }
} // end of isr

#endif
