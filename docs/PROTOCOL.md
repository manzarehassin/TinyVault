# TinyVault — Wire Protocol and Register Reference

[← README](../README.md) · [Master guide](MASTER.md) · [Slave guide](SLAVE.md)

This specification describes the implemented master/slave communication in the provided **v1.0.0** headers. It is intended as a technical reference; the shipped `TinyVault.h` already performs the transactions correctly for the companion slave.

## 1. Physical framing

- **Signal lines:** CS, SCK, MOSI, MISO with a shared ground.
- **CS polarity:** **active HIGH**; inactive LOW (use a **10 kΩ CS pull-down** at ATtiny13(A) PB4).
- **Clock:** SCK idle LOW, sample MOSI on the rising edge, shift output toward the next bit after the falling edge (Mode-0-style).
- **Bit order:** MSB first.
- **Full duplex:** one MISO bit is sampled on each MOSI clock; only the fetched response from a pending read is the application-level returned value.
- **Select/deselect:** CS rises to start a transaction and falls to close it; while inactive, slave MISO is high impedance.
- **Supported frame lengths:** an ordinary **16-bit command** or a **single-byte/8-bit response fetch**.

### Command frame: 16 clocks

```text
CS    ____/--------------------------------\____
SCK   _____/\_/\_ ... 16 complete pulses ... _____
MOSI       [ OPCODE, 8 bits ] [ OPERAND, 8 bits ]
MISO       [ previous TX / fill ] [ 0xAA fill ]
```

The slave processes the opcode and operand **after CS returns LOW**. Read requests prepare a pending response in `tv_tx`.

### Read-fetch frame: 8 clocks

```text
(First) request frame:    [ read opcode ][ address/index ]   CS LOW
(Second) fetch frame:    [      0x0A on MOSI      ]          CS LOW
MISO during fetch:       [    requested 8-bit value ]
```

Do not issue a second ordinary command between a read request and its fetch. A new 16-bit frame with a response pending causes the pending reply to be discarded and a bad-frame diagnostic to be recorded, rather than executing that command. A fetch that does not match `0x0A` also counts as bad. The slave fills idle/non-pending TX data with `0xAA`, which is **not** by itself an acknowledgement of a successful write.

## 2. Opcode map

| Operation | Command first byte | Second byte | Result |
| --- | --- | --- | --- |
| SRAM read | `0x30` | Address `0x00`–`0x1F` | Prepare response, then fetch `0x0A` |
| FLAG0 read | `0x30` | `0x20` | Prepare FLAG0, then fetch |
| FLAG1 read | `0x30` | `0x21` | Prepare FLAG1, then fetch |
| PB3 sample / FLAG2 read | `0x30` | `0x22` | Sample PB3, prepare response, then fetch |
| Diagnostics read | `0x30` | `0x23` | Prepare ISR diagnostic byte, then fetch |
| EEPROM read | `0x33` | Address `0x00`–`0x3F` | Prepare EEPROM byte, then fetch |
| SRAM write | `0x80 | address` | Value | Only addresses `0x00`–`0x1F`; requires RAM write-enable |
| FLAG0 write | `0xA0` | New FLAG0 value | Normalize flags and persist selected state |
| EEPROM write | `0xC0 | address` | Value | Only addresses `0x00`–`0x3E`; requires ROM write-enable |
| Read fetch | `0x0A` in an **8-clock frame** | No second byte | Clock out pending data on MISO |

The EEPROM write opcode interval is **`0xC0`–`0xFE`**, not `0xFF`: EEPROM `0x3F` is reserved for metadata. Invalid command/address combinations have no normal command effect; malformed frames can set diagnostic bits.

The master exposes read FLAG indices `0`–`2` only; the diagnostics read (`0x30 0x23` followed by fetch) is a protocol-level capability rather than a dedicated public `TinyVault` method.

## 3. FLAG0 control bit fields

FLAG0 is addressed by read-request `0x30 0x20`, and written using `0xA0 value`.

| Bit(s) | Mask | Function | Interpretation |
| --- | --- | --- | --- |
| 7 | `0x80` | SRAM write enable | `1` permits SRAM writes; `0` protects RAM |
| 6 | `0x40` | EEPROM write enable | `1` permits user EEPROM writes; `0` protects EEPROM |
| 5 | `0x20` | PB3 GPIO enabled | `1` enables GPIO/digital sample path |
| 4 | `0x10` | ADC enabled | `1` allows ADC3 sampling; requires GPIO on and sleep off |
| 3 | `0x08` | Sleep enabled | `1` permits optional idle sleep via `sleepIfIdle()` |
| 2:0 | `0x07` | Signature marker | `verifySignature()` reports true if this field is nonzero |

**Interactions implemented by the master:**

- `setPower_ADC(true)`: sets bits 5 and 4; clears bit 3.
- `setPower_ADC(false)`: clears bit 4.
- `setPower_GPIO(true)`: sets bit 5.
- `setPower_GPIO(false)`: clears bits 5 and 4.
- `setPower_Sleep(true)`: sets bit 3; clears bit 4.
- `setPower_Sleep(false)`: clears bit 3.
- `writeEnRAM` and `writeEnROM`: change bits 7 and 6, respectively.
- `writeSignature(value)`: replaces bits 2:0 with `value & 0x07`.

The slave further **normalizes** FLAG0: if `(FLAG0 & 0x28) != 0x20` (GPIO not enabled or sleep enabled), ADC bit 4 is cleared.

### Defaults and nonvolatile metadata

The fallback startup value is **`0x88`** (RAM write enabled, sleep enabled; EEPROM write disabled). If EEPROM location `63` holds a recognized metadata byte `(byte & 0xF8) == 0xB0`, startup restores only **bits 7, 6, and 3** from it. A FLAG0 write uses metadata encoding:

```cpp
metadata = 0xB0
         | ((flag0 >> 3) & 0x01) // sleep -> metadata bit 0
         | ((flag0 >> 6) & 0x02) // RAM write enable -> metadata bit 1
         | ((flag0 >> 4) & 0x04); // EEPROM write enable -> metadata bit 2
```

PB3 GPIO, ADC, and signature markers are **not persisted** by this encoding. EEPROM location `63` is reserved, though readable with `readROM(63)`.

## 4. FLAG1 — last EEPROM write

FLAG1 is read through `0x30 0x21` plus a fetch. Format:

```text
  bit:    7 6 | 5 4 3 2 1 0
          code | address (0–62)
```

The public master helper `verifyROM(uint8_t &address, uint8_t &result)` decodes the lower six bits as the last **requested EEPROM write address**, and the upper two as the result:

| Bits `7:6` | `result` | Meaning |
| --- | --- | --- |
| `00` | `0` | Write attempted, not verified / failed |
| `01` | `1` | Write completed and read-back verified |
| `10` | `2` | Write blocked (write-enable disabled) |
| `11` | `3` | EEPROM already contained the value; verified match, write skipped |

Before any EEPROM write request, `tv_flag1` starts as **`0x3F`**, meaning no write request has been made. In that initial state, decoded address `63` is only a sentinel, not an application-write address. Later writes replace the status; `verifyROM()` does not retroactively validate older writes.

## 5. FLAG2 — PB3 read result

FLAG2 is the slave's sampled value, obtained with `0x30 0x22` followed by fetch:

| FLAG0 state | Returned FLAG2 |
| --- | --- |
| GPIO disabled | `0` |
| GPIO enabled, ADC disabled | Digital PB3 value `0` or `1` |
| GPIO enabled, ADC enabled, sleep disabled | Eight-bit ADC3 sample `0`–`255` (`ADCH` from left-adjusted conversion, VCC reference) |

The master `digitalRead()` tests only the sample's least significant bit. Use it in the digital GPIO configuration. The master `analogRead()` returns the entire FLAG2 byte.

## 6. Slave SPI diagnostics

A raw diagnostics read is possible with `0x30 0x23` and fetch. The slave has two relevant diagnostic bits:

| Mask | Meaning |
| --- | --- |
| `0x04` | Bad/incomplete/stray frame, invalid fetch, or skipped pending read |
| `0x08` | A CS-HIGH state was observed while the slave was finishing busy processing, so selection was ignored until rearmed |

`TinyVaultSlave::diagnostics()` reads these bits locally; `TinyVaultSlave::clearDiagnostics()` clears them. Ordinary user commands do not automatically clear the diagnostics.

## 7. Reference-master timing

The actual `TinyVault.h` defines:

```cpp
HALF_US         = 100;   // microseconds before rising and during SCK HIGH
SELECT_SETUP_US = 1000;  // microseconds after CS goes HIGH
ORDINARY_US     = 1000;  // microseconds after non-EEPROM operations
ROM_MS          = 20;    // milliseconds after operations marked EEPROM-related
```

The master lowers CS after a 100 µs delay and adds another 100 µs delay before its ordinary/EEPROM recovery delay. EEPROM read requests, EEPROM writes, and FLAG0 writes are marked as EEPROM-related in the implementation. The **read-fetch** portion itself uses ordinary recovery timing. Digital I/O function call overhead affects actual SCK frequency; these constants should not be converted to a guaranteed throughput figure.

When designing a different master, preserve **idle-LOW SCK**, **CS active-HIGH**, required setup time, complete 16-bit/8-bit frame boundaries, and adequate gap for slave EEPROM/ADC processing. Do not assert CS while the slave is still busy with the previous command.

## 8. Storage and functional boundaries

- SRAM `0`–`31` is `.noinit` and not durable across power loss.
- EEPROM `0`–`62` is user storage; address `63` holds control metadata.
- Read requests and fetches are **two separate CS selections**.
- This protocol includes simple framing checks and write read-back, but **does not implement cryptographic identity/authentication**. The signature bits are merely a nonzero local flag.
- The slave supports an ATtiny13(A) build profile at **1.2 MHz**; it is not a generic hardware SPI slave.
