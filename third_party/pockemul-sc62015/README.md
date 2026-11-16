# Standalone SC62015 instruction engine

Extracted from Matsumo's PockEmul, revision
`898c891abb25da94df8951f13a9fc4a0b986c69d`:

- <https://github.com/matsumo/PockEmul/blob/898c891abb25da94df8951f13a9fc4a0b986c69d/src/cpu/sc62015.cpp>
- <https://github.com/matsumo/PockEmul/blob/898c891abb25da94df8951f13a9fc4a0b986c69d/src/cpu/sc62015.h>
- License: GNU GPL version 2, reproduced in `COPYING`.
- Original copyright: Matsumo, 1998–1999; PockEmul contributors.

Only instruction handlers, addressing helpers and register representation are
retained. Qt, GUI/debugger integration, PC-E500 memory mapping, expansion
cards, audio, UART integration and XML persistence are excluded. The calculator
backend supplies its own external bus and internal port callbacks.

Local changes include wrapping internal multi-byte accesses at 256 bytes,
segmented multi-byte instruction fetches, bounded pre-byte recursion, explicit
illegal-opcode failures, and replacing the upstream interrupt, OFF, timer-clear
and WAIT integration. Interrupt dispatch, wake-up and timers belong to this
standalone integration. The register union retains upstream's little-endian
layout; compilation checks the host byte order. Snapshots serialize fields
explicitly and never serialize the union or its padding.

Instruction behavior and timing still require further validation; importing
an existing core does not establish EL-9300 hardware accuracy. In particular,
UART, address-modification hardware and exact board clock/wait states are not
qualified. Sharp's ESR-L instruction manual is used to check the adaptation:
<https://basic.hopto.org/basic/manual/Sharp%20PC-E500%20CPU%20Tech%20Reference.pdf>.
