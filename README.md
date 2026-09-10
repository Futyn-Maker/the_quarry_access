# The Quarry Access

A mod that makes _The Quarry_ (Supermassive Games, 2022, PC/Steam) playable by blind players.
It reads the game's screens through your screen reader: NVDA, JAWS and other Tolk-supported
readers, with a SAPI fallback when no screen reader is running. Speech and braille are both
supported. The menus, settings and pause menu are covered, with the focused item, its description
and the available key prompts spoken as you move; captions, notifications, alerts, subtitles,
button prompts, choices, quick-time events, button mashes and Don't Breathe prompts that the
game shows during play are read as they appear, with short tones for what has to be fast.
While exploring, the things the scene lets the player walk to are listed with their distance
and direction, and a beacon leads to the chosen one; notes are read page by page.

The mod is built on [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) and [Tolk](https://github.com/dkager/tolk).
It never changes the game's own settings: everything it says follows what the game displays,
including the accessibility and subtitle options you choose in the game.

See `CHANGELOG.md` for what each version includes.

## Installation (players)

Release packages contain everything needed (UE4SS with the configuration for The Quarry,
the Tolk runtime and the mod) plus an `install.cmd`. Developers can use the setup below.

## Hotkeys

| Keyboard | Gamepad (hold Back/Select) | Action                                  |
| -------- | -------------------------- | --------------------------------------- |
| F5       | D-pad up                   | Repeat the last message                 |
| F6       | D-pad right                | Read the current screen                 |
| F7       | D-pad down                 | Stop speech                             |
| F8       | D-pad left                 | Help for the current situation          |
| F4       | Left stick click           | Repeat the last subtitle line           |
| F9       | Right stick click          | Turn subtitle reading on or off         |
| Ctrl+F9  |                            | Write a screen dump for bug reports     |
| Ctrl+F10 |                            | Toggle the function tracer (developers) |
| Ctrl+F11 |                            | Cycle the log level                     |

These guide the exploration while the character walks freely. On the gamepad they need no
chord there, because the game leaves those buttons unused while you walk.

| Keyboard | Gamepad     | Action                                    |
| -------- | ----------- | ----------------------------------------- |
| N        | D-pad right | Next thing to walk to                     |
| P        | D-pad left  | Previous thing to walk to                 |
| H        | D-pad up    | Where the target is now                   |
| G        | X           | Walk there, and press again to stop       |
| T        | D-pad down  | Beacon on or off                          |

The Back chord above also reaches the first four from anywhere: right bumper for the next
thing, left bumper for the previous, X for where it is, Y for the beacon.

All keys can be changed in `Mods\QuarryAccess\QuarryAccess.ini`.

Short tones complement speech where speed matters: a rising two-note tone marks a choice
committing or a challenge won, a falling one a challenge lost; a quick-time event plays its
direction (notes climbing for up, falling for down, a note in the left or the right ear for
the sides) at the same moment it is spoken; blips of rising or falling pitch follow the
button-mash ring and the breath bars. Their volume is set in the same file, and 0 turns them off.

## Languages

The mod follows the game's text language. Its own messages exist for all 20 game languages;
English and Russian are maintained by the author, the others were machine-assisted and
corrections are very welcome (edit `mod\lang\<code>.ini` and open a pull request).

## Developer setup

Requirements: Windows, Visual Studio 2022 Build Tools (C++ workload, MSVC 14.43+, Windows SDK,
LLVM clang-format), CMake 3.22+, Ninja, Rust (for UE4SS), Git, and a GitHub account linked to
Epic Games (UE4SS's `UEPseudo` submodule is only visible to linked accounts).

```cmd
git clone --recurse-submodules https://github.com/Futyn-Maker/the_quarry_access.git
cd the_quarry_access
scripts\build.cmd
powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1
```

`build.cmd` uses `D:\QuarryTools\RE-UE4SS` when the `QA_UE4SS_SOURCE_DIR` environment variable is
not set; set it to the submodule path (`third_party\RE-UE4SS`) or any RE-UE4SS checkout.
`scripts\format.cmd` formats the sources with clang-format; run it before committing.
The game needs UE4SS installed with the official "The Quarry" custom game config
(`UE4SS_Signatures\StaticConstructObject.lua` and `VTableLayout.ini` from the RE-UE4SS repository).

Documentation: `docs/ARCHITECTURE.md`, `docs/TESTING.md`, `docs/HOOKS.md`.

## License

MIT (see `LICENSE`). Tolk is LGPL-3.0; UE4SS is MIT. Their licenses apply to the redistributed binaries.
