# TinyVault — Master Guide

[← README](../README.md) · [Slave guide](SLAVE.md) · [Protocol reference](PROTOCOL.md)

**Component:** `TinyVault.h` · **Class:** `TinyVault` · **Version:** `1.0.0`  
**Author:** Manzar E Hassin · [mnz247@hotmail.com](mailto:mnz247@hotmail.com)

This document covers the Arduino-compatible **software-SPI master** that controls a TinyVault slave on an ATtiny13(A). The master implements transfers using digital GPIO; no `SPI.h` object or hardware SPI peripheral is required.

## 1. Wiring and electrical behavior

| Master example (Uno) | Slave ATtiny13(A) | Function |
| --- | --- | --- |
| D12, input | PB2 / physical pin 7 | MISO |
| D11, output | PB0 / physical pin 5 | MOSI |
| D13, output | PB1 / physical pin 6 | SCK (idle LOW) |
| D10, output | PB4 / physical pin 3 | CS (**active HIGH**, add a 10 kΩ pull-down to GND) |
| GND | GND / physical pin 4 | Shared ground |

Power the slave from a compatible supply (ATtiny13A DIP-8 physical pin 8 is VCC); match logic levels to that supply. For PB3 input/ADC, connect a sensor or logic input to physical pin 2 within the slave's voltage limits.

The constructor is default/no-argument; supply pin mapping using the `init` method. **Parameter order is MISO, MOSI, SCK, CS**, not the order printed in many SPI diagrams.

## 2. Quick start

```cpp
#include <TinyVault.h>

TinyVault tv;

void setup() {
  Serial.begin(9600);
  tv.init(12, 11, 13, 10); // miso, mosi, sck, cs

  tv.writeEnRAM(true);
  tv.writeRAM(7, 0x5A);
  uint8_t value = tv.readRAM(7);
  Serial.println(value, HEX);
}

void loop() {}
```

Upload the slave sketch first; then run the master sketch. For a fuller demonstration, open [`TinyVaultMasterExample.ino`](../examples/TinyVaultMasterExample/TinyVaultMasterExample.ino). That original example executes its whole `Task()` sequence once in `setup()` and uses Serial at `9600` baud.

## 3. Master API reference

All methods below are `public` instance methods on `TinyVault`.

### Initialization

```cpp
void init(uint8_t miso, uint8_t mosi, uint8_t sck, uint8_t cs);
```

Stores the pin mapping; drives CS, SCK, and MOSI LOW and configures them as outputs; configures MISO as an input. Invoke before other methods. The code uses the global Arduino `digitalRead`, `digitalWrite`, `pinMode`, `delay`, and `delayMicroseconds` functions.

### RAM access

```cpp
uint8_t readRAM(uint8_t address);
void writeRAM(uint8_t address, uint8_t value);
void writeEnRAM(bool en);
bool getReadOnlyState_RAM();
```

- Valid SRAM addresses: **`0`–`31`**. `readRAM` returns a byte; out-of-range reads return `0`. Invalid writes are ignored by the master.
- `writeRAM` only changes slave SRAM when the FLAG0 SRAM write-enable bit is set. Call `writeEnRAM(true)` to permit writes and `writeEnRAM(false)` to prohibit them.
- `getReadOnlyState_RAM()` returns **`true` when writes are disabled**, `false` when enabled.
- SRAM is in the slave's `.noinit` section. Do **not** treat it as EEPROM: contents are not guaranteed after power removal and are not initialized to a known value by TinyVault.

### EEPROM / ROM access

```cpp
uint8_t readROM(uint8_t address);
void writeROM(uint8_t address, uint8_t value);
void writeEnROM(bool en);
bool getReadOnlyState_ROM();
void verifyROM(uint8_t &address, uint8_t &result);
```

- `readROM` permits addresses **`0`–`63`**. Address **63** is internal metadata and should not be considered application data. Addresses over `63` return `0`.
- `writeROM` permits addresses **`0`–`62`** and ignores addresses `63` and higher. EEPROM writes also require `writeEnROM(true)`.
- `getReadOnlyState_ROM()` returns **`true` when EEPROM writes are disabled**.
- The slave compares EEPROM contents before writing and verifies the data after any physical write.
- Call `verifyROM(address, result)` **immediately after the write of interest**. This returns the *last requested* EEPROM write address in `address` and its outcome in `result`; it does not start a new verification write.

| `result` | EEPROM status | Interpretation |
| --- | --- | --- |
| `0` | Failed / unverified | Write was attempted but could not be verified |
| `1` | Written | EEPROM programmed and read-back verified |
| `2` | Blocked | EEPROM write-enable was off |
| `3` | Skipped | EEPROM already held the value; read-back matched |

```cpp
tv.writeEnROM(true);
tv.writeROM(12, 0xA5);
uint8_t lastAddress = 0;
uint8_t status = 0;
tv.verifyROM(lastAddress, status);
// status == 1: written+verified; == 3: value already matched
```

**Caution:** EEPROM endurance is finite. Avoid repeatedly changing EEPROM values when not needed. `writeEnROM()` and other control changes can also update the reserved metadata byte when its stored value changes.

### Flags and register reads

```cpp
uint8_t readFlag(uint8_t index);
void writeFlag(uint8_t val);
```

`readFlag(0)` returns the control/status byte, `readFlag(1)` the last EEPROM-write status, and `readFlag(2)` a sample from PB3. Any index greater than `2` returns `0` through this API. `writeFlag` directly submits a new FLAG0 byte (`0xA0` command); the slave normalizes incompatible ADC/GPIO/sleep combinations and persists selected bits in EEPROM metadata.

For everyday applications, prefer the dedicated write-enable and power methods over raw `writeFlag` manipulation. The complete FLAG0 bit table is in [PROTOCOL.md](PROTOCOL.md#flag0-control-bit-fields).

### Write protection and signature marker

```cpp
void writeEnRAM(bool en);
void writeEnROM(bool en);
bool getReadOnlyState_RAM();
bool getReadOnlyState_ROM();

void writeSignature(uint8_t val);
bool verifySignature();
```

`writeSignature` replaces **bits `0`–`2` of FLAG0** with `val & 0x07`; the remaining bits are preserved. `verifySignature` returns `true` when **any** of those three bits is nonzero. It does *not* compare against a known token, authenticate the slave, or validate EEPROM contents. The low three bits are **not included** in the metadata restored after reboot.

### PB3 GPIO and ADC

```cpp
void setPower_GPIO(bool en);
void setPower_ADC(bool en);
void setPower_Sleep(bool en);

bool getState_GPIO();
bool getState_ADC();
bool getState_Sleep();

bool digitalRead();
uint8_t analogRead();
```

The dedicated signal is **PB3 (physical pin 2, ADC3)**. It is separate from the software SPI pins.

- `setPower_GPIO(true)` enables PB3 sampling. `setPower_GPIO(false)` disables both GPIO and ADC modes.
- `setPower_ADC(true)` enables GPIO **and** ADC, and clears the sleep-enable flag. `setPower_ADC(false)` disables ADC alone.
- `setPower_Sleep(true)` enables optional idle sleep and disables ADC. `setPower_Sleep(false)` disables that sleep policy.
- The `getState_*` methods report the corresponding current FLAG0 bits; they do not measure instantaneous electrical activity or CPU power use.
- When GPIO is disabled, sampling returns `0`.
- With GPIO on and ADC off, the sampled result is the digital input value `0` or `1`.
- With GPIO and ADC both enabled, the sampled result is the **upper eight bits** of the slave ADC3 conversion (`0`–`255`). The ADC reference is **VCC**. `digitalRead()` uses only the low bit of that returned sample, so use it specifically in digital mode rather than as an ADC threshold check.

**Digital PB3 example:**

```cpp
tv.setPower_ADC(false);
tv.setPower_GPIO(true);
bool level = tv.digitalRead();
```

**8-bit ADC3 example:**

```cpp
tv.setPower_ADC(true);  // also enables GPIO and disables idle sleep
uint8_t sample = tv.analogRead();
```

Actual conversion happens on the slave when it processes the flag-read request; the master returns the sample in the subsequent fetch transaction. No ADC sample is made simply by calling `getState_ADC()`.

## 4. Read/write protocol mechanics

The master uses a **bit-banged, full-duplex, MSB-first** transfer with SCK idle LOW. A `writeRAM`, `writeROM`, or `writeFlag` executes a two-byte command. A `readRAM`, `readROM`, or `readFlag` executes the two-byte read request, deselects, then starts a new selection and transmits `0x0A` while reading the response.

The reference master's built-in timing constants are:

| Internal constant | Value | Use |
| --- | --- | --- |
| `HALF_US` | `100` µs | Hold before clock rising and at clock high |
| `SELECT_SETUP_US` | `1000` µs | Wait after CS asserted |
| `ORDINARY_US` | `1000` µs | Delay after ordinary command/transfer |
| `ROM_MS` | `20` ms | Delay after commands marked EEPROM-related |

These are implementation delays, not separately validated operating limits for alternative masters. Because the implementation uses GPIO calls, actual clock intervals include software overhead. Consult [the protocol document](PROTOCOL.md) if implementing a custom master.

## 5. What the shipped master example does

The unchanged demo's `Task()` calls, in order:

1. Wait 2 seconds; read all 32 SRAM addresses; write values `100`–`131` to SRAM.
2. Read 64 EEPROM addresses, including reserved address `63`.
3. Attempt writes to EEPROM user addresses `0`–`62`, printing each write status.
4. Check EEPROM write protection; if locked, enable EEPROM writes; inspect SRAM write protection.
5. Repeat the EEPROM user-address writes and reread all 64 EEPROM addresses.
6. Check/set the three-bit signature marker and demonstrate PB3 reads before and after enabling GPIO/ADC.

This demonstration **can overwrite EEPROM user data** and **changes control flags**; choose whether to run it on a device with saved content. The demo initializes with `tv.init(12, 11, 13, 10)` and outputs at `9600` baud.

## 6. Master troubleshooting

- **Always-zero/unexpected data:** confirm pin order to `init`, both power/ground connections, pull-down, SCK idle polarity, and the correct slave firmware.
- **Write disabled:** `getReadOnlyState_RAM()` and `_ROM()` both return `true` for **read-only**, despite the corresponding `writeEn*` methods using `true` to enable writing.
- **EEPROM result address wrong:** `verifyROM` tracks only the **latest** EEPROM write request; query it after each write when logging a batch.
- **ADC reads `0` or `1` only:** ADC may not be enabled; call `setPower_ADC(true)` and verify PB3 input.
- **Using hardware SPI:** conventional peripherals typically expect active-LOW chip select. The shipped master already implements the protocol in software.

See [Slave guide](SLAVE.md) for MicroCore programming and resource limits, and [Protocol reference](PROTOCOL.md) for flags/opcodes.
