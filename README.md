# TinyVault

**Software SPI memory, GPIO, and ADC access for an ATtiny13/ATtiny13A, controlled by an Arduino-compatible master.**

[![Version](https://img.shields.io/badge/version-1.0.0-blue)](#version-and-compatibility) [![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)

**Author:** Manzar E Hassin · [mnz247@hotmail.com](mailto:mnz247@hotmail.com)  
**Project:** <https://github.com/manzarehassin/TinyVault>  
**License:** [MIT](LICENSE) · **Version:** `1.0.0`

TinyVault connects two controllers without hardware SPI on the slave. A lightweight Arduino-side `TinyVault` class bit-bangs a four-wire, MSB-first interface; the ATtiny13(A) runs a software SPI slave driven by its pin-change interrupt. The slave exposes **32 bytes of SRAM**, **63 user-addressable EEPROM bytes**, a status/control register, and an optional **PB3 digital-input / ADC3** channel.

> [!IMPORTANT]
> TinyVault uses **active-HIGH CS** (not the usual active-LOW SPI select). **SCK must be LOW when CS rises.** The ATtiny13(A) slave must be built at **1.2 MHz**, and its CS pin needs a **10 kΩ pull-down** to GND. Use the documented software-SPI master; do not assume a default hardware SPI transaction will use the correct selection polarity or framing.

## Contents

- [Features](#features)
- [How it works](#how-it-works)
- [Hardware and wiring](#hardware-and-wiring)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Memory, registers, and API](#memory-registers-and-api)
- [Examples](#examples)
- [Version and compatibility](#version-and-compatibility)
- [Troubleshooting](#troubleshooting)
- [Documentation and repository layout](#documentation-and-repository-layout)
- [License and author](#license-and-author)

## Features

- **No hardware SPI needed on the slave:** bit-level handling is performed in `PCINT0_vect` using AVR assembly in the time-critical path.
- **Configurable master GPIO pins:** software SPI with MSB-first transfers and Mode-0-style idle-low clocking.
- **32-byte SRAM window:** addresses `0x00`–`0x1F`, with write-enable control.
- **EEPROM window:** reads of `0x00`–`0x3F`; writes to **`0x00`–`0x3E` only**. The final byte (`0x3F`) is reserved for internal configuration metadata.
- **EEPROM write feedback:** written/verified, blocked, already matched, or failed.
- **PB3 auxiliary input:** digital input or 8-bit ADC3 sample, using VCC as the ADC reference.
- **Power-management control:** GPIO/ADC gating and optional idle sleep on the slave.
- **No heap allocation** in the master class; compact slave state sized for ATtiny13(A) resources.

## How it works

TinyVault uses four signals: `MOSI`, `MISO`, `SCK`, and `CS`. The master raises **CS to select** the slave, waits for setup, clocks bits most-significant first, and lowers CS to end a transaction. The slave captures/serves each transaction in its pin-change ISR.

There are two types of operations:

1. **Writes and control commands:** send a **16-clock frame** consisting of an opcode and an operand/value, then deselect.
2. **Reads:** first send a **16-clock request frame**; then select again and send an **8-clock fetch byte `0x0A`** while receiving the requested value on MISO. A new ordinary command cannot replace an outstanding response; the protocol handles skipped or malformed fetches as frame errors.

The master uses built-in setup/recovery delays; EEPROM-related commands use longer waits. These timings are part of the provided implementation. For an opcode table, register bit fields, and timing details, see **[Protocol reference](docs/PROTOCOL.md)**.

## Hardware and wiring

Typical hardware: Arduino Uno (master), ATtiny13A DIP-8 (slave), suitable regulated power, common ground, jumper wires, and a **10 kΩ resistor** from CS to GND. The master pins below are the supplied example mapping and can be remapped through `init()`.

| Signal | Arduino Uno | ATtiny13A | Direction |
| --- | --- | --- | --- |
| CS (active HIGH) | D10 | PB4, physical pin 3 | Master → slave; **10 kΩ to GND** |
| MOSI | D11 | PB0, physical pin 5 | Master → slave |
| MISO | D12 | PB2, physical pin 7 | Slave → master |
| SCK (idle LOW) | D13 | PB1, physical pin 6 | Master → slave |
| Optional GPIO / ADC3 | — | PB3, physical pin 2 | External signal → slave |
| Ground | GND | GND, physical pin 4 | Common reference |
| Supply | Appropriate VCC | VCC, physical pin 8 | Use a compatible supply |

```text
Arduino Uno (master)                    ATtiny13A (slave, DIP-8)
D10  --------------------- CS --------> PB4 (pin 3)
                                           |
                                           +--[10 kΩ]-- GND
D11  --------------------- MOSI ------> PB0 (pin 5)
D12  <-------------------- MISO ------- PB2 (pin 7)
D13  --------------------- SCK -------> PB1 (pin 6)
GND  --------------------- GND -------- GND (pin 4)
Compatible VCC ----------- VCC -------- VCC (pin 8)
External sensor / input --------------- PB3 (pin 2), optional
```

**Electrical notes:** Share GND, match interface logic levels to the supply voltages, and do not drive ATtiny inputs outside their permitted voltage range. PB3 is independent of the SPI lines. The master example assumes Uno-style GPIO numbering, not that the slave uses those numbers.

## Installation

### 1. Set up the slave toolchain

Install the **MicroCore** Arduino board package for ATtiny13/ATtiny13A and have an ISP programmer available (for example, Arduino as ISP). Select the **ATtiny13** board and the exact **1.2 MHz internal oscillator** profile. The full list of source-specified MicroCore settings is in [Slave setup](docs/SLAVE.md#arduino-ide--microcore-settings).

The slave header intentionally checks the target MCU and `F_CPU == 1200000UL` at compile time; other targets or clock profiles are not the documented build.

### 2. Install the TinyVault library

Install the **`TinyVault` library folder** (the folder containing `TinyVault.h`, `TinyVaultSlave.h`, and `library.properties`) into your Arduino sketchbook's `libraries` directory, or use **Sketch → Include Library → Add .ZIP Library…** with the distributed ZIP. Restart the IDE if necessary.

The headers are placed at the library root so both example sketches can find their respective `#include` directives unchanged.

### 3. Flash the slave, then the master

1. Open `examples/TinyVaultSlave/TinyVaultSlave.ino` in the Arduino IDE.
2. Set the MicroCore options exactly as described in [Slave setup](docs/SLAVE.md); upload using an **ISP programmer**, not a serial bootloader.
3. Wire master and slave as above, including the CS pull-down and common ground.
4. Open `examples/TinyVaultMasterExample/TinyVaultMasterExample.ino` and select the Arduino master board and port.
5. Upload the master sketch and open Serial Monitor at **9600 baud**. Its test sequence runs **once** from `setup()`.

> [!NOTE]
> The original master example writes SRAM and tries EEPROM writes, changes write-enable state, writes a signature flag, and enables ADC sampling. It is a demonstration, not a read-only diagnostic. On valuable EEPROM contents, review the example's actions before running it.

## Quick start

For a new master sketch, include the header and construct an instance:

```cpp
#include <TinyVault.h>

TinyVault vault;

void setup() {
  Serial.begin(9600);
  vault.init(12, 11, 13, 10);  // MISO, MOSI, SCK, CS (in that order)

  // Enable SRAM writes, store one byte, then read it back.
  vault.writeEnRAM(true);
  vault.writeRAM(0, 42);
  Serial.println(vault.readRAM(0));

  // EEPROM writes require a separate enable.
  vault.writeEnROM(true);
  vault.writeROM(0, 123);

  uint8_t address = 0;
  uint8_t result = 0;
  vault.verifyROM(address, result);  // Result applies to the last EEPROM write.
  Serial.print("EEPROM status: ");
  Serial.println(result);
}

void loop() {}
```

A minimal slave sketch is the provided:

```cpp
#include "TinyVaultSlave.h"

void setup() { TinyVaultSlave::begin(); }
void loop()  { TinyVaultSlave::sleepIfIdle(); }
```

The slave runs communication inside the ISR; `loop()` is **not** required to poll SPI. `sleepIfIdle()` is an optional power-saving hook, and can be omitted in applications that need a busy foreground loop.

## Memory, registers, and API

| Resource | Address/index | Behavior |
| --- | --- | --- |
| SRAM | `0`–`31` | 32 bytes; non-initialized `.noinit` storage; not guaranteed across power loss |
| EEPROM user data | `0`–`62` | 63 bytes; writing requires EEPROM write-enable |
| EEPROM metadata | `63` (`0x3F`) | **Reserved**; readable through `readROM(63)`, not user-writable through `writeROM()` |
| FLAG0 | `readFlag(0)` | Write-enable, power, sleep, and small signature field |
| FLAG1 | `readFlag(1)` | Last EEPROM-write address and result code |
| FLAG2 | `readFlag(2)` | PB3 digital read or ADC3 sample, depending on flags |

### Main master methods

| Method | Purpose |
| --- | --- |
| `init(miso, mosi, sck, cs)` | Configure pin mapping and idle levels |
| `readRAM(address)` / `writeRAM(address, value)` | Read/write SRAM addresses `0`–`31` |
| `readROM(address)` / `writeROM(address, value)` | Read EEPROM `0`–`63`; write user EEPROM `0`–`62` |
| `writeEnRAM(bool)` / `writeEnROM(bool)` | Set write permissions |
| `getReadOnlyState_RAM()` / `getReadOnlyState_ROM()` | `true` means that resource is currently write-protected |
| `verifyROM(address, result)` | Query the most recent EEPROM-write result |
| `readFlag(index)` / `writeFlag(value)` | Read public flag indices `0`–`2` / set FLAG0 |
| `setPower_GPIO(bool)` / `setPower_ADC(bool)` / `setPower_Sleep(bool)` | Control auxiliary GPIO, ADC, and sleep flags |
| `getState_GPIO()` / `getState_ADC()` / `getState_Sleep()` | Read current control states |
| `digitalRead()` / `analogRead()` | Read PB3 as digital input or an 8-bit ADC3 sample |
| `writeSignature(value)` / `verifySignature()` | Set low three FLAG0 bits / test whether any are nonzero |

**EEPROM result codes** from `verifyROM()`:

| `result` | Meaning |
| --- | --- |
| `0` | Attempted write not verified / failed |
| `1` | Value written and verified |
| `2` | Blocked because EEPROM writes are disabled |
| `3` | Existing value already matched; write skipped, value verified |

FLAG0 controls have interactions: enabling ADC enables GPIO and clears sleep; enabling sleep disables ADC; disabling GPIO also disables ADC. The three low signature bits are a simple **nonzero marker**, not authentication and **not persisted** in EEPROM metadata. See the [Master API reference](docs/MASTER.md) and [Slave details](docs/SLAVE.md) for exact semantics.

## Examples

**`TinyVaultMasterExample.ino`** exercises memory reading/writing, EEPROM write-status reporting, EEPROM write-enable, the signature flag, and ADC3 reads. It prints to `Serial` at 9600 baud and calls `Task()` once from `setup()`. Its demonstrations may change EEPROM contents (addresses `0`–`62`).

**`TinyVaultSlave.ino`** calls `TinyVaultSlave::begin()` and repeatedly calls `TinyVaultSlave::sleepIfIdle()`; the actual SPI protocol is implemented in `TinyVaultSlave.h`.

The `.ino` files are shipped unmodified from the provided sources. Their folder names match the sketch filenames, as required for straightforward Arduino IDE use.

## Version and compatibility

- **Source version:** `1.0.0` (source headers dated 23 October 2026).
- **Master:** Arduino-compatible environment providing `Arduino.h`, `pinMode`, `digitalWrite`, `digitalRead`, `delay`, and `delayMicroseconds`. The supplied example targets Arduino Uno pin numbers.
- **Slave:** **ATtiny13 or ATtiny13A**, built using AVR support/MicroCore with **`F_CPU = 1200000UL`**. The implementation uses device-specific AVR registers, an AVR pin-change ISR, and inline assembly; it is **not** a generic AVR SPI-slave library.
- **Timing:** The reference master uses `100 µs` bit-half delays, `1000 µs` chip-select setup, `1000 µs` ordinary operation recovery, and `20 ms` for EEPROM-related operations. These are software delays and should not be interpreted as a rated SPI frequency independent of GPIO overhead.
- **Slave SRAM budget:** 38 bytes for static TinyVault state/storage, leaving only 26 bytes of the ATtiny13(A)'s 64-byte SRAM for stack and any application globals. Keep additional slave code minimal.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Reads return `0xAA`, unexpected values, or no response | Wire `MISO`/`MOSI` in the correct directions; verify **CS active-HIGH**, CS pull-down, idle-LOW SCK, common GND, and the ATtiny's 1.2 MHz clock. `0xAA` is also the slave's idle/ACK fill byte. |
| Slave compile reports wrong MCU or clock | Select ATtiny13/ATtiny13A with the 1.2 MHz internal clock; `TinyVaultSlave.h` has compile-time target/clock checks. |
| SRAM write does not stick | Call `writeEnRAM(true)`; check `getReadOnlyState_RAM()` (`true` means locked). |
| EEPROM write is blocked | Call `writeEnROM(true)` and inspect `verifyROM()` after **each** write. Do not attempt to write reserved address `63`. |
| `analogRead()` is `0` or `1` | Enable GPIO and ADC (`setPower_ADC(true)` does both); check PB3 wiring and signal level. With ADC disabled, PB3 sampling is digital. |
| No output appears in Serial Monitor | Use **9600 baud** and reset the master: its example logs once in `setup()`, with an initial 2-second pause. |
| Sleep or sampling behaves unexpectedly | Check interactions between GPIO, ADC, and sleep flags; see [FLAG0](docs/PROTOCOL.md#flag0-control-bit-fields). |

## Documentation and repository layout

- **[Master guide — `docs/MASTER.md`](docs/MASTER.md):** wiring, quick start, every public master API, memory access, and control behavior.
- **[Slave guide — `docs/SLAVE.md`](docs/SLAVE.md):** MicroCore settings, ISR design, registers, power management, memory budget, and diagnostics.
- **[Protocol specification — `docs/PROTOCOL.md`](docs/PROTOCOL.md):** frame grammar, opcodes, flag registers, fetch behavior, EEPROM result encoding, and timing.

```text
TinyVault/
├── README.md
├── LICENSE
├── library.properties
├── TinyVault.h                         # Original master header, unchanged
├── TinyVaultSlave.h                    # Original ATtiny13(A) slave header, unchanged
├── docs/
│   ├── MASTER.md
│   ├── SLAVE.md
│   └── PROTOCOL.md
├── examples/
│   ├── TinyVaultMasterExample/
│   │   └── TinyVaultMasterExample.ino    # Original example, unchanged
│   └── TinyVaultSlave/
│       └── TinyVaultSlave.ino           # Original example, unchanged
└── SHA256SUMS.txt                       # SHA-256 checksums of original source files
```

## License and author

Copyright © 2026 **Manzar E Hassin**. Released under the **MIT License**; see [LICENSE](LICENSE). The included code identifies the author as `Manzar-E-Hassin` and references the project repository above.

**Maintainer contact:** [mnz247@hotmail.com](mailto:mnz247@hotmail.com)
