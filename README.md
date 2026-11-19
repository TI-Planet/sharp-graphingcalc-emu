# Sharp Graphing Calculator Emulator

An experimental Sharp calculator emulator with separate **TLCS-900/L1 (EL-99xx)** and **SC62015 (EL-9200/9300)** hardware backends. The deterministic C++20 machines share native SDL3, WebAssembly and headless frontends. The combined application is distributed under GPLv2; existing BSD code retains its license and notices in `LICENSE.BSD`. ROM images are supplied separately by users.

> **Current status:** early hardware bring-up. The known 1.20 firmware boots through its real first-use initialization flow and reaches the calculation screen. Its complete visible keyboard matrix is verified against the ROM translation table; several other peripherals still need verification. See [Implementation status](#implementation-status) before reporting behavior as fully emulated.

### EL-9300 / SC62015

The supplied **EL-9300 1.11** dump is automatically selected by its 256 KiB size
and diagnostic banner. SHA-256:

```
050375c45a00db09fd77008a4de3ca02b8bc0c0af06f8593f886f143859f6c86
```

It executes the real reset code, displays the complete 96×64 initialization
prompt, accepts **CL**, then **ENTER** at `PRESS ANY KEY`, reaches `REAL MODE`,
and evaluates `1+2` to `3`. The LCD's two controllers cover the upper and lower
halves; column and page orientation come from the firmware's drawing routines.
Native and web builds use the EL-9300 skin for identified EL-9300 ROMs, with the
96×64 LCD and all 52 physical keys mapped. Its nine LCD indicators are decoded
from controller RAM and shown above the dot matrix. EL-9200 and unidentified
SC62015 ROMs show the same LCD on the button panel.

The backend has its own 20-bit bus, immutable ROM at `C0000–FFFFF`, RAM ending
at `1FFFF`, separate 256-byte CPU internal RAM/I/O, interrupt/wake-up handling,
disassembler and CRC/ROM-keyed `.el93state` snapshots. EL-9200 selects an 8 KiB
RAM profile; **only the supplied EL-9300 ROM has been exercised**. Unknown
256 KiB images use the experimental SC62015/32 KiB profile. EL-9400/9600 support
is not implemented by this change.

Remaining work includes physical validation, link/UART wiring, and accurate clock, timers,
power and wait-state behavior. The provisional 2.304 MHz oscillator corresponds
to 768,000 CPU execution states per second; SC62015 `run` budgets count those
states. See [`config/el9300.json`](config/el9300.json) for evidence and assumptions.

```sh
./build/el9950-headless /path/to/EL-9300.bin 1000000 --json
EL9300_ROM=/path/to/EL-9300.bin ./build/sharp-machine-tests
```

The optional ROM test completes initialization and checks the actual `1+2=3`
screen and deterministic snapshot continuation. ROM-free tests cover both
backends, CPU addressing/flags/stacks/interrupts, every opcode handler,
LCD orientation, matrix strobes and state compatibility/rejection.

## ROM

Supply a raw 1 MiB (`0x100000` byte) EL-99xx ROM or a 256 KiB (`0x40000` byte)
EL-9200/9300 ROM. The following identification details concern EL-99xx.
The known EL-9950 1.20 dump is recognized by SHA-256:

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

Requirements are CMake 3.25+, Ninja or Make, C and C++20 compilers, and Git. CMake fetches the pinned SDL3, Dear ImGui, and libwebp sources for the default native build. To build against system libraries, set `EL9950_FETCH_FRONTEND_DEPS=OFF`, install SDL3 and the libwebp decoder development library, and set `imgui_SOURCE_DIR` to a Dear ImGui checkout.

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

`screenshot` returns a base64 PGM image at the selected machine's dimensions
(132×64 or 96×64); `state.dump` reports its `el99state` or `el93state` format.
Status adds machine/CPU identifiers, physical/scheduler frequencies and named
CPU registers. Existing protocol-v1 fields and EL-99xx snapshots remain compatible.
IDs are optional strings echoed for request/response correlation. Protocol data
stays on stdout; fatal startup errors and usage go to stderr.

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

The web build downloads WebP keyboard images on demand after a ROM is selected,
including the other side only when the keyboard is flipped. Downloaded images
are reused for the rest of the session. Startup and image downloads show progress
and offer a retry if a request fails. Host `loader.js` and the `assets/` directory
alongside `index.html`, `index.js` and `index.wasm`; no `index.data` bundle is needed.

## Controls

- Drag a raw ROM into the native window, or use **Load ROM** on the web.
- **Screenshot** saves the LCD as a PNG, including any active EL-9200/9300 indicators. The web build downloads the image and the desktop build opens a save dialog.
- **Speed** offers 1x, 1.5x, 2x and 4x. EL-9200/9300 defaults to 1.5x and EL-99xx defaults to 1x. Headless cycle budgets are independent of this setting.
- Number row, arrows, Return, Backspace and Delete map to their calculator equivalents.
- Shift maps to `2ndF`, `X` maps to `X/theta/T/N`, and Escape maps to `ON`.
- The reset-compartment contact is modeled as an explicit open/close operation so maintenance chords can remain held while the contact closes.
- Mouse and multitouch presses are tracked independently for maintenance combinations.
- Pressed keys are highlighted on the calculator skin. Right-click a key to latch it in orange, then right-click again to release it. This also works on the SC62015 fallback button panel and lets you hold multi-key chords across reset.
- EL-9900 family ROMs show a **Keyboard** selector above the calculator: choose **Advanced** (default) or **Basic** to flip the keyboard. The skin, hit regions and physical P4.6 input follow the selected side. Press a calculator key to let the firmware detect the flip, then acknowledge its keyboard/mode-change message. The ROM selects its own menus and key translations; no reset is required. EL-9950 has a fixed keyboard.
- Save states retain the keyboard side. Older states (v4–v6) load with the previous advanced-side default. Loading another ROM starts on Advanced.
- Headless clients can use `--keyboard-side basic` or `{"command":"keyboard","side":"basic"}`. Key IDs remain electrical-position IDs: for example `sin` presses **Simp** on the basic side.
- The toolbar battery control retains an operating-battery setting from 0–100% and exposes the independent backup-battery input. Both default to full/healthy. The operating-battery setting is saved but its low-voltage input is not yet modeled.

## Architecture

- `include/sharp/machine.hpp`, `src/machine.cpp`: family-neutral session and complete-machine interface; ROM loading constructs a replacement backend before switching. EL-99xx is adapted without changing its existing implementation or snapshot bytes.
- `src/el9300.cpp`, `include/sharp/sc62015.hpp`: SC62015 CPU integration and independent EL-9200/9300 board. Devices and CPU state are owned by each machine, never attached to another family's bus.
- `third_party/pockemul-sc62015/`: GPLv2 instruction engine extracted from a pinned PockEmul revision, without Qt or PC-E500 hardware dependencies.
- `include/el9950/`, `src/`: platform-independent CPU, bus and devices.
- `third_party/mame-tlcs900/`: the TLCS-900/H core adapted from the pinned MAME revision documented in `THIRD_PARTY_NOTICES.md`.
- `src/frontend/`: SDL3 and Dear ImGui frontend shared by native and web builds.
- `config/hardware.json`: observed facts and explicitly marked provisional assumptions.
- `assets/skin.json`: reference EL-9950 geometry, electrical coordinates and host bindings; `src/skin.cpp` selects model-specific images and hit/LCD geometry.

The stable core surface covers ROM loading, reset/contact reset, cycle execution, keys, framebuffer access, link pins, versioned save states, memory inspection and breakpoints.

## Implementation status

The following details concern the existing EL-99xx backend; SC62015 status is
described above. Implemented and covered by deterministic tests:

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

The combined application is under GPLv2 (`LICENSE`). Existing BSD code retains
its original notice (`LICENSE.BSD`). The PockEmul-derived SC62015 engine is GPLv2
and is not offered under BSD or a BSD/GPL dual license. Native installation
includes the license and third-party notices. Release distributors must provide
the corresponding source for their exact build, including any dependency changes;
the [source repository](https://github.com/TI-Planet/sharp-graphingcalc-emu) and
pinned dependency revisions are the starting point.
Third-party source attribution is in `THIRD_PARTY_NOTICES.md`.
