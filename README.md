# TinyVault

**TinyVault** is an ultra-secure, power-optimized, memory-safe bit-banged SPI slave vault firmware engineered for the **Microchip ATtiny13A** microcontroller. It provides an isolated memory architecture with hardware-assisted sleep control, software write protection, deferred EEPROM persistence, dual-mode GPIO/ADC sampling, and strict register-level access control.

---

## Technical Specifications

| Parameter | Specification |
| :--- | :--- |
| **Target Microcontroller** | Microchip ATtiny13A (8-pin DIP/SOIC) |
| **Clock Frequency (`F_CPU`)** | $1.2\text{ MHz}$ Internal RC Oscillator (`CKDIV8` disabled) |
| **SPI Mode** | Bit-banged SPI Mode 0 (Slave mode) |
| **User RAM Capacity** | 32 Bytes (`0x00` – `0x1F`) |
| **User EEPROM Capacity** | 63 Bytes (`0x00` – `0x3E`) |
| **Metadata Storage** | 1 Byte (`0x3F` / EEPROM Address 63) |
| **Sleep Mode** | Power-Down Mode (`MCUCR` setup, awakened via Pin Change Interrupt `PCINT4`) |

---

## Pinout Mapping

The firmware directly controls Microchip ATtiny13A Port B I/O pins as follows:

| Pin Name | Physical Pin | Direction | Description |
| :---: | :---: | :---: | :--- |
| `PB0` (`PB_MOSI`) | Pin 5 | Input | SPI Master Out Slave In Line |
| `PB1` (`PB_MISO`) | Pin 6 | Output | SPI Master In Slave Out Line |
| `PB2` (`PB_SCK`) | Pin 7 | Input | SPI Serial Clock Input Line |
| `PB3` (`PB_GPIO`) | Pin 2 | Input | Dual-mode Input (Digital Read or ADC Channel 3 / `ADC3`) |
| `PB4` (`PB_CS`) | Pin 3 | Input | Chip Select Line (Active LOW) with internal pull-up & `PCINT4` wake-up |
| `PB5` (`RESET`) | Pin 1 | Input | External Reset / Programming |

---

## Memory Map & Control Registers

TinyVault implements a unified memory map combining volatile RAM, control/status registers, and non-volatile EEPROM.

### 1. RAM & Register Space (35 Direct Addresses)

* **Addresses `0` – `31` (`0x00` – `0x1F`):** `user_ram[32]` — Volatile general-purpose memory.
* **Address `32` (`0x20`):** `FLAG0` — Device Configuration & Status Register.
* **Address `33` (`0x21`):** `FLAG1` — EEPROM Operations Status Register.
* **Address `34` (`0x22`):** `FLAG2` — ADC Conversion / Digital Input Storage Register.

#### Register Layouts

##### `FLAG0` (Address 32 / `RAM_SIZE`) — Control & Configuration
* **Bit 7 (`BUSY`):** System busy state (`1` = Busy executing background tasks).
* **Bit 6 (`ROM_RO`):** User EEPROM Read-Only lock status bit (Synced with EEPROM metadata).
* **Bit 5 (`RAM_RO`):** User RAM Read-Only write protection flag (`1` = RAM write-protected).
* **Bit 4 (`ADC_CTRL`):** ADC Control (`1` = ADC active on `PB3`, `0` = Digital Input / Power-Down enabled).
* **Bits 3–1 (`SIG`):** 3-bit user-configurable device signature flags.
* **Bit 0 (`PENDING`):** Deferred EEPROM write pending flag (`1` = Staged payload waiting for `CS` HIGH transition).

> **Write Protection for `FLAG0`:** Bits 7 and 0 are system-protected. Master writes via SPI can only modify bits 6 through 1 (`flag0 = (flag0 & 0x81) | (data & 0x7E)`). Modifying bits 6–5 triggers a metadata synchronization write to EEPROM address 63.

##### `FLAG1` (Address 33 / `RAM_SIZE + 1`) — EEPROM Execution Status
* **Bit 7 (`SKIP`):** Set to `1` if a physical write was skipped because the target EEPROM byte already matched the payload.
* **Bit 6 (`VERIFY`):** Set to `1` if post-write verification succeeded.
* **Bits 5–0 (`ADDR`):** Stored address of the last processed EEPROM operation (User range: 0–62).

> **Access Restriction:** Direct SPI writes to `FLAG1` (Address 33) are **strictly blocked**.

##### `FLAG2` (Address 34 / `RAM_SIZE + 2`) — Data Input Storage
* **When ADC is Enabled (`FLAG0.bit4 == 1`):** Holds the 8-bit downsampled analog value from `PB3` (`(ADC_10bit + 2) >> 2`).
* **When ADC is Disabled (`FLAG0.bit4 == 0`):** Holds the live digital input state of `PB3` (`0x01` if HIGH, `0x00` if LOW) if `DIGITAL_PIN_READ_ENABLE` is set to `1`.

> **Access Restriction:** Direct SPI writes to `FLAG2` (Address 34) are **strictly blocked**.

---

### 2. EEPROM Space (64 Bytes)

* **Addresses `0` – `62` (`0x00` – `0x3E`):** User non-volatile persistent storage.
* **Address `63` (`0x3F`):** System Metadata Storage (Stores packed protection bits `RAM_RO` and `ROM_RO`).

---

## SPI Protocol Specification

TinyVault utilizes a bit-banged SPI Mode 0 interface ($\text{CPOL}=0, \text{CPHA}=0$). Transactions are bounded by `PB_CS` (PB4).

### Frame Structure

Every frame begins with an 8-bit Command Header byte:
$$\text{Header Byte} = [\text{Opcode (2 bits)}] \;\vert\; [\text{Address / Modifier (6 bits)}]$$

```
 Bit:   7   6   5   4   3   2   1   0
      +-------+-----------------------+
      | OPCODE|     ADDRESS (0-63)    |
      +-------+-----------------------+
```

| Opcode (`bits 7-6`) | Operation Class | Command Frame Sequence |
| :---: | :--- | :--- |
| `0b00` | **RAM / Register Read** | **MOSI:** `[00 | Addr]` $\rightarrow$ **MISO:** Returns `[Data Byte]` |
| `0b01` | **RAM / Register Write** | **MOSI:** `[01 | Addr]` $\rightarrow$ `[Payload Byte]` |
| `0b10` | **EEPROM Read** | **MOSI:** `[10 | Addr]` $\rightarrow$ **MISO:** Returns `[EEPROM Data]` |
| `0b11` | **EEPROM Deferred Write** | **MOSI:** `[11 | Addr]` $\rightarrow$ `[Payload Byte]` |

### System Response & Busy Guards

1. **Busy State Rejection:** If `FLAG0.bit7` is set (`BUSY`), any incoming transaction while `CS` is LOW continuously shifts out the error byte `0x80` over MISO.
2. **Early Transfer Abort:** The bit-banged SPI transfer function (`spi_transfer_byte`) continuously monitors `PB_CS`. If `CS` transitions HIGH mid-byte, the function immediately aborts execution and returns `0x00`.

---

## Key Firmware Logic & Safety Subsystems

### 1. Deferred EEPROM Write Cycle & Wear Leveling
To avoid blocking SPI clock lines during long physical write operations (~3.4 ms per byte), TinyVault implements a deferred write mechanism:
1. **In-Frame Staging:** When an EEPROM write command (`Opcode 11`) is received, the target address and data byte are saved into `pending_eeprom_addr` and `pending_eeprom_data`. `FLAG0.bit0` (`PENDING`) is set to `1`.
2. **Post-Frame Execution:** When `CS` goes HIGH, `spi_management()` sets `FLAG0.bit7` (`BUSY`) and invokes `eeprom_write_handling()`.
3. **Wear-Reduction Check:** Before writing, the existing byte is read from EEPROM.
   * If existing data equals the payload, the write is skipped, `FLAG1.bit7` is set to `1`, and `FLAG1.bit6` is cleared.
   * If data differs, physical programming is performed (`eeprom_write`), and a post-write verification read is performed to set or clear `FLAG1.bit6`.
4. **Hardware Polling:** Physical write calls poll the hardware `EEPE` bit (`while (EECR & (1 << EEPE));`) to safeguard hardware charge pumps before clearing the `PENDING` and `BUSY` flags.

### 2. EEPROM Metadata Persistence (`metadataRead` / `metadataWrite`)
EEPROM Address 63 stores the hardware protection states for `RAM_RO` (`FLAG0.bit5`) and `ROM_RO` (`FLAG0.bit6`):
* **Initialization:** `init_config()` executes `metadataRead()`, restoring bits 6 and 5 of `FLAG0` from EEPROM Address 63 bits 1 and 0 (`(buf << 5) & 0x60`).
* **Write Sync:** Writing to `FLAG0` (Address 32) triggers `metadataWrite()`. If the new protection mask differs from EEPROM Address 63, the metadata is updated in non-volatile memory.

### 3. Power-Down Sleep Engine & Interrupt Wake-Up
* **Wake-Up Interrupt:** Pin change interrupt `PCINT0_vect` listens to `PB4` (`CS`). On a falling edge (CS going LOW), `GIMSK` masks off `PCIE` temporarily to process `spi_management()` in a synchronous, blocking context.
* **Auto-Sleep Conditions:** Inside `main()`, the system checks conditions on every loop iteration:
  $$\text{Sleep Allowed} \iff (\text{FLAG0.bit0} == 0) \land (\text{FLAG0.bit4} == 0)$$
  If no EEPROM writes are pending (`PENDING == 0`) and the ADC is disabled (`ADC_CTRL == 0`), the MCU enters `MCUCR` Power-Down sleep via inline assembly (`__asm__ __volatile__("sleep" ::: "memory");`).

---

## Building and Flashing

### Firmware Compilation (AVR-GCC)
Compile the source file using `avr-gcc` targeted for `attiny13a`:

```bash
avr-gcc -Wall -Os -DF_CPU=1200000UL -mmcu=attiny13a -o tinyVault.elf ATTINY_13A_tinyVault_firmware.ino
avr-objcopy -O ihex -R .eeprom tinyVault.elf tinyVault.hex
```

### Flashing via AVRDUDE
Flash the `.hex` file using an ISP programmer (e.g., USBasp or Arduino as ISP):

```bash
avrdude -c usbasp -p t13 -U flash:w:tinyVault.hex:i
```

#### Fuse Bit Configuration (1.2 MHz Internal RC)
Ensure the fuses are set for the 9.6 MHz internal oscillator with `CKDIV8` enabled (resulting in 1.2 MHz CPU clock):
* **Low Fuse:** `0x6A`
* **High Fuse:** `0xFF`

```bash
avrdude -c usbasp -p t13 -U lfuse:w:0x6A:m -U hfuse:w:0xFF:m
```

---

## License

This project is licensed under the **MIT License**.

Copyright (c) 2026 **Manzar-E-Hassin**  
Source Repository: [https://github.com/manzarehassin/TinyVault](https://github.com/manzarehassin/TinyVault)
