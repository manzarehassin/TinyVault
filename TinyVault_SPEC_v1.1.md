# TinyVault

**ATtiny13A battery-backed SRAM / EEPROM vault with an active-high, software SPI slave**

**Document type:** normative firmware, wire-protocol, hardware-integration, and verification specification  
**Specification version:** 1.1-draft (BOD-off policy, initial SPI speed profile, and explicit register/stack budget)  
**Target:** Microchip ATtiny13A, 8-pin package pin numbers used throughout  
**Repository name:** `TinyVault`  
**Implementation status:** specification only; no implementation or test results are claimed

> **Non-negotiable pin rule:** **Physical pin 2 (PB3 / ADC3) is reserved solely for optional GPIO digital/analog input. It is never a MOSI, MISO, SCK, CS, SPI-ready, SPI-status, SPI-timing, SPI-interrupt, or SPI-debug pin.** The SPI engine must not configure, toggle, or inspect PB3 for any SPI purpose. PB3 is accessed only by the GPIO/ADC feature. The pin-change interrupt mask for SPI enables **PCINT4 only**.

## 1. Scope, design principles, and conformance

TinyVault exposes 32 bytes of battery-retained **volatile** SRAM, 63 bytes of user EEPROM, three logical flag addresses backed by two physical flag bytes, one optional GPIO/ADC input, and one protected metadata byte. It uses a blocking assembly software-SPI slave because the ATtiny13A has no normal application SPI slave peripheral. The SPI master bit-bangs the clock and therefore controls all timing.

Requirements use **MUST**, **MUST NOT**, **SHOULD**, and **MAY** in their ordinary standards-document sense. This document specifies a design suitable for implementation and production qualification. A specification alone cannot establish that a future firmware build is bug-free, within 1 KB of Flash, or electrically safe; the release gates in §20 are mandatory before making such claims.

Guiding decisions:

1. Keep the externally visible command format in the updated `logic.txt`, including **active-high CS**, one-byte FETCH, and two-transaction reads.
2. Resolve ambiguity in favor of deterministic behavior and conservative error handling.
3. Use CPU registers for time-critical SPI shifts; borrow **EEDR and EEARL** for SPI-only scratch/status when EEPROM has no ownership.
4. **Never allow a SPI transaction during EEPROM read/write/verification/metadata processing.** Early CS assertions are ignored, not queued.
5. Never let software SPI touch physical pin 2, including for auxiliary status signaling.
6. Preserve the 32-byte user array through ordinary sleep and intentionally avoid automatic startup clearing, while explicitly distinguishing retention from integrity verification.
7. Avoid unnecessary EEPROM writes, extra SRAM allocations, unnecessary wake time, and blocking Arduino library features that expand Flash usage.

### 1.1 Derived from the original inputs versus deliberate refinements

**Preserved requirements:** fixed pinout; 32-byte SRAM; 63 user EEPROM bytes plus protected address 63; FLAG0/FLAG1 plus virtual FLAG2; RAM/EEPROM read/write opcodes; `0x0A` FETCH; active-high CS; RAM/EEPROM work after CS falls; RAM/EEPROM write-enable bits; compare-before-write and verification; sleep policy; internal metadata updates; shared MOSI/SCK/MISO; and use of EEPROM I/O registers for SPI scratch while safe.

**Explicitly resolved here:** exact SPI framing and timing responsibilities; second-byte MISO acknowledgment; response-pending behavior; busy-frame rejection; EEDR/EEARL ownership; invalid command handling; EEPROM status meanings; validity encoding for erased metadata; reset handling for retained SRAM; effective FLAG0 dependency rules; atomicity expectations; error recovery; and minimum production acceptance tests.

**Not added:** additional physical pins, a hardware READY line, new public SPI opcodes, background EEPROM jobs, GPIO outputs, or an automatic claim that three signature bits prove SRAM integrity.

## 2. Hardware platform and immutable pin ownership

| Physical pin | Port / alternate function | TinyVault assignment | SPI engine allowed access? |
|---|---|---|---|
| 1 | PB5 / RESET | RESET / in-system programming | **No**; do not repurpose |
| **2** | **PB3 / ADC3 / PCINT3** | **Optional GPIO/ADC input only** | **No, never** |
| 3 | PB4 / PCINT4 | CS, **active HIGH**, external 10 kΩ pulldown | Yes: CS sense and wake |
| 4 | GND | Ground | Not an I/O pin |
| 5 | PB0 / ISP MOSI | Software MOSI input | Yes |
| 6 | PB1 / ISP MISO | **Software SCK input** | Yes |
| 7 | PB2 / ISP SCK | **Software MISO**, output only while selected and ready | Yes |
| 8 | VCC | Switched 5 V / CR2032-backed supply | Not an I/O pin |

The application software assignment of PB1/PB2 is **intentionally reversed** relative to the device's ISP SPI labels. This is legal for a GPIO software protocol but matters during ISP programming and PCB design. Provision programming access according to the manufacturer's **actual ISP pin functions**; design isolation so other slaves, the bit-bang master, and the battery network do not interfere with ISP programming.

### 2.1 Absolute PB3 separation rule

- The SPI assembly code MUST use only PB0 (input), PB1 (input), PB2 (transmit direction/data), and PB4 (CS).
- SPI initialization MUST preserve the existing **DDRB3** and **PORTB3** values; it MUST NOT reconfigure PB3 or enable a pull-up on PB3.
- SPI MUST NOT enable PCINT3, sample PB3 as an SPI control input, drive PB3 as an SPI output, or assign it to a debug/READY signal.
- A full-port `PINB` hardware read inherently returns several pin bits; SPI may use that register to sample PB0/PB1/PB4, but MUST ignore **bit 3 completely**. Such a port snapshot is not permission to treat PB3 as an SPI signal.
- Only the GPIO/ADC module may configure or sample PB3. For the specified feature it is always an **input**, never an output.
- An external-clock fuse configuration that appropriates PB3/CLKI conflicts with using PB3 for GPIO/ADC. **Use an internal clock source** in the supported configuration.
- The SPI interrupt's PCMSK selection MUST be exclusively PCINT4; changes in PB3 MUST NOT trigger SPI execution.

### 2.2 Electrical integration requirements

- Keep the external **10 kΩ CS pulldown** so the device is deselected while the master is unpowered or its output is high-impedance.
- PB2/MISO MUST be high-impedance whenever CS is low, during EEPROM-exclusive work, reset, and rearm waiting. No internal pull-up on PB2 in these states.
- The master MUST never select another shared-bus slave before TinyVault has had time to release MISO after CS falls.
- Shared inputs, output voltage levels, and power sequencing MUST satisfy the MCU's absolute maximum I/O ratings across the 5 V-to-coin-cell transition. In particular, do not allow unpowered-master or recovering-master signals to back-power TinyVault through I/O protection structures.
- The CR2032 MUST NOT be charged by the main 5 V rail. Use a properly engineered isolation/power-OR arrangement with adequate hold-up, low leakage, and tested transition behavior. Directly shorting the two supply sources together is prohibited.
- A coin cell's pulse-current capability, temperature performance, aging, and voltage drop under active CPU load MUST be included in the power budget.

## 3. Resources and memory budget

The ATtiny13A provides **1,024 bytes Flash, 64 bytes SRAM, 64 bytes EEPROM, and 32 CPU working registers**. The working registers do **not** consume the 64-byte SRAM area.

### 3.1 Required logical memory

| Resource | Size | Location / notes |
|---|---:|---|
| User SRAM | 32 bytes | Dedicated retained array; addresses 0–31 |
| FLAG0 | 1 byte | Writable control and 3-bit user signature |
| FLAG1 | 1 byte | Read-only EEPROM write status |
| Virtual FLAG2 | 0 bytes | GPIO result generated on read |
| `SPI_RX0`, `SPI_RX1` | 2 bytes | Last complete incoming frame |
| `SPI_TX` | 1 byte | Pending one-byte reply or idle acknowledgment |
| **Baseline permanent SRAM total** | **37 bytes** | **27 bytes remain before call stack and other variables** |

The sample naked ISR uses **8 bytes of stack** (two return-address bytes plus six saved register bytes). With that ISR alone, only **19 bytes** remain beyond the baseline for application calls, startup activity, interrupt nesting if any, and local storage. The complete firmware MUST be assessed from an actual build and worst-case stack analysis. Any dedicated retained-data validity marker, CRC, extra status byte, or compiler-generated local variable increases this budget.

### 3.2 CPU-register, EEPROM-register, and local-variable budget

The 32 general-purpose CPU registers are **not SRAM**. The uploaded sample SPI ISR touches **five** general-purpose registers, `r16` through `r20`, saving and restoring all five, plus `SREG`. Thus its interrupt invocation consumes **8 SRAM stack bytes**: 2 bytes for the return PC and 6 pushed bytes (five CPU registers plus one saved SREG).

| Register resource | Currently in sample | TinyVault design budget | SRAM allocated? |
|---|---:|---:|---|
| General-purpose registers `r16`–`r20` | 5 bytes | 5 bytes | No (but saved on stack during ISR) |
| `EEDR` | 8 usable bits | 8 usable bits | No |
| `EEARL` | Not used in sample | 6 usable bits reserved for scratch/checkpoint | No |
| **SPI scratch/register capacity** | **48 bits (6 bytes)** | **54 bits (6 bytes + 6 bits)** | **0 static SRAM bytes** |
| ISR stack footprint | 8 bytes | 8 bytes if save set remains unchanged | Yes, while ISR runs |

The six EEARL bits are **proposed for final firmware**; merely reserving them does not imply the provided template already uses them. SREG is saved/restored, **not available as an additional general-purpose scratch byte**. If the final assembly uses extra CPU registers, the saving overhead may increase.

The amount of memory actually usable for *local variables* is **not a fixed 19 bytes**. From the 64-byte SRAM: 64 − 37 permanent bytes = **27 bytes before stack**; 27 − 8 ISR overhead = **19 bytes for all additional SRAM allocations, foreground call stack, local variables, and safety margin**. A hypothetical 8-byte foreground call stack (excluding locals) and a 4-byte guard leaves **7 bytes for locals/other SRAM**. A 12-byte foreground call stack and the same guard leaves **3 bytes**. These are **planning examples**, not validated guarantees; real C/C++ stack depth must be extracted from the finished build and exercised at worst case. Register-allocated locals may consume no SRAM, whereas spilled locals, return addresses, and compiler call frames do.

### 3.3 Persistent versus transient state

- The 32-byte user SRAM is the only guaranteed **dedicated data array**; it is not a 32-byte application stack or RX area.
- `SPI_RX0/1` and `SPI_TX` are SRAM and survive Power-down unless explicitly overwritten.
- CPU registers hold the hot SPI shift chain and sample/counter values; the SPI ISR saves/restores all modified registers and SREG.
- `EEDR` and `EEARL` may store SPI scratch/status **only when the EEPROM controller is idle and SPI owns them**. Contents are NOT generally preserved across EEPROM work or MCU reset.
- Avoid dynamic allocation, virtual C++ dispatch, recursion, large local arrays, and long Arduino call chains. A class façade is acceptable only if its linked Flash/RAM/stack cost is verified.

## 4. SPI electrical and byte-level contract

| Property | Normative value |
|---|---|
| Chip select | **Active HIGH**, PB4 / physical pin 3 |
| SPI mode | **Mode 0: CPOL=0, CPHA=0** |
| Bit order | MSB first |
| SCK | Master-driven on PB1 / physical pin 6 |
| MOSI sample | On each SCK **rising edge** |
| MISO update | Before first rising edge, then after falling edges |
| MISO output pin | PB2 / physical pin 7 |
| Standard request | Exactly **16 complete clock cycles** while CS remains high |
| FETCH | Exactly **8 complete clock cycles** while CS remains high |
| Selected idle clock | SCK MUST already be low at CS assertion |
| Unselected condition | CS low; MISO high-impedance; SCK ignored |

### 4.1 Master timing contract

The master controls clock speed but may not use arbitrarily short pulses. Its timing profile MUST define, **from instrumented worst-case firmware and hardware testing**, at least:

| Symbol | Definition | Requirement |
|---|---|---|
| `T_CS_SETUP` | CS rises until first SCK rising edge | Long enough for Power-down wake, ISR entry, and first MISO-bit preload |
| `T_SCK_HIGH` | SCK high duration | Long enough for rising-edge sampling and transition to fall-wait |
| `T_SCK_LOW` | SCK low duration | Long enough for transmit update and return to rise-wait |
| `T_CS_HOLD` | Final falling SCK edge until CS falls | Long enough to count the final completed bit |
| `T_MISO_RELEASE` | CS falls until another slave drives shared MISO | Long enough for software MISO tri-state |
| `T_REQUEST_PROCESS` | End of ordinary command until next CS assertion | Long enough for post-frame decoding and read preparation |
| `T_EEPROM_BUSY_MAX` | End of EEPROM-affecting command until next permitted CS | Covers controller write, readback verification, status restoration, margin |
| `T_METADATA_BUSY_MAX` | End of FLAG0 write until next permitted CS | Covers possible metadata update and verification, margin |

### 4.1.1 Initial conservative qualification profile (not yet production-certified)

For the unchanged five-register polling-loop SPI *template*, start validation with the **factory-selected calibrated 9.6 MHz internal oscillator and CKDIV8 programmed**, giving **1.2 MHz nominal CPU clock** (do not switch to the 128 kHz oscillator or increase CPU prescaling while the 5 kHz SPI profile is in use). Use an **initial 5 kHz SPI clock**: 100 µs high and 100 µs low, 200 µs total period, nominally 120 CPU cycles per half-cycle. A full 8-clock FETCH takes **1.6 ms** and a full 16-clock request takes **3.2 ms**, excluding CS and processing delays. This is a deliberately conservative starting point, **not a manufacturer-guaranteed software-SPI maximum nor a tested production-safe limit**.

| Master timing parameter | Initial proposed minimum / action | Qualification status |
|---|---|---|
| `T_CS_SETUP` after Power-down wake | ≥ 1 ms | Must be verified for exact wake/ISR/clock configuration |
| `T_SCK_HIGH`, `T_SCK_LOW` | ≥ 100 µs each (5 kHz) | Must be verified at slowest CPU clock and low VCC |
| `T_CS_HOLD` after last SCK falling edge | ≥ 100 µs | Must be verified for full-frame counting |
| `T_MISO_RELEASE` / shared-bus deselect guard | ≥ 100 µs | Must be verified electrically |
| `T_REQUEST_PROCESS` (ordinary read/write) | ≥ 1 ms | Must be verified on compiled application |
| Host pause after EEPROM/metadata-affecting operations | ≥ 20 ms **initial test value** | Not a guaranteed upper busy-time bound; must be measured with margins |
| Host wait after a full reset / power-on | Separately validated; do not equate with sleep wakeup | Default SUT can add 64 ms reset start-up delay plus firmware initialization |

The manufacturer's **3.4 ms EEPROM atomic erase/write figure** is a controller programming characteristic, not a complete TinyVault master turn-around guarantee. The final master timings MUST be documented after tests at the lowest backup VCC, slowest permitted actual CPU clock, worst-case temperature, and all EEPROM/metadata paths. No new CS pulse is valid while EEPROM ownership is active, even if the proposed master pause has expired. The master needs a tested worst-case duration or an alternate ready protocol for a real production guarantee. An eventual 10 kHz SCK mode (50 µs high/low) is a plausible optimization **only after validation**, and must never be inferred from the CPU frequency alone.

### 4.2 Frame and abort rules

1. A transaction begins only on a new CS low-to-high transition **while the SPI engine is armed**.
2. The engine verifies a valid start state: CS high and SCK low before accepting the first edge. High SCK at entry is a framing fault, never silently interpreted as bit 1.
3. A bit is complete only after its rising **and falling** SCK edges have been observed while CS is high.
4. CS falling with **0 complete bits** is a no-op, not a request.
5. CS falling with any bit count other than exactly 8 or 16 is malformed; no application write or pending-reply replacement is allowed.
6. More than 16 rising clock cycles before CS falls invalidates the whole frame. Discard the frame, keep MISO in the selected/controlled state only as needed until CS falls, and do not publish received data.
7. CS falling during any rise-wait, fall-wait, or bit operation aborts immediately. The clock-wait loop MUST always observe CS.
8. On CS falling, stop driving PB2 before any subsequent shared-bus selection; then either publish exactly one complete RX frame or mark an error.
9. A previously unread RX frame MUST NOT be overwritten. If the host violates turn-around time, reject the new frame and flag receive overrun rather than corrupting the previous RX data.
10. All writes occur **after CS falls**, never during the selected byte-shifting window.

## 5. Complete command map

### 5.1 Request acknowledgment rule

In the absence of a pending reply, each of the two MISO bytes of a 16-clock command frame MUST equal **`0xAA`, `0xAA`**, matching `logic.txt`. This deliberately differs from the uploaded template header, whose second MISO byte is `0x00`. That header is a **reference implementation template**, not a wire-compatible final TinyVault engine.

During an expected FETCH frame, MISO's **first and only byte** is the already prepared response. The slave cannot decode the incoming `0x0A` before sending that byte; validation of FETCH happens **after** the frame. Consequently, the response is preauthorized by the protocol state, not by an opcode received earlier in the same byte.

### 5.2 Command table

| Operation | MOSI frame while CS high | MISO for the frame | Commit after CS low |
|---|---|---|---|
| SRAM read | `0x30`, address `0x00–0x1F` | `0xAA`, `0xAA` | Stage SRAM byte in `SPI_TX` |
| FLAG0 read | `0x30`, `0x20` | `0xAA`, `0xAA` | Stage FLAG0 |
| FLAG1 read | `0x30`, `0x21` | `0xAA`, `0xAA` | Stage FLAG1 |
| Virtual FLAG2 read | `0x30`, `0x22` | `0xAA`, `0xAA` | Sample PB3 as configured; stage result |
| EEPROM read | `0x33`, address `0x00–0x3F` | `0xAA`, `0xAA` | Exclusive EEPROM read; stage result |
| SRAM write | `0x80–0x9F`, data | `0xAA`, `0xAA` | Write SRAM if FLAG0.7=1 |
| FLAG0 write | `0xA0`, data | `0xAA`, `0xAA` | Normalize flags; possibly update metadata |
| EEPROM write | `0xC0–0xFE`, data | `0xAA`, `0xAA` | Protected compare/write/verify |
| FETCH | `0x0A` (one byte) | Previously staged `SPI_TX` byte | Consume pending reply |

**Not valid:** `0x30` with address `0x23–0xFF`; `0x33` with values above `0x3F`; RAM-write-like values `0xA1–0xBF`; EEPROM write `0xFF`; unrecognized opcodes; the wrong frame length for a known opcode. These MUST NOT read/write out of bounds or alias a valid location. **Metadata at EEPROM address 63 is readable, but never writable through SPI.**

### 5.3 Required two-transaction read sequence

1. Master ensures the slave is ready, raises CS, and clocks a valid 16-bit read request.
2. MISO returns `0xAA`, `0xAA`; master lowers CS.
3. Firmware processes the request with CS low; a valid result becomes one **pending response**.
4. Master observes the applicable processing/busy interval.
5. Master raises CS, clocks **one byte `0x0A`**, and reads the pending value on MISO at the same time.
6. Master lowers CS; firmware validates the received FETCH, marks the pending reply consumed, and restores `SPI_TX` to idle `0xAA`.

The next transaction after a staged read **MUST be the one-byte FETCH**. No pipelining or multiple outstanding reads are supported. An invalid command while a reply is pending discards the pending reply and performs **no** requested operation; the MISO byte(s) may already have leaked the pending value because transmission began before opcode recognition. The host MUST therefore disregard data from any protocol-violating frame. A second FETCH after the response is consumed is invalid and produces idle `0xAA`, not the old data.

### 5.4 Read response ownership

- One pending read at most; `SPI_TX` holds its data, including legal values `0x00`, `0xAA`, and `0xFF`.
- Pending-valid state MUST be stored separately from the data value; **do not use a sentinel value inside `SPI_TX`**.
- Idle MISO acknowledgment is `0xAA` and the default `SPI_TX` is `0xAA` when no result is pending.
- Any invalid, malformed, overlong, or aborted frame that occurs while a read reply is pending **invalidates that pending reply** after CS becomes low; it must never apply a memory write. The master recovers by issuing a fresh read request. Without a pending reply, such frames leave `SPI_TX` at idle `0xAA`.
- Pending reply state survives Power-down, but not a reset. It must be restored after any EEPROM-exclusive period in which EEDR/EEARL were borrowed.

## 6. EEDR / EEARL register-sharing contract

**This is mandatory:** SPI and EEPROM have **mutually exclusive ownership** of the EEPROM access registers. No EEPROM access is performed from the blocking SPI ISR. No SPI frame is accepted during an EEPROM controller operation, including read, erase/write, verification readback, or internal metadata update.

### 6.1 Proposed six-bit EEARL SPI allocation

EEARL has writable bits **5:0**; bits **7:6** are reserved and read zero. During SPI ownership only:

| EEARL bits | SPI scratch meaning | Notes |
|---|---|---|
| 4:0 | Last checkpointed completed-clock count (0–16) | Optional per-byte/exit checkpoint; not necessarily updated on every bit |
| 5 | Overlength / frame-drop scratch flag | 1 after the 16-clock limit is exceeded |
| 7:6 | Reserved | Must be ignored/left zero |

The live per-bit counter SHOULD remain in a fast CPU register (`r19` in the sample) to avoid repeated I/O-register traffic. EEARL is a **checkpoint and temporary state register**, not a reason to slow every clock edge. Its contents are temporary and lose their SPI meaning when EEPROM takes ownership.

### 6.2 Proposed EEDR SPI allocation

| EEDR bits | SPI meaning while SPI owns EEDR |
|---|---|
| 1:0 | Published unread frame length: 0, 1, or 2 bytes |
| 4 | Pending one-byte reply valid |
| 6 | Malformed frame diagnostic |
| 7 | RX overrun diagnostic |
| 5, 3, 2 | Reserved for future internal use; initialized to zero |

Diagnostic bits 6:7 are **best-effort transport diagnostics**, not user-visible flag registers or durable fault logs. They may be intentionally cleared during EEPROM ownership transitions; they are not required to survive EEPROM access, sleep/reset, or loss of power. Correctness MUST NOT depend on their persistence. The pending-valid indication is different: its meaning **must** be reconstructed correctly after EEPROM operations before SPI is rearmed.

### 6.3 Ownership hand-off: SPI to EEPROM

1. Receive and fully close the SPI frame; MISO goes high-impedance.
2. In a race-safe critical section, disarm acceptance of further CS-triggered SPI work **before** touching EEARL/EEDR for EEPROM purposes.
3. Consume/decode `SPI_RX0/1`, and release the unread-frame marker; copy any still-needed command data to safe CPU registers or a bounded temporary as required by the compiled application.
4. Never rely on EEDR SPI status or EEARL SPI bit-count values after EEPROM begins; capture any necessary information before hand-off.
5. Wait until `EEPE=0` before accessing EEPROM registers and before beginning the new EEPROM operation.
6. Load fresh EEPROM address into EEARL and appropriate data into EEDR; complete the read or write protocol.
7. For writes, keep SPI disarmed until `EEPE=0`, read back as needed, and finish FLAG1 or metadata result processing.
8. Reconstruct SPI status, including correct pending-reply validity and idle TX behavior; restore scratch-register defaults.
9. Rearm **only after confirming CS low** under the race-safe rearm procedure in §8.

A blanket instruction to “clear EEDR after EEPROM work” is insufficient: doing so could destroy the pending reply for an EEPROM READ. The new reply and its valid bit must be established before the next FETCH may begin.

### 6.4 EEPROM owner restrictions

- The EEPROM module alone controls `EECR`, including `EEMPE`, `EEPE`, `EERE`, and programming mode.
- The write-enable timed sequence MUST meet the datasheet's four-CPU-cycle requirement; prevent all interrupt preemption across that short sequence.
- Do not repurpose EEARL while `EEPE=1`; wait for programming completion before returning ownership to SPI.
- No EEPROM-ready ISR, timer ISR, Arduino EEPROM helper, or other background code may borrow these registers without using the same exclusivity policy.
- The sample header's public `available()`/`errors()` functions MUST be treated as invalid while EEPROM owns EEDR. They may be called only in an SPI-owned phase.

## 7. Runtime state machine

| State | CS behavior | MISO | EEPROM registers | Allowed next state |
|---|---|---|---|---|
| `RESET_INIT` | Do not accept frames | High-Z | EEPROM owner for metadata reading | `WAIT_CS_LOW` |
| `WAIT_CS_LOW` | Ignore high CS until it returns low | High-Z | No SPI frame yet | `ARMED_IDLE` |
| `ARMED_IDLE` | New low→high transition selects slave | High-Z until selection | SPI-owned scratch/status | `SELECTED` / `SLEEP` |
| `SELECTED` | Blocking clock polling; CS low aborts/closes | Drives PB2 | SPI-owned only | `FRAME_READY` / `ARMED_IDLE` |
| `FRAME_READY` | CS low; decode/process published bytes | High-Z | SPI-owned until hand-off | `ARMED_IDLE` / `EEPROM_EXCLUSIVE` |
| `EEPROM_EXCLUSIVE` | **All CS activity ignored** | High-Z | EEPROM-owned | `WAIT_CS_LOW` |
| `SLEEP` | PB4 pin-change can wake MCU | High-Z | SPI-owned idle values retained | `SELECTED` / `ARMED_IDLE` |

An EEPROM transaction has priority once ownership has been acquired. A master frame presented during `EEPROM_EXCLUSIVE` is **lost**, never queued or replayed. Its MISO contents are undefined because the slave is high-impedance.

### 7.1 Busy entry and exit are race-sensitive

- Set the logical busy condition **and prevent CS ISR entry atomically** before touching EEDR/EEARL for EEPROM.
- While busy, do not treat a PCINT flag as a valid start request. The pin-change flag can become pending even when ISR service is masked.
- At busy completion, wait for **CS low**. If CS remained high since busy time, do **not** adopt that ongoing high level as a selected transaction.
- Clear/handle stale PCINT flags and re-enable the interrupt using an atomic rearm sequence that checks CS again and cannot discard a real low→high transition occurring during rearm.
- If a low→high edge races the last arm step, ensure the already-high state is serviced as a new frame **only if** it occurred after rearm eligibility; otherwise refuse it until a new low pulse. This race MUST be tested with hardware fault injection.
- The SPI engine MUST NOT begin driving MISO while unarmed or busy, regardless of CS voltage.

## 8. Sleep and wakeup protocol

- After all pending work finishes, the MCU MAY enter **Power-down** only when `FLAG0.3=1`, CS is low, SPI is armed to detect the next valid CS transition, no EEPROM job is active, and the entire receive-frame/postprocessing state has been resolved.
- If `FLAG0.3=0`, stay awake and idle when not selected. This applies even when on CR2032 because the source specification provides no main-power-sense input; battery runtime in this configuration may be short.
- The **only SPI wake source** is PB4/PCINT4. GPIO on physical pin 2 is not an SPI wake source, and PCINT3 remains disabled by the SPI subsystem.
- A race-free sleep entry MUST atomically coordinate interrupt enable, pending PCIF handling, CS-low verification, and sleep execution. A CS rise between “check CS” and “sleep” must neither be lost nor put the device to sleep with CS already high.
- After wakeup, allow the internal clock, interrupt entry, and MISO preload to finish before expecting the first SCK edge. BOD is **fuse-disabled at all times** and must never be activated. Host `T_CS_SETUP` covers the wake/ISR path.
- Disable unneeded peripherals and ADC when idle/sleeping. Disable analog comparator if unused. Use Power-down, not a busily polled idle loop, when sleep is allowed.
- Do not sleep while `EEPE=1`; finish EEPROM verification and register hand-off first.
- BOD MUST remain **permanently disabled by the high-fuse `BODLEVEL[1:0]=11` setting** during startup, active execution, EEPROM processing, and sleep. No runtime BOD toggling is needed. Watchdog settings must be deliberately evaluated for power and reliability; never assume watchdog/BOD defaults are sufficient.

## 9. FLAG0: control and user session signature

**FLAG0 is readable and writable** at `0x30,0x20` and `0xA0,data` respectively.

| Bit | Meaning | Default | Write behavior |
|---|---|---|---|
| 7 | SRAM write enable | 1 | 0 blocks SRAM writes |
| 6 | User EEPROM write enable | 0 | 0 blocks user EEPROM writes; not internal metadata |
| 5 | GPIO feature enable, PB3 input | 0 | 0 disables reads from PB3 |
| 4 | ADC sampling enable | 0 | Allowed only if bit5=1 and bit3=0 |
| 3 | Power-down sleep allowed | 1 | 1 forces bit4=0 |
| 2:0 | User-set three-bit session signature | `000` | May be any value `000`–`111` |

### 9.1 Normalize a FLAG0 write

1. Receive exactly two bytes under opcode `0xA0`.
2. Form the requested byte, then force ADC enable bit4 to **0** if GPIO bit5=0 **or** sleep-allowed bit3=1.
3. Identify whether effective bits 7, 6, or 3 differ from the currently valid metadata representation, or whether metadata is uninitialized/invalid.
4. If a metadata update is required, take EEPROM-exclusive ownership and compare/update/verify metadata first.
5. **Commit the complete effective FLAG0 value only after any required metadata update verifies successfully.** On metadata verification failure, retain the former effective FLAG0 and its control permissions; the master can detect failure by reading FLAG0 and metadata back.
6. Apply ADC/GPIO hardware configuration after the effective FLAG0 value commits; no PB3 change is performed from the SPI engine itself.

This policy prevents RAM-write/EEPROM-write/sleep settings from silently disagreeing with their stored boot configuration when the metadata write fails. All `0xA0` writes require the master to respect `T_METADATA_BUSY_MAX`, because even an apparently trivial change can encounter invalid metadata.

### 9.2 Session signature semantics and honesty

- The signature is **user-controlled** and only three bits wide; it is a **weak session marker**, **not** a checksum or proof of data integrity.
- On **any MCU reset**, set the session signature to `000` and discard pending SPI replies. The master SHOULD use a **nonzero** session value when it wishes to detect a likely reset.
- Through uninterrupted, powered sleep and battery-backup operation **without reset**, preserve the chosen signature with FLAG0.
- A matching three-bit signature cannot show that all 32 SRAM bytes are uncorrupted. For actual validation the master SHOULD read back all 32 bytes and compare with an expected externally stored image or checksum/CRC.
- A reset may conservatively invalidate the session even if some physical SRAM bytes remained intact. This avoids incorrectly presenting reset-affected RAM as verified.

## 10. FLAG1: last user EEPROM write result

**FLAG1 is read-only** at `0x30,0x21`. It reports the most recent **attempted user EEPROM write**; internal metadata writes do not replace it.

| Bits | Meaning |
|---|---|
| 7 | Skip/block status |
| 6 | Verified comparison/write success |
| 5:0 | Last requested user EEPROM address, 0–62; `63` means no request since reset |

| Bit7 | Bit6 | Exact result |
|---|---|---|
| 0 | 0 | Write attempted but readback verification failed |
| 0 | 1 | EEPROM write performed and readback verified |
| 1 | 0 | EEPROM write denied by FLAG0.6; no verification claimed |
| 1 | 1 | EEPROM already contained requested value; physical write skipped and equality verified |

On reset FLAG1 is **`0x3F`**, denoting no valid prior user EEPROM write request. FLAG1 address low six bits change only for a valid EEPROM-write request to address 0–62; malformed frames and reserved `0xFF` do not change it. Status completion precedes sleep or SPI rearm, so the master sees a coherent status byte on the next read.

## 11. Virtual FLAG2: GPIO / ADC read-only endpoint

**Virtual FLAG2** is read at `0x30,0x22`. It consumes **no permanently allocated SRAM byte**: PB3 sampling is performed after the read request's CS falls, and the resulting one-byte value is placed directly into `SPI_TX`. PB3 is **physical pin 2** and MUST never participate in SPI transport or signaling.

| Effective FLAG0 settings | Returned virtual FLAG2 byte |
|---|---|
| GPIO bit5=0 | `0x00` (GPIO feature disabled) |
| GPIO bit5=1; ADC bit4=0 | Digital PB3 read: `0x00` or `0x01`, all higher bits zero |
| GPIO bit5=1; ADC bit4=1; sleep bit3=0 | 8-bit scaled ADC3 conversion, 0–255 |
| Sleep bit3=1 with ADC requested | Invalid combination normalized to ADC off; use digital behavior if GPIO enabled |

GPIO remains configured as **floating input**, with internal pull-up disabled, when enabled. A physically unconnected floating pin has **undefined analog/digital readings**; external circuitry must establish a meaningful level. When GPIO is disabled, PB3 remains a high-impedance input and the ADC is off.

For ADC mode, select **ADC3/PB3**, use **VCC as ADC reference** unless a documented future revision changes it, acquire a completed 10-bit conversion, then scale to eight bits by discarding the two least significant bits. This makes readings ratiometric to VCC; absolute measured voltage will change interpretation as supply switches from 5 V to battery voltage. Respect ADC prescaler, startup/conversion time, and input-source impedance requirements. A single conversion on demand is preferable to continuous background conversion.

No GPIO sampling or ADC conversion takes place inside the SPI ISR; work occurs only with CS low and after frame validation. ADC activity MUST be finished and the result staged before FETCH is allowed.

## 12. User SRAM: 32 bytes

- Address range **0–31** (`0x00–0x1F`). Each SRAM address maps to exactly one physical byte in the dedicated array.
- SRAM read uses `0x30,address`, followed by FETCH.
- SRAM write uses first byte **`0x80 | address`** (only address 0–31) and a second data byte. Execute after CS becomes low.
- If FLAG0.7=0, silently deny modification; there is no per-command write acknowledgment in this protocol. The master can verify by reading back.
- An invalid address MUST NOT wrap, mask into address 0–31, touch flags, or write outside the dedicated array.
- SRAM read/write commands never borrow PB3 or the ADC.
- The 32-byte SRAM array MUST NOT be cleared by ordinary startup initialization. Keep it in a linker-managed no-initialize/retained section and verify startup behavior for the exact toolchain build.
- SRAM is retained only while **VCC remains within a suitable retention range**. The 32-byte contents are not permanent in the event of power loss, severe brown-out, or physical damage; the host must validate before trusting them.

## 13. EEPROM map, access, and endurance

| EEPROM addresses | Meaning | SPI read | SPI write |
|---|---|---|---|
| `0x00–0x3E` (0–62) | 63 user bytes | Yes | Yes, subject to FLAG0.6 |
| `0x3F` (63) | Protected metadata | Yes | **No**; internal firmware only |

EEPROM read: `0x33,address` for the **full** `0x00–0x3F` range, followed by FETCH.  
EEPROM write: first command byte `0xC0 | address` for addresses 0–62, then the data byte. `0xFF` is **always invalid** and may not update metadata.

### 13.1 User EEPROM write algorithm

1. Validate the two-byte frame and address; publish the requested address in FLAG1 low six bits.
2. If FLAG0.6=0, **do not read/verify/write EEPROM**; set FLAG1 result to skip/block=`1`, verified=`0`.
3. Otherwise obtain EEPROM-exclusive ownership, wait for `EEPE=0`, and read the current byte.
4. If the byte already equals requested data, skip the physical write and set FLAG1 to skip=`1`, verified=`1`.
5. Otherwise perform the controller's **atomic erase-and-write mode** with interrupts protected during the required timed write-enable sequence.
6. Wait until the controller clears `EEPE`; perform a readback and compare with the requested value.
7. Publish verified success (0,1) or verified failure (0,0) in FLAG1.
8. Restore SPI status, return MISO to high-impedance, wait for CS low if necessary, and rearm before accepting another frame.

EEPROM operations MUST be entirely serial. Do not use arbitrary fixed delays as a substitute for polling the controller's completion flag; the master still needs its independently specified maximum wait before trying a later transaction. There is no automatic retry of a failed EEPROM write in version 1.0, to avoid repeated wear and complicated failure semantics.

### 13.2 Endurance and power-loss requirements

- Compare-before-write is mandatory to reduce wear.
- Keep the required supply voltage stable throughout EEPROM programming; a nominal coin-cell voltage does not guarantee correct high-current or transient behavior.
- Interrupting power during a byte write can produce an old, new, or unusable stored result depending on supply conditions; a **single metadata byte is not a redundant transactional journal**.
- For a hard guarantee against arbitrarily interrupted updates, the protocol and storage allocation would have to change (e.g., multiple records with sequence/checksum), which this version does not do.

## 14. EEPROM metadata: address 63

Metadata stores only these three persistent configuration controls:

| Metadata bit | Source FLAG0 bit | Meaning |
|---|---|---|
| 0 | FLAG0.3 | Sleep allowed |
| 1 | FLAG0.7 | SRAM write enabled |
| 2 | FLAG0.6 | User EEPROM write enabled |
| 7:3 | Fixed identification pattern | Detect erased or many corrupt metadata values |

**Normative encoding:** Metadata high five bits (7:3) MUST equal binary **`10110`**, i.e. metadata matching mask `0xF8` equals `0xB0`. Lower three bits contain the controls above.

- **Factory-safe effective FLAG0 default:** `0x88` (RAM write on; EEPROM write off; GPIO/ADC off; sleep on; signature zero).
- **Default valid metadata:** **`0xB3`** (upper marker `0xB0`; lower controls `0b011`).
- An erased EEPROM byte (`0xFF`) fails validation and **MUST NOT** enable EEPROM writes accidentally.

### 14.1 Startup metadata rules

1. Do not accept SPI while startup metadata is being read and validated.
2. If the upper marker matches, restore FLAG0 bits 3, 7, and 6 from metadata. Other runtime FLAG0 bits initialize to their defaults; signature always resets to `000`.
3. If the marker is invalid (including erased `0xFF`), apply safe defaults (`FLAG0=0x88`), mark metadata as needing repair, and **do not assume the existing EEPROM byte is valid**.
4. Invalid metadata is not automatically rewritten during uncertain backup-only startup. A later explicit FLAG0 write when power is demonstrably adequate may repair it, even when the requested control bits equal safe defaults.
5. Read of EEPROM address `0x3F` exposes the actual stored metadata byte, **not** a fabricated default. This lets the master detect invalid/noncanonical metadata.

A five-bit marker catches an erased byte and many corruption patterns but cannot detect every changed lower control bit. The electrical design must prevent writes under undervoltage; full torn-write recovery is outside this fixed one-byte format.

### 14.2 Internal metadata update rules

- Metadata updates are allowed regardless of user EEPROM write-enable bit FLAG0.6.
- Update only if the new encoded metadata differs from the actual EEPROM content, or if invalid metadata must be repaired.
- Perform the same compare/atomic write/busy-wait/readback process as user EEPROM programming.
- The **full effective FLAG0 write commits only after metadata is verified**, as defined in §9.1; otherwise leave FLAG0 unchanged.
- Internal metadata operations do **not** overwrite FLAG1's user EEPROM status.
- Ignore all master SPI transactions for the entire metadata operation, and rearm only after CS has returned low.

## 15. Reset, brown-out, and backup-retention policy

### 15.1 Required backup behavior

TinyVault's intended backup state is **continuous VCC through transition from regulated 5 V to CR2032**. When the main host is down, external CS pulldown keeps PB4 low, and the device stays in Power-down if FLAG0.3 allows it. SRAM contents are retained only if supply and temperature remain within the device's allowed operating/retention limits.

A power transfer causing reset is not equivalent to ordinary sleep. Even where physical SRAM cells happen to retain contents, normal AVR C/C++ startup could overwrite variables in `.bss`; therefore the dedicated user array MUST be placed outside automatic clearing and its linker/map/startup behavior must be verified.

### 15.2 Reset initialization contract

On any reset:

1. Tri-state MISO, keep SPI unarmed, and clear any stale pending SPI reply.
2. Leave the **user 32-byte SRAM array untouched** by startup code. Treat it as **untrusted** until validated by the host.
3. Read and validate EEPROM metadata; restore managed control bits or safe defaults.
4. Force FLAG0's GPIO and ADC feature bits off, and signature bits to `000`.
5. Set FLAG1 to `0x3F` (no last write since reset); set idle `SPI_TX=0xAA`; clear RX availability and temporary diagnostics.
6. Configure PB0/PB1/PB4 and PB2 safely without altering PB3 except through the separate GPIO initialization policy.
7. Rearm only from an observed CS-low state; then remain awake or use race-safe sleep according to FLAG0.3.

A nonzero pre-outage host-selected signature becoming zero provides a conservative indication of reset. It does **not** distinguish arbitrary SRAM data corruption, which must be detected by host validation.

### 15.3 Mandatory BOD-off and clock constraints

- **BOD MUST be OFF at all times**, through startup, foreground execution, interrupt-driven SPI, EEPROM reads/writes, backup operation, and Power-down. Set the ATtiny13A **high-fuse `BODLEVEL[1:0]=11`**, i.e. **both bits unprogrammed**. Confirm by reading fuses back on programmed hardware. The whole high fuse byte is `0xFF` only if its other six bits retain their unprogrammed values; do not blindly change unrelated fuse bits. There must be no software path that enables BOD.
- **BOD-off removes automatic undervoltage reset protection.** The device may execute incorrectly, write bad EEPROM data, or reset unpredictably if VCC sags. The backup circuit MUST maintain acceptable VCC through power transfer, must never charge CR2032 from the 5 V rail, and must prevent off-rail SPI signals from backfeeding. The system MUST test EEPROM safety during loss of primary power; disabling BOD is an accepted risk, not proof of data integrity.
- Initial clock profile: factory internal 9.6 MHz RC oscillator with CKDIV8 programmed, **1.2 MHz nominal CPU clock**, with **5 kHz SPI master bit-bang** as an unverified conservative starting profile. Use no 128 kHz CPU source or additional prescale during the SPI mode unless all host timings are rederived and qualified. The actual RC clock varies with voltage, temperature, and calibration.
- Maintain SCK and host-turnaround timing valid for the **slowest supported real CPU clock**; validate using the §4.1.1 initial profile and release gates. Do not claim a fixed production-safe maximum SPI speed until the exact assembled ISR and board are measured.
- Verify restart and low-voltage behavior experimentally; a CR2032 nominally rated at 3 V can sag during active or EEPROM-write current demand.
- FLAG0.3=0 can keep the CPU awake for the entire outage. The host/operator accepts this backup-current trade-off unless a future hardware revision adds independent power-source detection.

## 16. Host behavior and recovery

The master MUST:

1. Idle SCK low and CS low; never use physical pin 2 as a shared SPI control.
2. Respect the configured setup, pulse, hold, MISO release, and processing times.
3. After a valid read request, issue FETCH as **the immediately next** selected frame, not an unrelated command.
4. After EEPROM- or metadata-affecting requests, wait longer than the qualified worst-case busy interval before raising CS again.
5. Treat any CS pulse occurring during EEPROM busy as **not accepted**, even if the EEPROM finishes while CS is still high. To recover, drive CS low, wait for rearm, and begin a fresh transaction.
6. Validate reads/writes by an explicit subsequent readback when data integrity matters. `0xAA` is a filler/acknowledgment byte, **not proof that a write was accepted or completed**.
7. On unexpected MISO, timing violations, or suspected timeout: release CS, return SCK low, wait long enough for possible ongoing EEPROM work, then retry from a fresh read/write request—not midframe.
8. Avoid driving shared signals into TinyVault when its VCC is below the master's I/O level unless isolation/translation guarantees electrical safety.
9. On restoration from backup, verify nonzero expected session signature and preferably all 32 bytes against an externally held checksum/image before trusting RAM.

No public READY pin or busy-status SPI response exists in this revision. Therefore, a fixed qualified host wait plus readback/retry is an essential part of the interface contract. **MISO is high-impedance during busy; whatever voltage the master happens to sample on the shared line is not a TinyVault response.**

## 17. Source-header integration requirements

The uploaded `Tiny13SoftSPISlave_PB4.h` is an **assembly transport template only**. It must not be represented as already implementing this specification.

**Features to preserve from the template:** naked PCINT4-initiated blocking receive; PB0 MOSI, PB1 SCK, PB2 MISO; direct instruction-level pin handling; two CPU-register RX shift chain; CS checks in clock wait loops; 8-/16-bit length handling; MISO high-impedance on CS low; save/restore of touched CPU registers and SREG; receive overrun protection; one RX0/RX1 and one TX byte.

**Required integration changes (design-level, no code in this document):**

- Enforce starting SCK low and complete master timing contract.
- Transmit `0xAA` on **both** bytes of two-byte requests, not `0xAA` followed by `0x00`.
- Implement pending reply state and one-byte FETCH semantics, including its speculative first-byte transmission.
- Define and implement exact invalid-frame/extra-bit/CS-abort behavior.
- Integrate strict SPI-versus-EEPROM exclusion and race-safe rearm, while borrowing EEDR/EEARL only under their legal ownership.
- Reconstruct EEDR's pending-valid state after EEPROM read, not just zero it.
- Use EEARL's six bits for SPI checkpoint/scratch status, without sacrificing the fast CPU-register hot loop.
- Add complete opcode dispatch, storage/flags/ADC/metadata, reset-safe SRAM initialization, and sleep handling outside the SPI shifting ISR.
- Ensure `begin()` does not unexpectedly violate the application's global interrupt policy; only one translation unit may define the ISR and static data.
- Preserve PB3's direction/pull-up and ignore its value during all SPI processing. GPIO/ADC code is the **only** owner of PB3.

## 18. Resource, robustness, and quality constraints

- **Flash:** final linked firmware including startup, interrupt vector, compiler runtime, EEPROM, ADC, power management, and C++ wrappers MUST fit within 1,024 bytes. No estimate or feature promise replaces an actual linker-map report.
- **SRAM:** static allocation plus maximum stack MUST remain within the ATtiny13A's 64 bytes with adequate verified headroom; account for the 8-byte ISR overhead and any helper calls.
- **Interrupts:** no nested ISR dependency. SPI is fully blocking while CS remains high; any other enabled interrupt must be assessed as a potential timing disruptor or explicitly disabled.
- **EEPROM:** one operation at a time. Timed write-enable sequence protected. No EEPROM owner overlap with SPI; no premature sleep.
- **Shared MISO:** no bus contention with other selected slaves; test with oscilloscope/logic analyzer.
- **Data integrity:** distinguish completed command, write verification, SRAM-session marker, and independently validated RAM contents. Do not conflate them.
- **Fault containment:** never access outside RAM[0..31] or EEPROM[0..63]; reject reserved opcodes, never write protected metadata from user commands.
- **Power:** define minimum backup voltage, backup duration, leakage path budget, and usable environmental range for the actual PCB and CR2032.

## 19. Normative test vectors

These are behavioral examples. For 16-clock requests the master sees `AA AA` on MISO; for FETCH it simultaneously sends `0A` and reads the queued value.

| Test | Master operation | Required result |
|---|---|---|
| Default RAM write | Send `85 C7`, then read `30 05`, then FETCH `0A` | Reads back `C7` when RAM writes enabled |
| RAM write disabled | Write FLAG0 with bit7=0, then try `85 5A` | RAM address 5 unchanged |
| FLAG0 read | Send `30 20`, then `0A` | Returns effective normalized FLAG0 |
| FLAG1 read | Send `30 21`, then `0A` | Returns last user EEPROM write status |
| Virtual FLAG2 with GPIO off | Send `30 22`, then `0A` | Returns `00`; PB3 never used as SPI |
| Default metadata | Send `33 3F`, then `0A` | Returns `B3` if metadata properly initialized |
| Erased metadata | Boot with EEPROM[63] = `FF` | Safe FLAG0 controls, EEPROM writes disabled; metadata still reports actual `FF` until repaired |
| Enable EEPROM writing | Send `A0 C8` | Effective RAM/write/sleep permissions updated only after `B7` metadata write verifies |
| User EEPROM write | With bit6 enabled, send `C7 5A`, then read `33 07`, FETCH | `5A`; FLAG1 shows performed-and-verified or matching-skip success |
| Protected EEPROM address | Send `FF 5A` | Rejected; EEPROM[63] unchanged |
| RAM opcode overlap | Send `A0 88` | Updates FLAG0, never attempts RAM address 32 |
| Invalid RAM-like opcode | Send `A1 34` | Rejected, no memory change |
| Two-byte ACK | Any valid 16-clock request with no pending reply | MISO exactly `AA AA` |
| FETCH without reply | Send one-byte `0A` | MISO idle `AA`; command not treated as a valid read completion |
| Early CS drop | Send fewer than eight SCK cycles, lower CS | No completed frame and no write |
| Extra clocks | Send 17 or more bits in one selected frame | Drop entire frame; no side effects |
| Busy CS pulse | Pulse CS high during EEPROM programming | MISO high-Z, no frame accepted or replayed |
| CS kept high across busy completion | Raise CS during EEPROM busy and keep high | Still ignored; new low followed by **new high** required |
| PB3 isolation | Toggle/change PB3 while CS low and high | Never starts SPI, alters SPI timing, or writes any SPI state |

`85 C7`, for example, means first byte `0x85` then second byte `0xC7`; it is not source code.

## 20. Release qualification checklist

A TinyVault firmware build is **not production-ready** until all of the following are demonstrated and recorded:

### 20.1 Firmware and protocol

- [ ] Exact AVR device, core/toolchain versions, linker map, Flash utilization, static SRAM, and worst-case stack margin are recorded.
- [ ] SPI mode-0 bitstream passes all byte values (`0x00`, `0x01`, `0x55`, `0x80`, `0xAA`, `0xFF`), both frame lengths, and back-to-back legal commands.
- [ ] Bus timing has measured safe bounds at the slowest clock and minimum backup voltage; values are published for master implementers.
- [ ] No false bit is accepted if SCK was high before CS rose.
- [ ] Partial, overlong, noise-induced, malformed, and overrun frames cause no unintended writes.
- [ ] Every CS-low exit releases MISO before the next slave can drive it.
- [ ] FETCH works for all 256 reply-byte values, especially `0xAA` and `0x00`, with correct pending-state handling.
- [ ] The ISR plus runtime code never reads PB3 as an SPI decision, modifies DDRB3/PORTB3 for SPI, or enables PCINT3 for SPI.
- [ ] EEPROM ownership blocks all SPI entry and `EEDR`/`EEARL` collision cases, including boundary/race timing.
- [ ] All 63 user EEPROM addresses and protected metadata are verified with write protection enabled/disabled.
- [ ] FLAG1 values distinguish write success, matching-skip, write-denied, and write-verify failure.
- [ ] FLAG0 normalization, atomic metadata-update policy, and invalid-metadata defaults pass cold-boot tests.
- [ ] Sleep entry and exit do not lose CS wake events; no sleeping with EEPROM busy.

### 20.2 Hardware and endurance

- [ ] Main-to-CR2032 and CR2032-to-main transitions preserve VCC within qualified limits and produce no back-power or battery charging.
- [ ] No CS/MOSI/SCK/MISO leakage violates I/O ratings when either host or slave is unpowered.
- [ ] BOD is read back as permanently disabled (`BODLEVEL[1:0]=11`), and low-battery behavior, wake-start times, and full-temperature range are measured.
- [ ] Power-down current is measured on the complete PCB (including pulldown, regulators, isolation circuitry, and external GPIO load), not merely estimated from MCU tables.
- [ ] Brown-out/reset, reset during EEPROM operations, and corrupt/erased metadata result in defined safe behavior.
- [ ] EEPROM write endurance and metadata wear under realistic traffic are estimated and subjected to suitable stress testing.
- [ ] Host retention-validation procedure verifies actual SRAM bytes, rather than relying solely on the three-bit signature.
- [ ] ISP programming still works with the unusual software SCK/MISO assignments and shared bus hardware.

### 20.3 Documentation gate

- [ ] Final host timing values, fuse settings, voltage limits, battery circuit, supported operating conditions, observed limits, and known issues are published.
- [ ] The implementation is tested with the intended master and with at least one other slave connected to the shared SPI bus.
- [ ] All changes affecting opcode ranges, ACK bytes, pin assignments, or busy behavior increment the wire-protocol revision and update host documentation.

## 21. Reference hardware documentation

The verified device-specific facts in this specification come from these Microchip sources:

1. [ATtiny13A product page](https://www.microchip.com/en-us/product/ATtiny13A) — Flash, SRAM, EEPROM, ADC, supply range, and hardware peripheral inventory.
2. [ATtiny13A complete datasheet, DS40002307A](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/DataSheets/ATtiny13A-Data-Sheet-DS40002307A.pdf) — pin configuration; EEPROM register descriptions and write protocol (sections 5.3–5.5); Power-down, BOD, and low-power modes (section 7); pin-change interrupts (section 9); GPIO/ADC (sections 10 and 14); electrical characteristics and fuse settings.

The wire protocol, unusual software pin assignment, battery-backed use case, and feature flags are **TinyVault project requirements**, not standard hardware-SPI features defined by Microchip. The extra safeguards and precise behavior defined above are **normative design decisions** for this repository.

---

**TinyVault 1.1-draft summary:** active-high PB4 CS, PB0 MOSI, PB1 software SCK, PB2 software MISO, and **PB3/physical pin 2 exclusively reserved for GPIO/ADC**; two-byte requests plus one-byte FETCH; no SPI acceptance during any EEPROM job; EEARL's six low bits and EEDR borrowed only during SPI-owned phases; 32-byte retained SRAM; 63-byte user EEPROM; two real plus one virtual flag endpoints; safe metadata defaults; and testable reset, sleep, framing, and recovery behavior.
