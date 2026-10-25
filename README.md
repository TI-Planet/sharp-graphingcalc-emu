# Sharp EL-99xx Emulator

An experimental Sharp EL-9900, EL-9900G, EL-9900G SII and EL-9950 emulator. The project has a deterministic C++20 core shared by native SDL3 and WebAssembly frontends and is licensed under BSD-3-Clause. ROM images are supplied separately by users.

> **Current status:** early hardware bring-up. The known 1.20 firmware boots through its real first-use initialization flow and reaches the calculation screen. Its complete visible keyboard matrix is verified against the ROM translation table; several other peripherals still need verification. See [Implementation status](#implementation-status) before reporting behavior as fully emulated.

## ROM

Supply a raw, exactly 1 MiB (`0x100000` byte) ROM image. The known EL-9950 1.20 dump is recognized by SHA-256:

```
deae0913c635b02e5b273db20905fa57993ec035edb7cbf78f08f9bc79f42082
```

Model and version are detected from the diagnostic routine's model banner and
version bytes at `0xFFBFE4–E5`, independently of the filename or whole-ROM hash.
The supplied EL-9900 1.09, EL-9900G 3.01, EL-9900G SII 4.10/4.11 and
EL-9950 1.20/1.21 dumps are recognized. SII 4.11 retains a 9950 diagnostic
banner; its major version identifies the SII family. Unknown 1 MiB images are
accepted with a warning and the fixed EL-9950 skin. Encoded `.sim` files are not supported. The source ROM is never modified: Flash writes are stored in a SHA-256-keyed overlay. A state snapshot is taken before the boot-page range `FFC000-FFFFFF` is changed.

## Build

Requirements are CMake 3.25+, Ninja or Make, a C++20 compiler, and Git. Native frontend dependencies are pinned and fetched by CMake. No separate SDL or image-loader installation is needed for the default build.

### Headless core and tests

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

To include the optional known-ROM smoke test:

```sh
EL9950_ROM=/path/to/el9950_ver120.bin ./build/el9950-tests
./build/el9950-headless /path/to/el9950_ver120.bin 100000
```

The reversible-keyboard menu regression also supports the supplied EL-9900
1.09, EL-9900G 3.01, EL-9900G SII 4.10/4.11 and EL-9950 1.20/1.21 raw dumps:

```sh
EL99_MENU_ROM=/path/to/ROM.bin ./build/el9950-tests
```

It checks the real firmware's PRGM tabs, extended MATH tabs and `2ndF` + PRGM
(SOLVER), plus basic/advanced switching, firmware key translation on both sides,
state restoration and recovery of older snapshots.

The headless executable is built by default and has no SDL/ImGui dependency. It is intended for CI, scripts and coding agents. For example:

```sh
# Run until the firmware first enters HALT and return one JSON object.
./build/el9950-headless ROM.bin --until-halt 20000000 --json

# Reproduce the maintenance reset chord, save state, and capture device I/O.
./build/el9950-headless ROM.bin 1000000 \
  --hold second --hold variable --reset \
  --state-out autodiag.el99state --trace-io autodiag.ndjson --json

# Restore state and disassemble 12 instructions from the current investigation area.
./build/el9950-headless ROM.bin --state-in autodiag.el99state \
  --disasm 0xffa080 12 --json
```

Key arguments use the stable IDs from `assets/skin.json`. Options are applied from left to right before execution, which allows multi-key/reset sequences. JSON output includes the ROM hash, cycle counts, PC, SR, HALT state, framebuffer hash and optional disassembly. I/O traces are newline-delimited JSON suitable for debugging and automated tests.

For a persistent agent session, start `el9950-headless ROM.bin --control`. It writes a ready handshake, then accepts one JSON command per stdin line and writes exactly one flushed JSON response per line. The full framing and error contract is in [`headless-protocol.md`](headless-protocol.md). Supported protocol-v1 commands are:

```json
{"id":"1","command":"status"}
{"id":"2","command":"keys","press":["second","variable"],"release":[]}
{"id":"3","command":"reset","action":"pulse"}
{"id":"3b","command":"battery","operating_percent":100,"backup_healthy":true}
{"id":"4","command":"run","cycles":1000000,"until_halt":true}
{"id":"5","command":"screenshot"}
{"id":"6","command":"state.dump"}
{"id":"7","command":"state.load","data_base64":"..."}
{"id":"8","command":"memory.read","address":16711680,"length":64}
{"id":"9","command":"memory.write","address":34,"value":8}
{"id":"10","command":"disassemble","address":16752896,"count":16}
{"id":"11","command":"quit"}
```

`screenshot` returns the 132x64 LCD as a base64 PGM image; `state.dump` returns the complete versioned `.el99state` payload as base64. IDs are optional strings echoed for request/response correlation. Protocol data stays on stdout; fatal startup errors and usage go to stderr.

### Native macOS application

```sh
cmake -S . -B build-native -G Ninja \
  -DEL9950_BUILD_FRONTEND=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-native
open build-native/el9950-emu.app --args /path/to/el9950_ver120.bin
```

The ROM can also be dropped onto the running window. A fresh ROM is advanced
through cold reset and powered on automatically; the **Power** button and
`Escape` subsequently generate the calculator's short ON edge. Save states and
Flash persistence live under macOS Application Support, keyed by ROM hash.
Multiple native instances are allowed; the planned two-calculator link mode
will also support hosting both machines inside one process.

### WebAssembly

Install and activate an Emscripten SDK, then:

```sh
emcmake cmake -S . -B build-web -G Ninja \
  -DEL9950_BUILD_FRONTEND=ON \
  -DEL9950_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
python3 -m http.server 8765 --directory build-web
```

Open `http://localhost:8765/` and select a ROM. ROM bytes remain local to the browser. State data uses IndexedDB; persistent ROM retention is not enabled.

## Controls

- Drag a raw ROM into the native window, or use **Load ROM** on the web.
- Number row, arrows, Return, Backspace and Delete map to their calculator equivalents.
- Shift maps to `2ndF`, `X` maps to `X/theta/T/N`, and Escape maps to `ON`.
- The reset-compartment contact is modeled as an explicit open/close operation so maintenance chords can remain held while the contact closes.
- Mouse and multitouch presses are tracked independently for maintenance combinations.
- Pressed keys are highlighted directly on the skin. Right-clicking a key latches it; right-clicking it again releases it, so multi-key maintenance chords can be assembled before reset.
- EL-9900 family ROMs show a **Keyboard** selector above the calculator: choose **Advanced** (default) or **Basic** to flip the keyboard. The skin, hit regions and physical P4.6 input follow the selected side. Press a calculator key to let the firmware detect the flip, then acknowledge its keyboard/mode-change message. The ROM selects its own menus and key translations; no reset is required. EL-9950 has a fixed keyboard.
- Save states retain the keyboard side. Older states (v4–v6) load with the previous advanced-side default. Loading another ROM starts on Advanced.
- Headless clients can use `--keyboard-side basic` or `{"command":"keyboard","side":"basic"}`. Key IDs remain electrical-position IDs: for example `sin` presses **Simp** on the basic side.
- The toolbar battery control retains an operating-battery setting from 0–100% and exposes the independent backup-battery input. Both default to full/healthy. The operating-battery setting is saved but its low-voltage input is not yet modeled.

## Architecture

- `include/el9950/`, `src/`: platform-independent CPU, bus and devices.
- `third_party/mame-tlcs900/`: the TLCS-900/H core adapted from the pinned MAME revision documented in `THIRD_PARTY_NOTICES.md`.
- `src/frontend/`: SDL3 and Dear ImGui frontend shared by native and web builds.
- `config/hardware.json`: observed facts and explicitly marked provisional assumptions.
- `assets/skin.json`: reference EL-9950 geometry, electrical coordinates and host bindings; `src/skin.cpp` selects model-specific images and hit/LCD geometry.

The stable core surface covers ROM loading, reset/contact reset, cycle execution, keys, framebuffer access, link pins, versioned save states, memory inspection and breakpoints.

## Implementation status

Implemented and covered by deterministic tests:

- immutable 1 MiB ROM and AMD-style Flash overlay;
- standalone TLCS-900/H instruction engine and debugger state;
- RAM/bus, paged 132x64 LCD with verified panel orientation, complete seven-output/eight-input visible keyboard matrix, and ON/reset inputs;
- provisional TMP91C025 register/timer shell plus the verified 16 ms external alarm used for keyboard debounce;
- three-wire open-drain virtual link primitive;
- versioned `.el99state` data with ROM SHA-256 and CRC;
- native/web frontend, polished skin workspace, visible/latchable multi-key input, battery controls, basic debugger, 1x/turbo scheduler.

Still under investigation:

- remaining TMP91C025/L1 interrupt sources beyond the verified INT1/INT6-INT9 table entries;
- remaining port wiring, timer behavior, clock and RAM sizing;
- complete menu navigation beyond the verified first-use initialization and calculation screen;
- DMA, SIO, operating-battery low-voltage input and analogue discharge, sleep behavior and exact wait states;
- validated Send/Receive/Send-with-Boot-Page transfers and two-machine UI;
- physical link/Web Serial/OHP support, which are intentionally post-v1.

The hardware configuration distinguishes tested behavior from provisional assumptions. ROM-dependent tests activate only when `EL9950_ROM` is set locally.

## Legal

Sharp and EL-9950 are trademarks of their respective owner. Their use identifies compatibility only and does not imply endorsement.

This repository's code is under the BSD-3-Clause license.
Third-party source attribution is in `THIRD_PARTY_NOTICES.md`.
