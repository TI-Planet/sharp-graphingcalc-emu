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

Returns cycle count, CPU PC/SR/HALT state, reset-contact state, both battery inputs, pressed-key bitmask and framebuffer SHA-256.

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

```json
{"id":"3","command":"keys","press":["second","variable"],"release":[]}
{"id":"4","command":"keys","press":[],"release":["second","variable"]}
```

### `reset`

`open` opens the battery-compartment contact and suspends execution. `close` closes it and performs reset if it was open. `pulse` performs open then close in one command while preserving currently held keys.

```json
{"id":"5","command":"reset","action":"pulse"}
```

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

Returns the live 132×64 LCD, not the photographic skin, as a complete binary PGM image (`P5`) in `data_base64`. The response includes `mime_type`, `width` and `height`.

```json
{"id":"6","command":"screenshot"}
```

### `irq` (debugging)

Asserts, clears, or pulses a raw TLCS-900 input line. This is intended for hardware bring-up, debugger clients and automated tests; normal clients should not need it once the corresponding peripheral is modeled.

```json
{"id":"irq-probe","command":"irq","line":12,"action":"pulse"}
```

### `state.dump` / `state.load`

`state.dump` returns the complete versioned `.el99state` payload in `data_base64`. `state.load` accepts that payload and verifies magic, version, ROM SHA-256 and CRC before applying anything.

```json
{"id":"7","command":"state.dump"}
{"id":"8","command":"state.load","data_base64":"RUw5OVNUQVQ..."}
```

State dumps can be hundreds of KiB. Controllers must not impose a small line-length limit.

### `memory.read`

Returns at most 65,536 bytes from the 24-bit address space as base64. Reads are debugger peeks and do not trigger device side effects.

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

Returns up to 1,024 decoded TLCS-900/H instructions. If `address` is omitted, disassembly begins at PC; if `count` is omitted, it defaults to 16.

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

Startup failures such as an absent, unreadable or non-1-MiB ROM are reported on stderr and exit nonzero before the ready event.
