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

## Deferred (not tested this pass)

- [ ] Dead `ufo_interval` / `s_ufo_frame_timer` fields cleanup
- [ ] Bomb/bullet byte-alignment cosmetic offset (`& ~7` blit vs unaligned hitbox)
