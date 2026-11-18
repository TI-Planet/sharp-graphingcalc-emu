# Third-party notices

## PockEmul SC62015 instruction engine

Files in `third_party/pockemul-sc62015/` are adapted from PockEmul revision
`898c891abb25da94df8951f13a9fc4a0b986c69d`.

- Copyright Matsumo, 1998–1999, and PockEmul contributors.
- License: GNU GPL version 2; see that directory's `COPYING` and `README.md`.
- Source: <https://github.com/matsumo/PockEmul/tree/898c891abb25da94df8951f13a9fc4a0b986c69d/src/cpu>

The combined emulator is distributed under GPLv2 (`LICENSE`). Existing BSD
code and its copyright notice remain covered by `LICENSE.BSD`; other bundled
components retain their individual licenses below. The SC62015 component is
not offered under BSD or a BSD/GPL dual license.

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

## libwebp

The frontend uses the decoder from libwebp 1.6.0 in the default builds.
Copyright Google Inc. and contributors. BSD-3-Clause license and patent grant
are in `third_party/libwebp/`.
Source: <https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz>.
