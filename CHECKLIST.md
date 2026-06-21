# Test Checklist

## Dev Environment Setup

- [ ] Clone repo to local Mac
- [ ] Download Open Watcom 2.0 C DOS installer, install into `../watcom/` (or set `WATCOM_DIR`)
- [ ] `chmod +x compile.sh run.sh`
- [ ] `./compile.sh` — builds `INVADERS.EXE` inside DOSBox-X without errors
- [ ] `./run.sh` — game launches in DOSBox-X

## Bug Fix Verification (commit 060ffb2)

- [ ] **UFO explosion position** — shoot the UFO; explosion flash appears at the hit location (not off the left edge of the screen)
- [ ] **Bullet vs shields** — fire one shot into a bunker; bullet stops, bunker erodes a chunk, invader behind it survives
- [ ] **Bomb vs shields** — watch a bomb hit a bunker; bomb stops at the bunker surface and does not reach the player
- [ ] **Bunker dirty rects** — no persistent black holes in bunkers after projectile contact; surface repaints cleanly
- [ ] **Title screen audio** — attract theme plays on PC speaker (or SB melody); pressing fire stops it immediately
- [ ] **SBP sample rate** — run `INVADERS.EXE /SBP /SAVE`, quit, relaunch; SBP runs at 22050 Hz with stereo panning audible on UFO/explosions

## Gameplay Smoke Test

- [ ] Title screen displays correctly (invader sprites, high score table, attract animation)
- [ ] Player moves left/right and fires
- [ ] Invader grid marches and descends; speed increases as invaders die
- [ ] Bombs fall from invaders and kill the player
- [ ] Extra life awarded at correct score threshold
- [ ] Wave clears and next wave starts
- [ ] UFO appears and scores correctly (100/200/300/800 based on shot count)
- [ ] Game over screen displays; high score entry works
- [ ] High score persists across restarts (`HISCORES.DAT`)

## CLI Flags

- [ ] `/386` `/486` `/P66` — selects correct difficulty/speed mode
- [ ] `/SPK` `/SB` `/SBP` `/NOSOUND` — sets sound device
- [ ] `/SAVE` — saves config to `INVADERS.CFG`; settings persist on relaunch
- [ ] `/reset` — resets config; `/resethi` — resets high scores; `/resetall` — both
- [ ] `/SOUNDTEST` — runs in text mode (no EGA), plays test tones, exits

## Runtime Statistics (STATS.C/STATS.H)

- [ ] **ESC quits** — pressing ESC from the title screen and mid-game returns to DOS
- [ ] **Exit stats screen** — appears on quit (both ESC and Ctrl+Break); box renders cleanly with CP437 borders, values right-aligned
- [ ] **Session** — run time HH:MM:SS, frames rendered, average FPS, lowest FPS with wave number all plausible
- [ ] **Gameplay** — shots/kills/UFOs/waves/accuracy %/shields eroded %/best score/extra lives match what was played
- [ ] **Sound line** — shows device + `220h 5 1`, or `N/A` for PC Speaker / Disabled
- [ ] **Memory** — conventional free non-zero; sprite cache + PCM sizes sane
- [ ] **Performance** — CPU mode label, vsync misses, sprite cache hits, march events
- [ ] **Personality tags** — appear when conditions met (CRACK SHOT!, ROCK SOLID, HOOKED!, etc.)
- [ ] **`/NOSTATS`** — suppresses the stats screen
- [ ] **`SHOW_STATS=0`** — persists suppression across launches (`/NOSTATS /SAVE` then relaunch)
- [ ] **Redirection** — `INVADERS.EXE > OUT.TXT`, quit, OUT.TXT shows plain-ASCII layout (no garbled box glyphs)
- [ ] **Gameplay unaffected** — instrumentation hooks don't change timing or behavior

## Deferred (not tested this pass)

- [ ] Dead `ufo_interval` / `s_ufo_frame_timer` fields cleanup
- [ ] Bomb/bullet byte-alignment cosmetic offset (`& ~7` blit vs unaligned hitbox)
