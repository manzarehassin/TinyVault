# TinyVault — ATtiny13(A) Slave Guide

[← README](../README.md) · [Master guide](MASTER.md) · [Protocol reference](PROTOCOL.md)

**Component:** `TinyVaultSlave.h` · **Class:** `TinyVaultSlave` · **Target:** ATtiny13/ATtiny13A at `1.2 MHz`  
**Author:** Manzar E Hassin · [mnz247@hotmail.com](mailto:mnz247@hotmail.com)

The slave is a purpose-built **pin-change interrupt (PCINT0) software SPI implementation** for the tiny resources of the ATtiny13(A). Its inner clocking and byte shifts are in inline AVR assembly within `ISR(PCINT0_vect)`. The Arduino foreground `loop()` does **not** perform SPI polling, bit-shifting, or command dispatch.

## 1. Physical pins

| ATtiny13(A) port | DIP-8 pin | Function | Direction |
| --- | --- | --- | --- |
| `PB0` | 5 | MOSI | Input from master |
| `PB1` | 6 | SCK, Mode-0 idle LOW | Input from master |
| `PB2` | 7 | MISO | Output only while selected; otherwise high impedance |
| `PB4` | 3 | CS **active HIGH** | Input; **10 kΩ external pull-down** to GND |
| `PB3` | 2 | Auxiliary digital input or ADC3 | Sample input, outside SPI capture logic |
| `GND` | 4 | Ground | Shared with master |
| `VCC` | 8 | Supply | Compatible regulated voltage |

Do not omit the CS pull-down: it holds PB4 LOW when the master is resetting or tri-stated. SCK must already be **LOW** when CS rises. Avoid using PB4 or PB1 for unrelated functions while TinyVault is running. PB3 may be connected to a digital signal or an analog voltage within supply rails.

## 2. Arduino IDE / MicroCore settings

The settings below are transcribed from the provided slave source's build profile. Names and menu placement depend on the installed MicroCore version; choose equivalent items if your IDE displays them differently.

| Setting | Source-specified selection |
| --- | --- |
| Board | **ATtiny13** |
| Override Clock Source | **Internal Oscillator 1.2 MHz** |
| Override Frequency | **1.2 MHz** |
| Processor Speed | **1.2MHz Internal Oscillator** |
| Brown-out Detection Level | Disabled |
| Use Bootloader | **No (ISP Programmer Upload)** |
| Millis, Tone Support | No Millis, No Tone |
| Link Time Optimisation | LTO Enabled |
| Millis Accuracy | Better Or Equal 50% Error (Lowest Code Use) |
| Print Support | Bin, Hex, Dec Supported |
| Serial Support | Half Duplex, Read+Write |
| Processor Version | **ATtiny13a** (or supported ATtiny13 variant) |
| Programmer | DIY ATtiny: Arduino as ISP, or a suitable ISP programmer |

The header explicitly rejects unsupported CPU targets and an `F_CPU` other than **`1200000UL`** at compile time:

```cpp
#if !defined(__AVR_ATtiny13A__) && !defined(__AVR_ATtiny13__)
#error "TinyVaultSlave requires ATtiny13 or ATtiny13A"
#endif
#if F_CPU != 1200000UL
#error "TinyVault validation profile requires 1.2MHz CPU"
#endif
```

Use ISP programming to upload the sketch. Ensure the chip's clock/fuse configuration matches the selected 1.2 MHz build setting; a correct menu selection alone cannot compensate for mismatched actual clock hardware.

## 3. Example sketch and startup

The included example is intentionally minimal and **unchanged**:

```cpp
#include "TinyVaultSlave.h"
void setup(){TinyVaultSlave::begin();}
void loop() {TinyVaultSlave::sleepIfIdle();} // Te enable sleep.
```

`TinyVaultSlave::begin()` does all required SPI setup, including:

1. Disabling interrupts during setup; disabling the watchdog.
2. Setting PB0/PB1/PB2/PB4 as inputs and disabling their pull-ups.
3. Powering down the ADC and Timer0, and disabling the analog comparator initially.
4. Initializing control state and checking reserved EEPROM location `63` for saved configuration metadata.
5. Setting up pin-change detection on **`PCINT4`** (PB4), clearing pending PCINT, and enabling interrupts.

`TinyVaultSlave::sleepIfIdle()` is optional. The provided example calls it continuously to enter sleep if the corresponding control bit is enabled and CS is LOW. With no foreground work, this is a convenient minimal application. In a different application, `loop()` can do other work, but must respect the very limited SRAM/stack and shared peripheral requirements.

Include the slave header from **one compilation translation unit**: it defines storage and the `PCINT0_vect` ISR, rather than merely declaring them.

## 4. Public slave methods

All methods are `static`; no slave object needs to be constructed.

| Method | Return | Role |
| --- | --- | --- |
| `TinyVaultSlave::begin()` | `void` | Initialize hardware, flags, interrupts, and SPI capture |
| `TinyVaultSlave::sleepIfIdle()` | `void` | Optionally enter sleep when enabled and CS is LOW |
| `TinyVaultSlave::diagnostics()` | `uint8_t` | Return stored SPI diagnostic bits |
| `TinyVaultSlave::clearDiagnostics()` | `void` | Clear the diagnostic bits while preserving interrupt state |
| `TinyVaultSlave::flag0()` | `uint8_t` | Inspect FLAG0 control byte locally |
| `TinyVaultSlave::flag1()` | `uint8_t` | Inspect last EEPROM-write status locally |
| `TinyVaultSlave::readRetained(address)` | `uint8_t` | Read retained SRAM `0`–`31`, or `0` if invalid |
| `TinyVaultSlave::dispatch(first, second)` | `void` | Internal command execution entry point; **do not call from `loop()`** |

`dispatch` is public in the header but explicitly intended for ISR-only use after CS falls. The interrupt owns command receipt and dispatch; manually calling it can interfere with the protocol's state.

### Diagnostics

| Diagnostic bit | Hex value | Meaning |
| --- | --- | --- |
| Bit 2, `BADFRAME` | `0x04` | Malformed/stray frame, invalid fetch, or skipped pending response |
| Bit 3, `BUSY_EDGE` | `0x08` | CS observed HIGH at end of busy command processing; that selection is ignored until CS returns LOW |

The slave provides `diagnostics()` and `clearDiagnostics()` for local code. A raw remote protocol read also exists at address `0x23` with opcode `0x30`; the supplied master class `readFlag()` only accepts indices `0`, `1`, and `2`, so it does **not** directly expose this diagnostic address.

## 5. Memory layout and limits

The ATtiny13(A) has only **64 bytes of SRAM**. The header explicitly allocates:

| State | Bytes | Source declaration |
| --- | ---: | --- |
| Retained user SRAM | 32 | `tv_ram[32]` in `.noinit` |
| RX command bytes | 2 | `tv_rx[2]` |
| TX byte | 1 | `tv_tx` |
| FLAG0 | 1 | `tv_flag0` |
| FLAG1 | 1 | `tv_flag1` |
| SPI state | 1 | `tv_state` |
| **Total** | **38** | Remaining **26 bytes** for stack and other data |

**Important:** The remaining 26 bytes are not a guarantee that arbitrary application code will fit. Interrupt prologue/epilogue and nested foreground needs consume stack. Check actual linked SRAM usage and worst-case stack use before extending the slave. The `.noinit` annotation avoids routine initialization of `tv_ram` but does **not** guarantee its contents across power loss.

## 6. EEPROM layout and persistence

The EEPROM address space is 64 bytes:

- **`0x00`–`0x3E` (0–62):** user data, writable only with EEPROM write-enable.
- **`0x3F` (63):** reserved metadata for control state; `readROM(63)` can return the raw metadata byte, but the public master `writeROM(63, ...)` deliberately does not issue a write.

On startup, the slave defaults to `tv_flag0 = 0x88` (SRAM writes enabled, sleep permitted, EEPROM writes disabled) and `tv_flag1 = 0x3F` (no EEPROM write request yet). If it finds metadata at EEPROM address 63 with the expected marker (`metadata & 0xF8 == 0xB0`), it restores **only** the SRAM write-enable (FLAG0 bit 7), EEPROM write-enable (bit 6), and sleep (bit 3) states from that metadata. Otherwise the default remains.

The slave's `writeFlag` operation converts the requested FLAG0 state to metadata, writes/verifies it if it differs, and updates FLAG0 only if EEPROM metadata is successfully verified. PB3 GPIO enable, ADC enable, and the three signature bits are **not restored from metadata**. This reserved byte is not available to application EEPROM writes.

For EEPROM user-data writes, `eeWrite()` checks whether the value already matches. It returns internal result `2` if identical, `1` if written and verified, and `0` if verification fails. The public FLAG1 status codes encode these outcomes and the write-disabled condition as documented in [PROTOCOL.md](PROTOCOL.md#flag1-last-eeprom-write).

## 7. GPIO, ADC3, and power management

PB3 (physical pin 2) is sampled only when a FLAG2 read command is processed, never from the SPI bit-capture hot path.

- **GPIO off (FLAG0 bit 5 = 0):** the sample is `0`.
- **GPIO on, ADC off (bit 5 = 1; bit 4 = 0):** PB3 is configured as a digital input and the sample is `0` or `1`.
- **GPIO and ADC on (bits 5 and 4 = 1, sleep off):** the slave enables ADC3, selects **VCC** reference, left-adjusts the ADC result and returns the upper eight bits (`ADCH`). It powers the ADC back down after sampling.
- **Optional sleep (FLAG0 bit 3):** `sleepIfIdle()` may put the MCU into a sleep mode when CS is LOW. Enabling sleep clears the ADC-enable bit; ADC cannot stay enabled concurrently with the slave sleep flag.

The master convenience methods manage these relationships. The slave's `normalizeFlag0` also rejects ADC-enabled combinations unless GPIO is on and sleep is off. Do not assume `sleepIfIdle()` is called automatically; the application chooses when to call it.

## 8. ISR and framing constraints

1. PCINT is enabled only for **PB4/PCINT4**, the CS line. SCK edges are handled inside the active CS ISR in a tight assembly loop.
2. Selection begins when CS becomes **HIGH** and the slave is armed. SCK must be LOW at selection; MISO is only an output while selected.
3. A complete command uses **16 SCK cycles** (opcode + operand). A read-fetch uses **8 SCK cycles** with opcode `0x0A` and a pending response.
4. Malformed lengths, incomplete SCK pulses, excess clocks, invalid fetches, or replacing an outstanding read response are rejected/flagged instead of silently being accepted.
5. The slave may perform EEPROM or ADC work after CS falls; the master must give it time before starting the next selection. A selection that arrives during busy work may be ignored.
6. The SPI ISR uses selected EEPROM registers as temporary scratch during capture. User code should not race those registers against in-flight protocol processing.

The master implementation already supplies the expected CS setup and ordinary/EEPROM recovery delays. Do not rewrite those assumptions into a conventional active-LOW `SPI.beginTransaction()` transaction without implementing the custom framing.

## 9. Troubleshooting and integration notes

- **Compilation error `requires ATtiny13 or ATtiny13A`:** select the correct MicroCore target.
- **Compilation error `requires 1.2MHz CPU`:** correct the CPU frequency profile and actual clock/fuse configuration.
- **No valid data:** inspect PB4 pull-down, idle-LOW SCK, MOSI/MISO orientation, supply, and shared ground.
- **Bad frames:** inspect CS pulse width, exactly 16 or 8 complete clocks, and spacing after EEPROM/ADC requests.
- **Unexpected data after reset:** `.noinit` SRAM is not a power-off data store; FLAG0 metadata only restores selected bits.
- **Unexpected application crash after adding code:** check the **26-byte SRAM headroom**, stack, and interrupt overhead.
- **Conflicting interrupts or timers:** the library owns `PCINT0_vect`, PB4 pin-change setup, and the documented peripheral registers/low-power behavior; coordinate additional slave code carefully.

For the command bytes and status-bit map, consult the [Protocol reference](PROTOCOL.md).
