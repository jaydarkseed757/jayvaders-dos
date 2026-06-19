# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Project

DOS EGA Space Invaders in C, targeting **real DOS execution inside DOSBox-X**.
The full design lives in [`ARCHITECTURE.md`](./ARCHITECTURE.md) — that document
is authoritative. Read it before making design decisions. Where it conflicts
with the original loose spec, ARCHITECTURE.md wins.

Final deliverable: `INVADERS.EXE` plus data files generated on first run.

## Toolchain

- **Compiler:** Open Watcom 2.0 — `wcc` (16-bit real-mode DOS), linker `wlink`.
- **Build:** `wmake`.
- **Memory model:** medium (near data, far code) — **fixed, not configurable**.

### Build commands

```
wmake          # default target: all -> INVADERS.EXE
wmake clean     # remove objects and binary
wmake rebuild   # clean then all
```

(The MAKEFILE is authored last in the build order; until it exists these are
the intended targets.)

## Hard Constraints — do not violate

1. **No floating point anywhere.** Integer math only. The build uses `-zf` and
   links no float libraries so accidental float use fails the build. If you need
   fractional math, use fixed-point integers.
2. **No external libraries.** Pure C plus direct hardware port I/O only.
3. **All gameplay timing via EGA vertical retrace** (poll port `3DAh`). Never
   use the BIOS timer or blocking delays (`delay()`, busy-wait sleeps) for
   gameplay. This keeps speed consistent across DOSBox cycle settings.
4. **No runtime sprite calculation during gameplay.** All frames are
   pre-generated to `SPRITES.DAT` on first run; gameplay does pure `memcpy` to
   EGA memory.
5. **Do not enter EGA mode during `/SOUNDTEST`** — it runs detection verbosely,
   plays test tones, and exits in text mode.

## Resolved design decisions (see ARCHITECTURE.md "Resolved Decisions")

Keep these straight — several diverge from the original brief:

- **D1 `invaders_per_frame`** — sequential processing cursor over the living
  grid (386=1, 486=2, P66=55). Animation is coupled to the cursor; 386
  choppiness is intentional.
- **D2 `march_accel`** — table index offset:
  `idx = min(SIZE-1, count_idx + march_accel)`.
- **D3 SB Pro pan** — voice register `0x04` (NOT `0x0E`), full 0–15 range.
- **D4 Shields** — 24×16 authentic arcade (NOT the spec's 14-wide ASCII).
- **D5 Bomb sequence** — substitute `BOMB_ROLLING` for inactive types; index
  advances uniformly so cadence is mode-invariant.
- **D6 Memory model** — medium, fixed.

## Source layout

```
INVADERS.C  main(), CLI parsing, startup, state machine
EGA.C/.H    hardware layer: mode set, vsync, blit, palette, planar pixel ops
SPRITES.C/.H raw arcade bitmap arrays, sprite structs
CACHE.C/.H  first-run pre-generation; SPRITES.DAT / SOUNDS.DAT I/O
TITLE.C     title screen, attract mode, high score display
INPUT.C     keyboard via port 60h
SOUND.C     PC speaker, SB, SB Pro detection + playback
HISCORE.C   high score table load/save/entry
GAME.C      invader grid, bullets, collision, shields, wave logic
MAKEFILE    Open Watcom wmake
```

Generated at runtime (not committed): `INVADERS.CFG`, `SPRITES.DAT`,
`SOUNDS.DAT`, `HISCORES.DAT`.

## Build order

Implement and validate in sequence (each layer validates the previous):

`[x] EGA.C → [x] SPRITES.C → [x] CACHE.C → TITLE.C → INPUT.C → SOUND.C →
HISCORE.C → GAME.C → INVADERS.C → MAKEFILE`

## Hardware reference (ports)

- EGA: mode `0Dh`; vsync port `3DAh`; palette `3C0h`/`3C1h`; map mask
  `3C4h`/`3C5h` (4 planes, write mode 0).
- Keyboard: port `60h`.
- PC speaker: PIT `42h`/`43h`, gate `61h`; `divisor = 1193180 / freq`.
- Sound Blaster: `base+6` reset, `base+0xA` read, `base+0xC` write,
  `base+0xE` status; SB Pro mixer `base+4`/`base+5`.

## Conventions

- C identifiers and constants follow the names in ARCHITECTURE.md
  (`GameSettings`, `WaveConfig`, `HiScoreEntry`, `CPU_386`, `SHIELD_PIXEL`,
  `BOMB_ROLLING`, etc.). Reuse them exactly.
- Match the style of surrounding code once files exist.
- Authentic arcade behavior is the goal: UFO shot timing, bomb sequence, march
  delay table, and wave start rows must match the original.

## Git

- Develop on branch `claude/dos-invaders-architecture-7p8dm1`.
- Commit with clear messages; push to that branch.
- Do **not** open a pull request unless explicitly asked.
