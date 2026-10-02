# Mushoku Tensei 64

An unofficial fan game based on *Mushoku Tensei: Jobless Reincarnation*. It is
a native Nintendo 64 game, not a web or PC game made to look like one. The
build produces a real cartridge ROM (`mushoku64.z64`). It is C code compiled
for the N64's VR4300 CPU, and its 3D graphics are drawn by the console's own
RSP and RDP chips using [libdragon](https://github.com/DragonMinded/libdragon),
the open-source N64 SDK.

Everything in it was made for this project: the code, the low-poly models,
the procedurally generated textures, the chiptune soundtrack and sound
effects, and the dialogue.

## What makes it an N64 game

- **CPU:** game logic in C, compiled with the libdragon MIPS toolchain for
  the 93.75 MHz VR4300.
- **Graphics:** 320×240, 16-bit color, triple buffered, with RDP
  anti-aliasing and dedithering. 3D goes through libdragon's OpenGL 1.1
  implementation: vertices are transformed and lit by microcode on the RSP,
  and triangles are rasterized, Z-buffered, fogged and textured (bilinear
  filtering) by the RDP. Static geometry is pre-recorded into RSP command
  blocks, which work like N64 display lists.
- **Textures:** small 32×32 detail maps tinted by vertex color, the classic
  trick for fitting in the 4 KB of texture memory.
- **Audio:** a 22 kHz synthesizer with no floating-point math, fed by the
  audio interface interrupt. It plays the score (written in MML) and the
  sound effects.
- **Saves:** cartridge 4 Kbit EEPROM (checksummed).
- **Rumble:** Rumble Pak support.
- **Memory:** runs on a stock 4 MB console. The Expansion Pak is not needed
  (the heap peaks around 1.9 MB of the 3.4 MB available).
- **Region:** region-free ROM header. Boots through libdragon's open-source
  IPL3.

### Where it runs

- **Emulators:** use one that emulates the RSP at low level and runs custom
  microcode. It was developed and tested in
  [ares](https://ares-emu.net) (both with and without the Expansion Pak).
  Other low-level emulators such as Gopher64 or simple64 should work but have
  not been tested. HLE graphics plugins (for example Project64's or
  mupen64plus's defaults) do not support libdragon's microcode and will not
  run it.
- **Real hardware:** copy `mushoku64.z64` to a flash cartridge (EverDrive 64,
  SummerCart64, 64drive, ...) and set the save type to EEPROM 4K if the cart
  asks. This build has not been tried on a physical console yet.

## The story

Five short chapters following Rudeus from Buena Village to the Demon
Continent:

1. **A New Life**: Mother, the new tutor Roxy, and the first lessons: Water
   Ball, Fire Ball and Stone Cannon.
2. **Sylphiette**: the hill, the bullies and a new friend. Learn Healing.
3. **Beyond the Door**: Roxy's final exam. Rudeus has to step past the
   village gate, then call down a Cumulonimbus on the plateau.
4. **Fittoa Forest**: Paul's request. Wolves, boars and the Great Boar.
5. **The Mana Calamity**: shatter the crystals, destroy the Mana Core, and
   wake up somewhere far from home.

The game autosaves at the start of every chapter. Choose **Continue** on the
title screen to resume.

## Controls

| Button | Action |
| --- | --- |
| Control Stick | Move (tilt further to run) |
| A | Jump / talk / read signs / advance dialogue |
| B | Staff strike |
| C-Left / C-Down / C-Right | Water Ball / Fire Ball / Stone Cannon (hold to charge, release to cast) |
| C-Up | Healing |
| Z | Hold to lock on to the nearest enemy (with none nearby, recenter the camera) |
| R | Recenter the camera |
| D-Pad | Rotate / zoom the camera |
| Start | Pause (shows learned spells and controls) |

Charging a spell longer makes it stronger (three levels) and costs more MP.
MP refills over time while you are not charging.

## Building

You need:

- The libdragon toolchain (`mips64-elf-gcc`) and libdragon's **preview**
  branch, which provides the OpenGL implementation. Set `N64_INST` to the
  install prefix, as libdragon's own instructions describe. It was built
  with GCC 16.2 and libdragon preview `39d0d60` (2026-09-15).
- GNU make.

```sh
export N64_INST=/path/to/libdragon/install
make
```

The build converts `assets/` (PNG textures and TTF fonts) into
`filesystem/` with libdragon's `mksprite` and `mkfont`, packs that into the
ROM's DragonFS, and writes `mushoku64.z64`.

The source art is already committed. To regenerate it you need Python 3 with
Pillow, plus a Japanese font (IPA Gothic) for the title logo:

```sh
python3 tools/gen_assets.py
```

### A note on libdragon

libdragon's GL pipeline records each vertex-loader command into a slot of
fixed size, but it fills only the instructions it needs. The RSP copies the
whole slot into its instruction memory and runs it. Memory for command blocks
is not cleared, so once a freed block's memory was reused, leftover words ran
as RSP code. The result was a crash on every map change. The Makefile links
with `--wrap=malloc_uncached` so that block memory comes back zeroed (see
`__wrap_malloc_uncached` in `src/gfx.c`). No patch to libdragon is needed.

## Project layout

```
src/main.c        boot, main loop, title / pause / game over / credits, fades, rumble
src/gfx.c         textures, mesh builder (batched indexed meshes, baked lighting), camera, 2D helpers
src/world.c       heightmap terrain, props, sky, water, weather, collision for the four maps
src/models.c      characters (rigid segments) and creatures
src/player.c      movement, staff, spells, lock-on, camera
src/combat.c      enemies and their AI, projectiles, particles
src/story.c       chapters, NPCs, dialogue, minigames, bosses
src/ui.c          dialogue box, HUD, banners
src/audio.c       synthesizer, MML songs, sound effects
src/save.c        EEPROM saves
src/test_input.h  scripted controller input for automated testing
tools/gen_assets.py  procedural texture / logo generator
assets/           source PNGs and fonts
```

## Testing

Two compile-time switches turn a build into a test harness. They print
progress to the emulator's debug log (ISViewer).

- `make EXTRA_CFLAGS=-DTEST_STEP=<n>` boots straight into story step `n`
  (see `story_step_t` in `src/game.h`).
- `-DTEST_INPUT=<script>` feeds scripted controller input from
  `src/test_input.h`:

  | Script | Use with | Covers |
  | --- | --- | --- |
  | 1 | `TEST_STEP=1` | Chapter 1 |
  | 4 | `TEST_STEP=7` | Chapters 2 and 3 |
  | 6 | `TEST_STEP=12` | Autopilot from the Cumulonimbus exam to the credits |
  | 7 | no `TEST_STEP` | Front end: new game, pause, game over, quit, continue |
  | 5 | `TEST_STEP=1` | Every map transition, twice |
  | 2 | `TEST_STEP=15` | Forest combat |
  | 8 | `TEST_STEP=1` | Character close-ups |

- `-DPERF_LOG` prints CPU and RSP timings per frame section.

For example:

```sh
make EXTRA_CFLAGS="-DTEST_STEP=12 -DTEST_INPUT=6"
```

## Legal

This is a non-commercial fan project. It is not affiliated with or endorsed
by Rifujin na Magonote, the publishers of *Mushoku Tensei*, or Nintendo.
*Mushoku Tensei* and its characters belong to their respective owners.
Nintendo 64 is a trademark of Nintendo.

The game's code, models, textures and music are original. The DejaVu fonts
are distributed under their own license (`assets/fonts/LICENSE-DejaVu.txt`).
