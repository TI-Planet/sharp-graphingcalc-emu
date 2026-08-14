# Third-party notices

## MAME TLCS-900/H CPU core and disassembler

Files in `third_party/mame-tlcs900/` are adapted from MAME revision
`1e1b6ce78a1645805bb5eef4049e3f1d3f926194` (retrieved 2026-08-14).

- Copyright Wilbert Pol, Felipe Sanches and MAME contributors.
- License: BSD-3-Clause.
- Source: <https://github.com/mamedev/mame/tree/1e1b6ce78a1645805bb5eef4049e3f1d3f926194/src/devices/cpu/tlcs900>

The original per-file notices are retained. `emu.h` is a clean standalone
compatibility layer written for this project and is not MAME framework code.

## Dear ImGui

Fetched only when the graphical frontend is enabled. Copyright Omar Cornut and
contributors. MIT license. Pinned in `cmake/Dependencies.cmake`.

## SDL

SDL3 is fetched only when the graphical frontend is enabled. Copyright Sam
Lantinga and contributors. zlib license. Its version is pinned in
`cmake/Dependencies.cmake`.

## stb_image

`third_party/stb/stb_image.h` comes from `nothings/stb` revision
`2c980bb59875b0d32144a71867fbdebb2f77cd20` and is used only for PNG decoding.
Copyright Sean Barrett and contributors. Dual-licensed public domain or MIT;
the complete notice is retained in the header.
