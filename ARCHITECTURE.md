# JAYVADERS-DOS — Architecture

DOS EGA Space Invaders in C, targeting real DOS execution inside DOSBox-X.

- **Compiler:** Open Watcom 2.0 (`wcc`, 16-bit real mode DOS), built with `wmake`.
- **Memory model:** Medium (near data, far code) — **fixed, no small-model option**.
- **Final deliverable:** `INVADERS.EXE` plus data files generated on first run.
- **Hard constraints:** No external libraries. No floating point anywhere. All
  gameplay timing via EGA vertical retrace (port `3DAh`) — never BIOS timer or
  blocking delays.

This document is the authoritative record of the confirmed design and of every
architectural decision resolved during review. Where this document and the
original loose spec disagree, **this document wins** (see
[Resolved Decisions](#resolved-decisions) and
[Corrections to Original Spec](#corrections-to-original-spec)).

---

## Table of Contents

1. [Resolved Decisions](#resolved-decisions)
2. [Corrections to Original Spec](#corrections-to-original-spec)
3. [Target Hardware & CPU Modes](#target-hardware--cpu-modes)
4. [Command Line Switches](#command-line-switches)
5. [Startup & First-Run Sequence](#startup--first-run-sequence)
6. [File Structure](#file-structure)
7. [EGA Display Layer](#ega-display-layer)
8. [Sprites & Pre-Generation](#sprites--pre-generation)
9. [Title Screen & Attract Mode](#title-screen--attract-mode)
10. [Gameplay](#gameplay)
11. [High Score System](#high-score-system)
12. [Sound System](#sound-system)
13. [Configuration File](#configuration-file)
14. [Game Loop](#game-loop)
15. [Build System](#build-system)
16. [Build Order](#build-order)

---

## Resolved Decisions

These six items were ambiguous or underspecified in the original brief and were
resolved during architecture review. They are now binding.

### D1 — `invaders_per_frame` semantics

`invaders_per_frame` is a **sequential processing cursor** over the living
invader grid. The game holds a persistent cursor index. Each frame it processes
the next `N` *living* invaders starting from the cursor, then advances the
cursor (wrapping at the end of the grid). "Process" means: toggle the invader's
animation frame and evaluate its opportunity to fire.

- **386** (`N=1`): one invader processed per frame. The cursor crawls through
  the grid, spreading animation/fire work across many frames. Intentionally
  staggered/choppy — a visible marker of the slow CPU tier.
- **486** (`N=2`): two per frame; more responsive cadence.
- **P66** (`N=55`): the cursor covers the entire grid every frame — full arcade
  behavior, whole grid animates and fire-checks simultaneously.

Animation is **coupled** to the processing cursor by design (the 386 choppiness
is a feature, not a bug).

### D2 — `march_accel` mechanism (Option A: table index offset)

When looking up the march delay, `march_accel` is added to the count-derived
index before clamping:

```c
effective_index = min(MARCH_TABLE_SIZE - 1, count_index + march_accel);
delay = march_delay_table[effective_index];
```

- **386** (`accel=0`): pure count-based lookup.
- **486** (`accel=1`): always one speed step faster than count alone.
- **P66** (`accel=2`): two steps faster — grid always feels more urgent than
  486 at equivalent invader counts.

Degrades gracefully: the table bottoms out at delay `1`, so over-indexing is
absorbed by the clamp.

### D3 — SB Pro pan register & range

Use voice (DAC) left/right volume register **`0x04`** (not `0x0E`). Full 0–15
range per channel:

```c
#define SBP_VOC_PAN 0x04   /* SB Pro voice L/R volume */

void set_pan(int screen_x) {
    int left  = 15 - (screen_x * 15 / 319);
    int right =       (screen_x * 15 / 319);
    outp(SBP_MIXER_ADDR, SBP_VOC_PAN);
    outp(SBP_MIXER_DATA, (left << 4) | right);
}
```

### D4 — Shield dimensions: 24×16

Bunkers are **24×16 pixels**, authentic arcade proportions (the original spec's
"~16×11" and its 14-wide ASCII art are **superseded** and treated as
illustrative only). Authoritative bunker bitmaps are authored in `SPRITES.C`.

Rationale and consequences:
- 4 bunkers × 24px = 96px of shield across 320px → natural arcade-like spacing.
- 24px = exactly **3 bytes per plane** → byte-aligned blits, no sub-byte shift
  on erosion redraws, when bunkers are placed on byte boundaries.
- `SHIELD_STAGED`: 4 damage states authored at 24×16.
- `SHIELD_PIXEL` (P66): per-pixel erosion over 384 px/bunker.

### D5 — `bomb_sequence[]` inactive-type handling: substitute

When the bomb cycle lands on a type not active in the current CPU mode, fire
**`BOMB_ROLLING`** in its place. The sequence index **always advances at the
same rate** regardless of mode, so the firing *rhythm* is identical across all
tiers; only bomb *variety* scales with hardware.

- **386** (`bomb_types=1`): every slot → ROLLING.
- **486** (`bomb_types=2`): ROLLING + PLUNGER as written; both SQUIGGLY slots →
  ROLLING.
- **P66** (`bomb_types=3`): full sequence as authored.

### D6 — Memory model: Medium (fixed)

Medium model is mandatory and not configurable. The simultaneous residency of
the full sprite cache, font glyphs, and SB Pro PCM samples overruns small
model's 64KB data segment; medium gives far-code headroom without far-data
pointer complexity.

---

## Corrections to Original Spec

Locked corrections carried from review (in addition to D1–D6):

| Area | Original | Corrected |
|------|----------|-----------|
| SB Pro pan register | `SBP_VOC_PAN = 0x0E` (master volume) | `0x04` (voice DAC L/R) |
| Pan formula range | `2 - (x*2/319)` (0–2) | `15 - (x*15/319)` (0–15) |
| `march_delay_table` lookup | unindexed for sub-55 grids | clamp index to `[0, SIZE-1]` |
| Shield size | ~16×11 / 14-wide art | 24×16 authentic |
| Memory model | "small or medium as needed" | medium, fixed |
| MAKEFILE float policy | implicit | explicit no-float (`-zf`, no float libs) |

---

## Target Hardware & CPU Modes

Three CPU modes selected at first startup, saved to `INVADERS.CFG`. On first
launch with no CFG, a text-mode prompt is shown:

```
  SPACE INVADERS

  Select your machine:

  [1] 386 DX/16
  [2] 486 DX/33
  [3] Pentium 66

  Press 1, 2, or 3...
```

DOSBox-X targets: 486 ≈ 33MHz cycles, Pentium ≈ 66MHz cycles, 386 ≈ 16MHz.

```c
#define CPU_386  0
#define CPU_486  1
#define CPU_P66  2
```

`GameSettings` populated by `apply_cpu_mode()`:

```c
typedef struct {
    int grid_cols;
    int grid_rows;
    int max_bombs;
    int ufo_enabled;
    int ufo_interval;
    int shield_mode;        /* SHIELD_STAGED or SHIELD_PIXEL */
    int explosion_frames;
    int title_starfield;    /* STAR_NONE, STAR_STATIC, STAR_SCROLL */
    int march_accel;        /* table index offset, see D2 */
    int invaders_per_frame; /* processing-cursor stride, see D1 */
    int bomb_types;         /* number of bomb types active */
    int extra_life_score;
    int ufo_shot_trigger;
} GameSettings;
```

### Per-mode values

| Field | 386 DX/16 | 486 DX/33 | Pentium 66 |
|-------|-----------|-----------|------------|
| `grid_cols` | 8 | 11 | 11 |
| `grid_rows` | 4 | 4 | 5 |
| `max_bombs` | 1 | 3 | 5 |
| `ufo_enabled` | 0 | 1 | 1 |
| `ufo_interval` | — | 800 | 500 |
| `shield_mode` | STAGED | STAGED | PIXEL |
| `explosion_frames` | 2 | 4 | 8 |
| `title_starfield` | NONE | STATIC | SCROLL |
| `march_accel` | 0 | 1 | 2 |
| `invaders_per_frame` | 1 | 2 | 55 |
| `bomb_types` | 1 (ROLLING) | 2 (ROLLING+PLUNGER) | 3 (all) |
| `extra_life_score` | 2000 | 1500 | 1500 |
| `ufo_shot_trigger` | 30 | 23 | 23 |

> 386 grid caps at 32 invaders, 486 at 44. The `march_delay_table` index is
> clamped (see D2 / corrections) so sub-55 starting counts are handled.

---

## Command Line Switches

CPU overrides (session-only unless `/SAVE`):
```
/386   /486   /P66
```

Sound overrides (session-only unless `/SAVE`):
```
/SBP        force Sound Blaster Pro
/SB         force Sound Blaster mono
/SPK        force PC speaker
/NOSOUND    disable all audio
/PORT:xxx   override SB base port (hex, e.g. /PORT:240)
/IRQ:x      override IRQ number
/DMA:x      override DMA channel
```
Combinable, e.g. `INVADERS.EXE /SBP /PORT:240 /IRQ:7 /DMA:3`.

Persistence & resets:
```
/SAVE       persist current session switches to INVADERS.CFG
/reset      reset CFG only; re-prompts CPU and sound next run
/resethi    reset HISCORES.DAT to compiled-in defaults
/resetall   reset both CFG and high scores
```

Developer/test:
```
/SOUNDTEST  verbose sound detection, play test tones, exit (no EGA mode)
```

`/SOUNDTEST` output format:
```
Sound Blaster Detection
-----------------------
BLASTER env : A220 I5 D1 T4
Base port   : 220h
DSP reset   : OK
DSP version : 3.01
Card type   : Sound Blaster Pro
IRQ         : 5
DMA         : 1
Testing PC Speaker...     OK
Testing SB DAC output...  OK
Testing SB Pro stereo...  OK
Press any key to continue or ESC to exit.
```

Switches are session-only unless `/SAVE` is also present.

---

## Startup & First-Run Sequence

**Very first launch** (no CFG or DAT files):
1. Run sound detection.
2. Show CPU selection prompt.
3. Show sound selection prompt with auto-detected recommendation.
4. `apply_cpu_mode()` and `apply_sound_mode()`.
5. Generate `SPRITES.DAT` (all sprite frames pre-rendered).
6. Generate `SOUNDS.DAT` (all PCM synthesized — SB modes only).
7. Write `HISCORES.DAT` (default table).
8. Write `INVADERS.CFG`.
9. Proceed to title screen.

**Subsequent launches:**
1. Run sound detection (re-detect unless `SOUND_AUTO=0`).
2. Load `INVADERS.CFG`.
3. Load `SPRITES.DAT`.
4. Load `SOUNDS.DAT` if in an SB mode.
5. Load `HISCORES.DAT` (verify magic + checksum; regenerate silently if bad).
6. Proceed to title screen.

---

## File Structure

Source files:
```
INVADERS.C  main(), command-line parsing, startup, state machine
EGA.C       hardware layer: mode set, vsync, blit, palette, pixel ops
EGA.H       EGA constants, prototypes
SPRITES.C   raw pixel bitmap arrays for all sprites
SPRITES.H   sprite structs and prototypes
CACHE.C/.H  first-run pre-generation; SPRITES.DAT / SOUNDS.DAT I/O
TITLE.C/.H  title screen, attract mode, high score display
INPUT.C     keyboard scanning via port 60h
GAME.C      invader grid, bullets, collision, shields, wave logic
SOUND.C     PC speaker, SB, SB Pro detection and playback
HISCORE.C   high score table load/save/entry screen
MAKEFILE    Open Watcom wmake — targets: all, clean, rebuild
```

Generated at runtime:
```
INVADERS.CFG   configuration (text)
SPRITES.DAT    pre-generated sprite frames (required after first run)
SOUNDS.DAT     pre-synthesized PCM (SB/SBP modes only)
HISCORES.DAT   high score table
```

Distribution ships: `INVADERS.EXE`, `README.TXT`. All DAT/CFG generated on
first run.

---

## EGA Display Layer

- **Mode:** EGA `0Dh` — 320×200, 16 colors, planar memory.
- **Vsync:** poll port `3DAh` for vertical retrace (locks to ~70Hz refresh).
- **Math:** integer only, no floating point.
- **Buffering:** double buffering via **dirty rectangle tracking** (not full page
  flip).
- **Palette:** register writes via ports `3C0h`/`3C1h` for color-cycling effects.
- **Planar writes:** 4 planes, write mode 0, map mask register
  (ports `3C4h`/`3C5h`).

EGA color assignments:

| Element | Color | EGA index |
|---------|-------|-----------|
| Type A invader (squid, top) | cyan | 3 |
| Type B invader (crab, middle) | magenta | 5 |
| Type C invader (octopus, bottom) | green | 2 |
| Player ship | white | 7 |
| UFO | red | 4 |
| Bullets & bombs | yellow | 14 |
| Shields | green | 2 |
| Background | black | 0 |
| Score text | white | 7 |
| Title logo | cyan + magenta + drop shadow | — |

---

## Sprites & Pre-Generation

All sprites pre-rendered to `SPRITES.DAT` on first run. **Zero runtime sprite
calculation during gameplay** — pure `memcpy` to EGA memory. Erasure is handled
by dirty-rectangle tracking (`ega_dirty_*` in `EGA.H`); the game loop fills
dirty rects with black before each redraw.

Sprites to pre-generate:
- Type A / B / C invaders: 2 animation frames each
- Shared invader explosion: 1 frame (covers all three invader types)
- Player ship: 1 normal frame; 8 explosion frames (always cached —
  CPU mode controls how many are displayed: 2 on 386, 4 on 486, 8 on P66)
- UFO: 2 frames (frame 0 = normal, frame 1 = explosion)
- Player bullet: 1 frame
- Bomb rolling: 4 frames
- Bomb plunger: 4 frames
- Bomb squiggly: 4 frames
- Shield: 4 damage states × 16 rows (for `SHIELD_STAGED`)
- Lives icon: small player ship for HUD
- Font glyphs: 0–9, A–Z (36 glyphs × 8 rows)

Invaders use **authentic 11×8 arcade bitmaps** (exact original Space Invaders
pixel data). Shields use **authentic 24×16 bitmaps** (see D4) authored in
`SPRITES.C`.

---

## Title Screen & Attract Mode

Title screen contents:
- "SPACE INVADERS" logo, large chunky pixel font.
- Cyan + magenta with drop shadow.
- EGA palette color cycling on logo (ports `3C0h`/`3C1h`) for shimmer.
- Starfield per CPU mode (`STAR_NONE` / `STAR_STATIC` / `STAR_SCROLL`).
- Animated marching invader (attract).
- Blinking "PRESS FIRE TO START".
- High score display.

Attract cycle (loops until fire):
1. Title with animated march (5s)
2. High score table (5s)
3. Demo game with AI player (15s)
4. Return to step 1

Demo attract (TITLE.C placeholder — steps 8–9 replace with full AI):
- Frozen invader grid drawn once; player sprite oscillates L↔R.
- No AI logic, no shooting, no collision — purely visual.
- Full AI demo (move toward nearest bomb, fire when invader above) is wired
  by GAME.C / INVADERS.C once those modules exist.

---

## Gameplay

### Lives
- Start with 3.
- Extra life at per-mode score threshold.
- Max 6 lives shown as ship icons (bottom-left). Beyond 6 tracked but not drawn.

### Score display
- Current score top center; high score top right.
- Values: bottom row 10, middle rows 20, top rows 30, UFO 50–800.

### Wave progression
- Each new wave grid starts one row lower.
- Wave 1 starts in top third.
- **Any invader reaching the player row = instant game over** regardless of
  lives.
- Wave 6+ repeats wave 6 (plateau).

```c
typedef struct {
    int grid_start_row;
    int bomb_speed;
    int bomb_fire_rate;
    int march_step;
    int ufo_shot_trigger;
} WaveConfig;
```

| Wave | start_row | bomb_speed | fire_rate | march_step | ufo |
|------|-----------|------------|-----------|------------|-----|
| 1 | 32 | 1 | 48 | 2 | 23 |
| 2 | 40 | 1 | 44 | 2 | 23 |
| 3 | 48 | 2 | 40 | 3 | 23 |
| 4 | 56 | 2 | 36 | 3 | 23 |
| 5 | 64 | 2 | 30 | 4 | 20 |
| 6+ | 72 | 3 | 24 | 4 | 18 |

> 386 mode deliberately starts grids lower to create a claustrophobic feel that
> makes the reduced grid count feel intentional.

### Invader grid
- 11×5 (P66), 11×4 (486), 8×4 (386).
- Marches right, steps down one row, reverses on hitting boundaries.
- Only the bottom invader in each column can fire.

### March speed
Frames between moves, indexed by remaining invader count (then offset by
`march_accel` per D2, then clamped):

```c
int march_delay_table[] = {
    48, 44, 40, 36, 32, 28,   /* 55,50,45,40,35,30 invaders */
    24, 20, 16, 12,  8,  1    /* 25,20,15,10, 5, 1 invaders */
};
```
- At 1 invader: moves nearly every frame.
- **Last-life march speed boost** (required): march is slightly faster than the
  count alone would give while the final life is active.

### Bomb types
- `BOMB_ROLLING` — random column bottom invader, rolling animation.
- `BOMB_PLUNGER` — targets player X, specific columns.
- `BOMB_SQUIGGLY` — purely random, most dangerous; all three active on P66.

Firing sequence (cycles in order; inactive types substitute ROLLING per D5):
```c
int bomb_sequence[] = {
    BOMB_ROLLING, BOMB_SQUIGGLY, BOMB_ROLLING,
    BOMB_PLUNGER, BOMB_ROLLING,  BOMB_SQUIGGLY
};
```

### Player bullet
- One bullet on screen at a time — fire locked until impact or exit.
- 4 px/frame upward.
- Can intercept invader bombs midair.

### UFO (Mystery Ship)
- Triggered by player shot count: shot 23, then every 15 shots after.
- Alternates direction each appearance.
- Score by shot count mod 15:

| shot % 15 | points |
|-----------|--------|
| 0,1,2 | 300 |
| 3,4,5 | 100 |
| 6,7,8 | 800 (skill shot) |
| 9,10 | 100 |
| 11,12 | 300 |
| 13,14 | 200 |

### Shields
- 4 bunkers evenly spaced, **24×16** each (see D4).
- Invader bombs erode top-down; player bullets erode bottom-up.
- Invaders marching through a shield row destroy it completely.
- `SHIELD_PIXEL`: per-pixel erosion (P66).
- `SHIELD_STAGED`: 4 damage states (486 and 386).

### Death sequences

Player hit:
1. Collision detected.
2. Player explosion animation.
3. **All action freezes** during explosion (invaders stop, bombs freeze).
4. Brief pause.
5. Show remaining lives.
6. Lives remain → respawn, invaders resume current positions.
7. No lives → game over.

Invader hit:
1. Explosion frame at position.
2. Remove from living bitmask.
3. **March speed recalculated immediately** from new count.
4. Score added and displayed.

### Game over sequence
1. "GAME OVER" red, center.
2. 2–3s pause.
3. Check score vs table.
4. If qualifies → initials entry screen.
5. Show table 5s.
6. Return to attract.

---

## High Score System

Three separate leaderboards (one per CPU mode), 10 entries each.

```c
typedef struct {
    char initials[4];   /* 3 chars + NUL */
    long score;
    int  cpu_mode;
} HiScoreEntry;

typedef struct {
    unsigned int magic;        /* 0x5349 */
    unsigned int version;
    HiScoreEntry entries[30];  /* 10 per mode, 3 modes */
    unsigned int checksum;     /* XOR, corruption detect */
} HiScoreTable;
```

If file missing or checksum fails: silently regenerate from compiled-in
defaults.

### Default tables (compiled-in fallback)

| 386 | 486 | P66 |
|-----|-----|-----|
| HAL 29500 | JAY 61800 | JAY 99999 |
| SID 18200 | ACE 54200 | ACE 87500 |
| C64 9999 | REX 43100 | RJC 72300 |
| REX 8800 | TOM 38800 | TOM 61800 |
| TOM 7200 | ZAP 29500 | ZAP 54200 |
| ZAP 6100 | VIC 22300 | VIC 43100 |
| ACE 5400 | HAL 18200 | REX 38800 |
| VIC 4300 | SID 14500 | HAL 29500 |
| RJC 3800 | C64 9999 | SID 18200 |
| JAY 2500 | RJC 7200 | C64 9999 |

### Initials entry
- Shown only when score qualifies.
- Three slots, A–Z cycling up/down with direction keys; fire confirms each.
- Active slot highlighted in EGA color.
- "YOU ARE #X!" during entry.
- After entry: full updated table with new entry highlighted.

Displayed in: attract cycle (5s), game over screen. Shows CPU-mode column per
entry.

---

## Sound System

Three tiers:
```
SOUND_SPK   PC Speaker (always available, fallback)
SOUND_SB    Sound Blaster 1.0/2.0 mono 8-bit
SOUND_SBP   Sound Blaster Pro stereo 8-bit
```

### Auto-detection order
1. Parse `BLASTER` env var (`A`=port, `I`=irq, `D`=dma, `T`=type).
2. DSP reset; verify `0xAA`.
3. DSP version via command `0xE1`:
   - 1.x → `SOUND_SB1`
   - 2.x → `SOUND_SB2` (treated as `SOUND_SB`)
   - 3.x → `SOUND_SBP`
   - 4.x → `SOUND_SBP` (SB16, 8-bit mode only)
4. If `BLASTER` unset: scan ports `210h, 220h, 230h, 240h`.
5. Nothing responds → `SOUND_SPK`.

### Port constants

```c
/* base = detected or 220h default */
SB_RESET  = base + 0x6
SB_READ   = base + 0xA
SB_WRITE  = base + 0xC
SB_STATUS = base + 0xE

SBP_MIXER_ADDR = base + 0x4
SBP_MIXER_DATA = base + 0x5
SBP_VOC_PAN    = 0x04      /* corrected, see D3 */
```

PC Speaker I/O: port `42h` (PIT counter 2), `43h` (PIT control), `61h` (gate).
`divisor = 1193180 / frequency`. Duration controlled by the game loop — no
blocking delays.

### Events per tier

| Event | SPK | SB | SBP |
|-------|-----|----|----|
| March beat | 4-note tempo beep (160,130,100,80 Hz) | bass thump | stereo bass thump |
| Player fire | high zap tone | zap sample | zap sample |
| Invader explosion | noise burst (freq sweep) | explosion sample | stereo explosion |
| Player death | descending tones | death sample | rich death sample |
| UFO pass | warbling tone | UFO hum loop | hum panned L→R tracking UFO |
| UFO hit | ascending fanfare | bonus sample | bonus sample |
| Shield hit | low tick | tick sample | tick sample |
| Level clear | ascending fanfare | fanfare sample | stereo fanfare |
| Title music | simple melody loop | richer melody loop | full stereo melody loop |

### SB Pro panning (see D3)
```c
void set_pan(int screen_x) {
    int left  = 15 - (screen_x * 15 / 319);
    int right =       (screen_x * 15 / 319);
    outp(SBP_MIXER_ADDR, SBP_VOC_PAN);   /* 0x04 */
    outp(SBP_MIXER_DATA, (left << 4) | right);
}
```

### Priority (higher interrupts lower on PC Speaker)
```
1: Player death, level clear
2: Player fire, UFO hit
3: Invader explosion
4: Shield hit
5: March, UFO hum (interruptible)
```

### SOUNDS.DAT
- Pre-synthesized on first run — pure math-generated PCM, no external WAV.
- Raw 8-bit unsigned PCM.
- SB mode: 11025 Hz. SB Pro: 22050 Hz.
- PC Speaker needs no DAT.

IRQ handler catches DMA completion to chain the next effect.

---

## Configuration File

`INVADERS.CFG`, text, one `key=value` per line:

```
CPU=486
SOUND_DEVICE=SBP
SOUND_PORT=220
SOUND_IRQ=5
SOUND_DMA=1
SOUND_AUTO=1        ; 1=re-detect on launch, 0=use saved values
HISCORE_MODE=SEPARATE
```

`SOUND_AUTO=1` re-detects every launch (safe for multi-machine use). Command
line switches without `/SAVE` never modify the CFG.

---

## Game Loop

Per frame:
1. Vsync wait (port `3DAh`).
2. Poll keyboard (port `60h`).
3. Update game logic (invaders, bullets, bombs, UFO).
4. Erase dirty sprites (restore background).
5. Draw updated sprites (`memcpy` pre-generated frames to EGA memory).
6. Update score display if changed.
7. Update sound (march tempo, queued effects).

No floating point. No blocking delays. All timing via vsync and frame counters.

---

## Build System

- Open Watcom 2.0 `wmake`.
- Targets: `all`, `clean`, `rebuild`.
- Compiler: `wcc` (C, real-mode DOS). Linker: `wlink`. Output: `INVADERS.EXE`.
- **Memory model: medium, fixed** (see D6).

### Compiler flags (`CFLAGS = -ml -3 -zf -zp1 -os -s -d0 -W3`)

| Flag | Rationale |
|------|-----------|
| `-ml` | Medium model — far code, near data. Fixed (see D6). |
| `-3` | Explicit 386 instruction set. Default in `wcc` but stated for clarity. Do not raise to `-4`/`-5`: CPU-scheduling hints are meaningless inside DOSBox (it processes guest instructions one-by-one); on real hardware the game is EGA-bus bound, not CPU bound. |
| `-zf` | No floating point. Any accidental FP use fails the build. |
| `-zp1` | Pack structs to 1-byte alignment. Required so on-disk formats (`HiScoreEntry` in `HISCORES.DAT`, `SessionStats`) have no invisible padding between fields. |
| `-os` | Optimize for size over speed. Smaller code improves instruction-cache hit rate on real 386/486 hardware, which matters more than scheduling for a game this compact. The inner loop is EGA-bandwidth bound, not compute bound. |
| `-s` | Strip stack-overflow checks. Saves a few bytes and cycles per call; safe because the stack budget (8 KB) is generous for a flat-model DOS game. |
| `-d0` | No debug info — release build. |
| `-W3` | Elevated warning level. Catches signed/unsigned mismatches and implicit-conversion issues that `-W1` (default) misses. |

### Linker options

`option stack=8192` — 8 KB stack (generous for this call depth).  
`option map` — emits `INVADERS.MAP` showing exact segment and symbol sizes.
Useful for auditing actual DGROUP usage and verifying that DGROUP stays well
under 64 KB.

---

## Build Order

Implement and validate in this order:

[x] 1. `EGA.C` — mode set, vsync, pixel ops, palette control
[x] 2. `SPRITES.C` — raw bitmap arrays, authentic arcade pixel data
[x] 3. `CACHE.C` — pre-gen pipeline, DAT read/write
[x] 4. `TITLE.C` — title screen, validates full EGA layer
    5. `INPUT.C` — keyboard via port `60h`
    6. `SOUND.C` — detection, PC speaker, SB, SB Pro
    7. `HISCORE.C` — table management, initials entry
    8. `GAME.C` — full arcade-accurate game logic
    9. `INVADERS.C` — `main()`, state machine, command-line parsing
   10. Final integration and MAKEFILE tuning
