# JAYVADERS-DOS

Authentic DOS Space Invaders in C, targeting real DOS execution inside DOSBox-X.
EGA 320×200 16-colour graphics, PC Speaker / Sound Blaster / SB Pro audio,
three CPU-speed difficulty modes, and a session statistics screen on exit.

---

## Requirements

- **DOSBox-X** (or any DOSBox fork with EGA support)
- **Open Watcom 2.0** to build — install into `../watcom/` or set `WATCOM_DIR`
- A Unix-like host (macOS/Linux) to run the build and launch scripts

---

## Building

```
chmod +x compile.sh run.sh
./compile.sh        # builds INVADERS.EXE inside DOSBox-X
```

Or directly with wmake inside a DOS environment:

```
wmake               # build INVADERS.EXE
wmake clean         # remove objects and binary
wmake rebuild       # clean then build
```

The linker emits `INVADERS.MAP` alongside the EXE showing exact segment sizes.

---

## Running

```
./run.sh            # launches INVADERS.EXE in DOSBox-X
```

**First launch** auto-detects your sound hardware, prompts for CPU mode and
sound device, then generates `SPRITES.DAT`, `SOUNDS.DAT`, and `HISCORES.DAT`
before dropping into the title screen. Subsequent launches load those files
directly.

Press **ESC** at any time (title or mid-game) to quit cleanly and see the
session stats screen.

---

## CPU Modes

Three difficulty tiers, chosen on first launch or overridden with a flag:

| Mode | Flag | Behaviour |
|------|------|-----------|
| 386  | `/386` | 1 invader processed per frame — choppy, slow march. Grid capped at 32. |
| 486  | `/486` | 2 invaders per frame, tighter march cadence. Grid capped at 44. |
| P66  | `/P66` | Full 55-invader grid, fast march, all bomb types, 8-frame explosions. |

CPU mode also controls starfield complexity on the title screen and the extra-life
score threshold.

---

## Sound

Auto-detected from the `BLASTER` environment variable on first run. Override:

```
/SBP              Sound Blaster Pro (22050 Hz stereo; UFO/explosion panning)
/SB               Sound Blaster mono (11025 Hz)
/SPK              PC speaker (PIT-based tones)
/NOSOUND          disable all audio
/PORT:xxx         SB base port in hex (e.g. /PORT:240)
/IRQ:x            override IRQ number
/DMA:x            override DMA channel
```

Flags are session-only. Add `/SAVE` to persist them to `INVADERS.CFG`.

---

## All CLI Flags

```
INVADERS.EXE [/386|/486|/P66] [sound flags] [/SAVE] [other]
```

| Flag | Effect |
|------|--------|
| `/386` `/486` `/P66` | CPU / difficulty mode |
| `/SBP` `/SB` `/SPK` `/NOSOUND` | Sound device |
| `/PORT:xxx` `/IRQ:x` `/DMA:x` | SB hardware overrides |
| `/SAVE` | Persist current session flags to `INVADERS.CFG` |
| `/reset` | Reset config; re-prompts CPU and sound next run |
| `/resethi` | Reset `HISCORES.DAT` to defaults |
| `/resetall` | Reset both config and high scores |
| `/NOSTATS` | Suppress the exit stats screen |
| `/SOUNDTEST` | Verbose sound detection + test tones; exits without starting game |

---

## Exit Statistics Screen

Pressing ESC (or Ctrl+Break) shows a text-mode summary of the session:

- Run time, frames rendered, average and lowest FPS (with wave number)
- Gameplay: shots fired, kills, UFOs hit, waves cleared, accuracy %, shields eroded %
- Best score, extra lives earned, bombs dodged
- Sound device and hardware config
- Memory: conventional free, sprite cache KB, PCM sample KB
- Personality tags (CRACK SHOT!, ROCK SOLID, UNTOUCHABLE, etc.)

Suppress with `/NOSTATS` or persist suppression with `/NOSTATS /SAVE`.

---

## Generated Files

These are created on first run and are not committed to the repo:

| File | Contents |
|------|----------|
| `INVADERS.CFG` | Saved configuration (text, `key=value`) |
| `SPRITES.DAT` | Pre-rendered EGA sprite frames |
| `SOUNDS.DAT` | Pre-synthesized PCM samples (SB/SBP modes) |
| `HISCORES.DAT` | High score table |

Delete any of these to force regeneration on next launch.

---

## Technical Notes

- **Graphics:** EGA mode `0Dh` (320×200, 16 colours, planar). All sprites
  pre-generated at first run; gameplay is pure `memcpy` to VRAM — no runtime
  sprite math.
- **Timing:** Locked to the EGA vertical retrace (~70 Hz) via port `3DAh`.
  No floating point anywhere; all math is integer.
- **Build:** Open Watcom 2.0, medium memory model (`-ml`), `-zp1` struct packing,
  `-W3` warnings. See `ARCHITECTURE.md` for full design documentation.
