# Live Process Memory Reader & Radar

**Author:** Liam Bottcher

A series of tools for reading live game state out of a running process's
memory on Linux, ending in a real-time 2D radar rendered with raylib.

Built against AssaultCube (open source, GPL) strictly for **singleplayer
use against AI bots** — no multiplayer, no other real players affected.
This was a self-directed learning project about reverse engineering,
Linux process internals, and C++ ABI layout — not about gaining an
advantage over other people.

## The learning arc

This repo intentionally keeps the earlier, rougher iterations alongside
the final tool, because the progression *is* the actual project — each
phase forced a different technique when the previous one hit a wall.

### Phase 1 — `code/manual-memory-scan/healthEditor.c`
The very first thing I built. Pure `gdb` + `scanmem`, no source code
involved at all: manually scanning process memory for a value that
changed when I took damage, narrowing it down through repeated "changed
value" scans, then reading the raw assembly around the instruction that
wrote to it to confirm the address and offset by hand. This is the
slow, blind, empirical way to find an offset — it works, but it teaches
you almost nothing about *why* the memory is laid out the way it is.

### Phase 2 — `code/pince-pointer-chains/xandyReader.c`, `ammoReader.c`
Moved to PINCE for pointer-chain discovery — scanning for a value,
then having the tool search backward for stable pointers to it,
producing chains like:
```
base + 0x19E280 -> +0x670 -> +0xD0 -> +0x768 -> +0x7E8
```
Faster than fully manual scanning, but still fundamentally blind trial
and error — every offset is a guess confirmed by observed behavior, not
by understanding the actual struct layout underneath it.

### Phase 3 — `code/open-source-memory-analysis/radar1.c`, `radar2.c`
The turning point: AssaultCube is open source. Instead of guessing
offsets empirically, I read the actual C++ class definitions
(`entity.h`), understood the inheritance chain (`playerent : public
dynent, public playerstate`), and used `gdb`'s `ptype /o` against a
debug build to get the *compiler-verified* byte offset of every field —
health, position, yaw — instead of finding them by trial and error.

From there:
- `radar1.c` — proves the whole chain works end-to-end for just your
  own player: find the process, read a static global's address via
  `nm`, dereference it, read fields at their verified offsets, render
  a rotating triangle in raylib showing live position/facing.
- `radar2.c` — extends the same technique to the game's `players`
  vector (a custom C++ vector of pointers), walking it to find every
  bot and rendering them as a real-time, north-up radar: your own
  position fixed at center, each bot's position and facing shown
  relative to you.

## What this demonstrates

- Linux process inspection: `/proc/<pid>/maps`, `/proc/<pid>/comm`,
  ELF symbol tables (`nm`), `process_vm_readv`
- C++ ABI internals: multiple inheritance layout, vtable placement,
  struct alignment/padding, and why none of that can be reliably
  hand-calculated — only verified against a real compiler
- `gdb` as a static analysis tool (`ptype /o`), not just a step-debugger
- Reverse engineering methodology: moving from blind empirical
  scanning (PINCE, `scanmem`) to source-verified ground truth, and
  understanding *why* the second approach is strictly more reliable
- Real-time rendering (raylib) driven entirely by external live memory
  reads, including coordinate-system and rotation math to build a
  proper player-relative radar

## Repo structure

```
code/
  open-source-memory-analysis/     - phase 3: offsets confirmed via source + gdb ptype /o
    radar1.c                  - player-only proof of concept
    radar2.c                   - full radar with bots
  pince-pointer-chains/       - phase 2: chains found via PINCE
    xandyReader.c
    ammoReader.c
  manual-memory-scan/         - phase 1: gdb + scanmem, manual assembly reading
    healthEditor.c
```

## Building

Requires `libraylib5-dev` (or build raylib from source) and a Linux
system. See comments at the top of each file for the exact `nm`/`gdb`
commands used to derive the addresses and offsets for your own build,
since these will differ if you compile the game yourself.

```bash
gcc radar2.c -o radar2 -lraylib -lm -lpthread -ldl
sudo ./radar2
```

`sudo` (or an adjusted `ptrace_scope`) is required because
`process_vm_readv` needs the same permissions as attaching a debugger.

## Platform

Linux only. The project was developed and tested in a Linux environment and uses Linux-specific process and memory interfaces.

## AI Assistance

AI tools were used throughout development, including for generating and modifying portions of the code, explaining unfamiliar concepts, debugging, exploring implementation approaches, and helping draft and refine this README. I directed the project, performed the memory analysis and experimentation, tested the implementations, and reviewed and modified the resulting code.
