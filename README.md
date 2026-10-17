# TinyVault

**TinyVault** is an ultra-secure, memory-safe, power-optimized SPI slave vault firmware built specifically for the **Microchip ATtiny13A** microcontroller. It turns the ATtiny13A into an encrypted/protected peripheral offering software-controlled read/write memory blocks (SRAM & EEPROM), hardware/software Write Protect (WP) mechanisms, multi-mode GPIO sampling (Debounce, Toggle, ADC), signal validation handshaking, and deep power management enabling up to 10+ years of operation on a single CR2032 coin cell.

---

## Technical Specifications

| Parameter | Specification |
| :--- | :--- |
| **Microcontroller** | ATtiny13A (AVR 8-bit) |
| **Clock Speed** | 9.6 MHz (Internal RC Oscillator, `F_CPU = 9600000UL`, CKDIV8 disabled) |
| **Flash Memory** | 1,024 Bytes (1 KB) |
| **SRAM Memory** | 64 Bytes total (34 Bytes User RAM + 4 Bytes Control Registers) |
| **EEPROM Memory** | 64 Bytes |
| **Operating Voltage** | 1.8V to 5.5V |
| **Power Consumption** | ~0.2 µA – 2.0 µA (Deep Sleep) / ~4 µA – 6 µA (Debounce WDT Active) |
| **Communication Protocol**| Bit-Banged SPI Slave Mode 0 (CPOL=0, CPHA=0) |
| **Safe Master SCK Rate** | $\le 250 \text{ kHz}$ to $500 \text{ kHz}$ recommended (due to software-polled bit-banging overhead at 9.6 MHz core speed) |
| **Package** | 8-pin PDIP / SOIC-8 |

---

## Pinout Mapping

```
                 +---\/---+
  (PCINT5/RESET) PB5 | 1  8 | VCC
   (P_WP)        PB4 | 2  7 | PB2 (P_SCK)
   (P_CS)        PB3 | 3  6 | PB1 (P_MISO)
                 GND | 4  5 | PB0 (P_MOSI)
                 +--------+
```

| Pin Name | Port Pin | Hardware Function | SPI / Peripheral Role |
| :--- | :--- | :--- | :--- |
| `P_MOSI` | `PB0` | Pin 5 | Master Out Slave In |
| `P_MISO` | `PB1` | Pin 6 | Master In Slave Out (Tri-stated when `P_CS` is HIGH) |
| `P_SCK`  | `PB2` | Pin 7 | SPI Serial Clock (Polled synchronously by software) |
| `P_CS`   | `PB3` | Pin 8 | SPI Chip Select (Active LOW, PCINT3 Interrupt Source) |
| `P_WP`   | `PB4` | Pin 2 | Hardware Write-Protect Input / Digital Sensor / ADC Input |

---

## SPI Timing & Safe Master Clock Rate

Because TinyVault implements **software bit-banging** for SPI slave communication instead of utilizing hardware SPI peripherals (the ATtiny13A lacks a dedicated hardware SPI module), the microcontroller must execute polling loops to detect rising and falling edges of the SCK clock line. 

* **Operating Frequency Constraint:** With the core running at $9.6 \text{ MHz}$, each bit shift requires multiple CPU cycles to sample MOSI, transition MISO, and check for a Chip Select (`P_CS`) abort condition.
* **Safe Master SCK Limit:** To prevent bit-misalignment, dropped clocks, or timing drift, the SPI Master should configure its clock rate to **$\le 250 \text{ kHz}$ to $500 \text{ kHz}$** (ensuring a minimum clock half-period of $\ge 2-4 \, \mu\text{s}$). Exceeding this frequency will cause communication desynchronization.

---

## Features

1. **Memory Isolation & Safety**:
   - 34 Bytes of volatile SRAM (`0x00` – `0x21`).
   - 64 Bytes of non-volatile EEPROM (`0x00` – `0x3F`).
   - Strict out-of-bounds bounds checking; invalid accesses yield `0x00` on reads and drop writes safely.
2. **Hardware & Software Protection**:
   - **Hardware Write-Protect (`P_WP` / `PB4`)**: Active-low pin state blocks EEPROM modification when active.
   - **Software Read-Only Flags**: Independent write-inhibit switches for EEPROM (`F_ROM_RO`) and SRAM (`F_RAM_RO`).
3. **SPI Command Architecture**:
   - 8-bit command payload encoding Read/Write, Target Memory Space, and 6-bit Target Address.
   - Optional 5-Byte Page Burst Reads enabled via control bit `F_PAGE_RD`.
   - Automatic early CS-abort safety to clear line lockups.
4. **GPIO Signal Conditioning, Signature Bits & ADC**:
   - **Debouncing**: Watchdog Timer (WDT) background filtering requiring 5 consecutive stable ticks (~16 ms interval).
   - **Toggle Mode**: Latching edge detection on `P_WP` state transitions.
   - **Signature & Signal Handshaking (`FLAG1`)**: Built-in signal configuration pattern bits (`0x38`) that automatically assert or clear the signal valid indicator status (`F_SIG_VAL`).
   - **Analog-to-Digital Converter**: 10-bit ADC channel reading `PB4`, splitting output across control register `FLAG1` (upper 2 bits) and `FLAG2` (lower 8 bits during active sampling).
5. **Ultra-Low Power Optimization**:
   - Default boot into `SLEEP_MODE_PWR_DOWN`.
   - Power Reduction Register (`PRR`) disables Timer0; Analog Comparator (`ACSR`) and Brown-Out Detector (`MCUCR`) disabled during sleep.
   - Estimated battery lifetime > 10 years on a 220 mAh CR2032 cell in default sleep configuration.

---

## Protocol & Memory Map

All SPI frames begin with an **8-Bit Command Byte**:

```
Bit 7: Write / Read Flag (1 = Write, 0 = Read)
Bit 6: Space Selector    (1 = EEPROM, 0 = RAM / Control Flags)
Bit 5..0: Address        (0 to 63 / 0x00 to 0x3F)
```

### Address Space Mapping

| Address Range | Space | Description |
| :--- | :--- | :--- |
| `0x00` – `0x21` (0–33) | RAM | Volatile Storage Area (User RAM Array) |
| `0x22` (34) | Control | **`ADDR_FLAG0`** (System Configuration Register) |
| `0x23` (35) | Control | **`ADDR_FLAG1`** (Signal, Signature & Peripheral Status Register) |
| `0x24` (36) | Control | **`ADDR_FLAG2`** (EEPROM Write Status / *Multiplexed with lower 8 bits of ADC when `F_ADC_RD` is active and sleep is off*) |
| `0x25` (37) | Control | **`ADDR_RAM_CLEAR`** (Mass RAM Wipe Trigger) |
| `0x00` – `0x3F` (0–63) | EEPROM | Non-volatile Data Memory Space |

---

## Register Definitions

### `ADDR_FLAG0` (Address 34 / 0x22) — System Control Register

*Default Boot State: `0xA4` (`F_SLEEP=1`, `F_ROM_RO=1`, `F_PULLUP=1`)*

| Bit | Name | Description |
| :---: | :--- | :--- |
| **7** | `F_SLEEP` | `1` = Enable Power-Down Sleep Mode; `0` = Disable Sleep (Run GPIO Handler) |
| **6** | `F_PAGE_RD` | `1` = Enable 5-Byte Page Burst Read Mode; `0` = Single-Byte SPI Read Mode |
| **5** | `F_ROM_RO` | `1` = Software Write Protect EEPROM (Read-Only); `0` = Writable |
| **4** | `F_RAM_RO` | `1` = Software Write Protect SRAM (Read-Only); `0` = Writable |
| **3** | `F_WP_MODE` | `1` = Treat `P_WP` pin as GPIO Input / Sensor; `0` = Treat `P_WP` as HW Write Protect |
| **2** | `F_PULLUP` | `1` = Internal Pull-up active on `P_WP`; `0` = Pull-up disabled |
| **1** | `F_ADC_RD` | `1` = Enable 10-bit ADC Sampling on `PB4`; `0` = Digital Input Mode |
| **0** | `F_DEBOUNCE`| `1` = Enable Watchdog Timer background filtering (requires `F_SLEEP=0`, `F_WP_MODE=1`) |

### `ADDR_FLAG1` (Address 35 / 0x23) — Signal, Signature & Digital Status Register

| Bit | Name | Description |
| :---: | :--- | :--- |
| **7** | `F_TOGGLE` | `1` = Enable latching toggle mode on input; `0` = Momentary state |
| **6** | `F_SIG_VAL` | **Signal Valid Status Indicator**: Automatically set (`1`) if signature bits (bits 5..3) are non-zero; cleared (`0`) otherwise. |
| **5..3** | *Signature Bits* | User-configurable signal/signature pattern bits (`0x38` mask). Used for external peripheral signature verification and handshaking. |
| **2** | `F_DIG_VAL` | Evaluated debounced/toggled digital reading of pin `P_WP` (`1` = HIGH, `0` = LOW) |
| **1..0**| `F_ADC_HI` | High 2 bits of 10-bit ADC result (Bits 9..8) |

### `ADDR_FLAG2` (Address 36 / 0x24) — EEPROM Status / ADC Result Register (Read-Only)

* **During Active ADC Sampling (`F_ADC_RD=1`, `F_SLEEP=0`):** 
  * Holds the **lower 8 bits** of the 10-bit ADC conversion result. EEPROM verification telemetry is conditionally bypassed during active ADC sampling.
* **During Normal / Sleep Operation:** 
  * Updated after every EEPROM write attempt to provide telemetry:
  ```
  Bit 7..6: Status Result Code
            0x40 (0b01) = Write Success (Value changed & updated)
            0x80 (0b10) = Write Skipped (Value identical, EEPROM endurance preserved)
            0xC0 (0b11) = Write Blocked (Hardware WP active or F_ROM_RO set)
  Bit 5..0: Target EEPROM Address (0–63)
  ```

### `ADDR_RAM_CLEAR` (Address 37 / 0x25) — Memory Purge Register

Writing value **`255` (`0xFF`)** to this register zeroes out all 34 bytes of user SRAM (`ram[0..33]`). Writing any other value has no effect.

---

## Sample SPI Command Flows

### 1. Read User RAM Byte at Address 10 (`0x0A`)
* **Command Byte**: `0b00001010` (`0x0A`) — Read (`0`), RAM (`0`), Address `10`.

```
SPI TX: [ 0x0A ] [ 0x00 ]
SPI RX: [ 0xFF ] [ DATA ]
```

### 2. Write EEPROM Byte at Address 5 (`0x05`) with Value `0xAA`
* **Command Byte**: `0b11000101` (`0xC5`) — Write (`1`), EEPROM (`1`), Address `5`.

```
SPI TX: [ 0xC5 ] [ 0xAA ]
SPI RX: [ 0xFF ] [ 0xFF ]
```

### 3. Configure Signature Bits & Handshaking (`FLAG1`)
* Objective: Write signature pattern into bits 5..3 (e.g., `0x18` / binary `011000`) which automatically asserts `F_SIG_VAL` (bit 6).
* **Command Byte**: `0b10100011` (`0xA3`) — Write (`1`), Control Space (`0`), Address `35` (`0x23`).

```
SPI TX: [ 0xA3 ] [ 0x58 ]
SPI RX: [ 0xFF ] [ 0xFF ]
```

### 4. 5-Byte EEPROM Page Burst Read starting at Address 60 (`0x3C`)
* Setup: Set bit `F_PAGE_RD` (bit 6 of `FLAG0`).
* **Command Byte**: `0b01111100` (`0x7C`) — Read (`0`), EEPROM (`1`), Address `60`.

```
SPI TX: [ 0x7C ] [ 0x00 ] [ 0x00 ] [ 0x00 ] [ 0x00 ] [ 0x00 ]
SPI RX: [ 0xFF ] [ E[60] ] [ E[61] ] [ E[62] ] [ E[63] ] [ 0x00 ]
```
*(Note: Address 64 exceeds the 64-byte boundary, so the 5th byte safely returns `0x00`).*

---

## Build & Flash Instructions

### Prerequisites
* `avr-gcc` compiler toolchain
* `avr-libc`
* `avrdude` programming utility
* Hardware programmer (e.g., USBasp, AVRISP mkII, or Arduino as ISP)

### Compilation Command

```bash
avr-gcc -Wall -Os -DF_CPU=9600000UL -mmcu=attiny13a -c main.c -o main.o
avr-gcc -mmcu=attiny13a main.o -o main.elf
avr-objcopy -O ihex -R .eeprom main.elf main.hex
```

### Fuse Bit Configuration

To set the ATtiny13A to run natively at the full **9.6 MHz Internal RC Oscillator** (disabling the default `CKDIV8` prescaler divider):
* **Low Fuse**: `0x7A`
* **High Fuse**: `0xFF`

```bash
avrdude -c usbasp -p t13 -U lfuse:w:0x7A:m -U hfuse:w:0xFF:m -U flash:w:main.hex:i
```

---

## License

This project is open-source under the MIT License.
