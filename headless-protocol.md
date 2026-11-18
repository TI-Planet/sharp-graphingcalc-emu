# Headless control protocol v1

`el9950-headless ROM.bin --control` is a long-lived, windowless emulator process for agents, test harnesses and debugger clients. It has no SDL or Dear ImGui dependency.

## Transport and framing

- stdin: UTF-8 NDJSON, exactly one command object per line;
- stdout: UTF-8 NDJSON, one ready event followed by exactly one response per accepted input line;
- stderr: fatal startup/ROM errors only;
- every stdout line is flushed immediately;
- unknown commands and malformed fields return `ok:false` without terminating the process;
- EOF cleanly terminates the process; `quit` additionally returns an acknowledgement;
- protocol version is reported by the initial ready event.

The optional string `id` is copied to the response. A controller should use unique IDs when it can have multiple outstanding commands. Commands execute serially, so a response also establishes that all prior changes are visible.

```json
{"event":"ready","protocol":"el9950-headless","version":1,"rom_sha256":"...","known_1_20":true}
{"id":"request-1","command":"status"}
{"ok":true,"command":"status","id":"request-1","total_cycles":0,"pc":16772253,"sr":63488,"halted":false,"reset_contact_open":false,"pressed_mask":0,"framebuffer_sha256":"..."}
```

All addresses and counters are unsigned decimal JSON integers. Binary payloads use standard padded base64. Inputs intentionally use a small flat JSON subset: command fields, unsigned integers, booleans, strings and arrays of strings.

## Commands

### `status`

Returns cycle count, CPU PC/SR/HALT state, reset-contact state, both battery inputs, detected model/version, keyboard side, pressed-key bitmask and framebuffer SHA-256.

Status responses also contain `machine` (`el99xx` or `el92xx`), `cpu`,
`address_bits`, `width`, `height`, `experimental`, and a named `registers`
object. `sr` is the TLCS status register or the SC62015 C/Z flags.
`clock_hz` describes the oscillator; `cycle_hz` describes scheduler units.
SC62015 budgets count execution states (provisionally 768,000/s at 2.304 MHz).
These are additive protocol-v1 fields; the executable/protocol name remains
`el9950-headless` for existing clients.

```json
{"id":"1","command":"status"}
```

### `run`

Runs up to `cycles`. With `until_halt:true`, returns as soon as the CPU enters HALT. The response adds `executed`, which can slightly exceed an instruction-level budget in normal mode because the CPU finishes its current instruction.

```json
{"id":"2","command":"run","cycles":20000000,"until_halt":true}
```

### `keys`

Applies all releases after all presses in one atomic command. IDs are the stable key IDs in `assets/skin.json`. Held keys remain held across later commands and compartment resets until explicitly released, allowing maintenance chords.

For EL-9200/9300, common named mappings currently include `n0`–`n9`, `decimal`,
`negative`, `enter`, `plus`, `minus`, `multiply`, `divide`, `lparen`, `rparen`,
`on`, `clear`, `second`, `alpha`, `del`, `backspace`, `sin`, `cos`, `tan`,
`square`, `log`, `ln`, `exp`, `fraction`, `power`, `sto`, `variable`, cursor directions,
`math`, `graph`, `prgm` and `stat`. The IDs `calc`, `setup`, `menu`, `quit`,
`eqtn`, `range`, `zoom` and `sqrt` use raw matrix state, separate from the
common `pressed_mask`. The previous ID `edit` is an alias for `eqtn`.

```json
{"id":"3","command":"keys","press":["second","variable"],"release":[]}
{"id":"4","command":"keys","press":[],"release":["second","variable"]}
```

### `reset`

`open` opens the battery-compartment contact and suspends execution. `close` closes it and performs reset if it was open. `pulse` performs open then close in one command while preserving currently held keys.

```json
{"id":"5","command":"reset","action":"pulse"}
```

### `matrix` / `internal.read` / `internal.write` (SC62015 debugging)

SC62015 has a separate 256-byte internal RAM/I/O address space; it is not
mapped into external memory. Internal commands access a single byte with
normal port behavior. Raw matrix coordinates use high-active KO strobes
(output 0–15) and KI inputs (0–7). Held raw positions persist in `.el93state`.
These commands return errors on the TLCS backend.

```json
{"command":"matrix","output":6,"input":2,"pressed":true}
{"command":"internal.write","address":240,"value":64}
{"command":"internal.read","address":242}
{"command":"matrix","output":6,"input":2,"pressed":false}
```

The example selects CL (KO6/KI2); the internal read returns `4` while held.
SC62015 I/O trace records use `0x100000 + internal_address` to distinguish
internal port accesses from the 20-bit external bus. This trace encoding
does not create an external memory region. Trace cycles are instruction-level.

### `keyboard`

Selects the physical side on EL-9900, EL-9900G and EL-9900G SII.
The ROM detects the flip on the next calculator key press and may show its
keyboard/mode-change message. EL-9950 and unknown ROMs reject `basic`.
The response includes `model`, `rom_version` and `keyboard_side`; these fields
also appear in `status` and other status-shaped responses. The CLI equivalent
is `--keyboard-side basic` (or `advanced`, the default).

```json
{"id":"keyboard-basic","command":"keyboard","side":"basic"}
```

Key IDs retain their electrical positions across sides: `sin` is **Simp** on
the basic side, while `cos`, `tan`, `log` and `ln` become fraction/decimal
conversion and integer division. Save-state v7 stores the selected side;
v4–v6 snapshots restore Advanced for compatibility.

### `battery`

Sets the saved operating-battery percentage and/or the emulated backup-battery
health input. Both default to full/healthy. The operating-battery low-voltage
input is not yet modeled; changing its percentage does not affect guest behavior.
A reset may be needed for the backup-battery warning checked during boot.

```json
{"id":"battery-full","command":"battery","operating_percent":100,"backup_healthy":true}
{"id":"battery-low","command":"battery","operating_percent":8}
{"id":"backup-flat","command":"battery","backup_healthy":false}
```

### `screenshot`

Returns the live LCD as a complete binary PGM image (`P5`) in `data_base64`.
The response includes `mime_type`, `width` and `height`: 132×64 for EL-99xx,
96×64 for EL-9200/9300. Separate SC62015 LCD indicators are not included.

```json
{"id":"6","command":"screenshot"}
```

### `irq` (debugging)

Asserts, clears, or pulses a raw TLCS-900 input line. This is intended for hardware bring-up, debugger clients and automated tests; normal clients should not need it once the corresponding peripheral is modeled.

The SC62015 backend accepts only external IRQ line `0` and latches the
external-request bit. Board-level input timing remains provisional.

```json
{"id":"irq-probe","command":"irq","line":12,"action":"pulse"}
```

### `state.dump` / `state.load`

`state.dump` returns the complete versioned payload in `data_base64` and reports
`format` as `el99state` or `el93state`. `state.load` verifies magic, version,
ROM SHA-256 and CRC. EL-99xx retains the existing snapshot bytes and older-version
loading support. SC62015 uses its own format and validates the complete payload
before applying it. States from another family or ROM are rejected.

```json
{"id":"7","command":"state.dump"}
{"id":"8","command":"state.load","data_base64":"RUw5OVNUQVQ..."}
```

State dumps can be hundreds of KiB. Controllers must not impose a small line-length limit.

### `memory.read`

Returns at most 65,536 bytes from the selected machine's external address
space as base64 (24-bit TLCS, 20-bit SC62015). Reads are debugger peeks and
do not trigger device side effects. Out-of-range requests return an error.

```json
{"id":"9","command":"memory.read","address":16711680,"length":64}
```

### `memory.write` (debugging)

Writes one byte through the emulated bus, including normal device side
effects. This is intended for hardware bring-up and debugger clients.

```json
{"id":"9w","command":"memory.write","address":34,"value":10}
```

### `disassemble`

Returns up to 1,024 instructions decoded by the selected CPU backend. If
`address` is omitted, disassembly begins at PC; if `count` is omitted, it
defaults to 16. SC62015 pre-bytes are included with their following instruction.

```json
{"id":"10","command":"disassemble","address":16752896,"count":16}
```

### `quit`

Acknowledges and exits successfully.

```json
{"id":"11","command":"quit"}
```

## Errors

Command errors do not change the framing and do not kill the session:

```json
{"id":"bad-key","command":"keys","press":["no_such_key"]}
{"ok":false,"command":"keys","id":"bad-key","error":"unknown key id: no_such_key"}
```

Startup failures such as an absent, unreadable or unsupported-size ROM are
reported on stderr and exit nonzero before the ready event.
