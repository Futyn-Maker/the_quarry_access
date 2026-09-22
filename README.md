[Download the latest version (the-quarry-access-windows.zip)](https://github.com/Futyn-Maker/the_quarry_access/releases/latest/download/the-quarry-access-windows.zip)

# The Quarry Access

The Quarry Access is a mod that makes [The Quarry](https://store.steampowered.com/app/1577120/The_Quarry/) playable without sight. It reads the game through your screen reader and adds sounds where the game relies on the picture: choices, quick-time events, exploration and aiming.

## Features

- Speech for all menus, settings and other interface elements
- Pause menu with clues, evidence, tarot cards and paths
- Subtitles
- Scene captions, loading and saving, notifications and alerts
- Button prompts and interruptions
- Two-option and four-option choices
- Quick-time events
- Button mashing
- Don't Breathe
- Exploration with a sound beacon and automatic walking
- Aiming in fights
- Looking around with the phone camera and the binoculars
- Notes and letters
- Descriptions of the tarot card visions
- Character fates in the epilogue
- Text on in-game monitors, such as the newspaper in the epilogue
- Credits
- Couch co-op, Movie Mode and Wolf Pack screens
- Keyboard and gamepad keys for all mod functions
- NVDA, JAWS, Narrator and other screen readers, SAPI and braille

The mod works with all languages of the game and speaks in the language the game uses.

## Installation

1. Download [the-quarry-access-windows.zip](https://github.com/Futyn-Maker/the_quarry_access/releases/latest/download/the-quarry-access-windows.zip).
2. Unpack it into the game folder, the one that contains `TheQuarry.exe`. For Steam, this is `C:\Program Files (x86)\Steam\steamapps\common\The Quarry`. If the game is in another Steam library, right-click it in Steam and choose Manage > Browse local files.
3. Start the game. After the opening videos, the mod says that it is loaded.

If the game shows "Failed to load UE4SS.dll" at start, install the [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe).

## How to Play

Use the arrow keys or the D-pad to move through menus, Enter or A to select, and Backspace or B to go back. Left and right change a setting. Escape or Start pauses the game, and in the pause menu [ and ] or LB and RB switch tabs. When a clue or a tarot card is found, Q or LB opens it. The mod reads the keys each screen offers.

Escape also stops the speech, so a screen you leave is not still being read over the one you arrive at.

The game's Accessibility settings make many mechanics easier. Choice Timer, QTE Speed and Interrupt Speed give more time. Simple QTE's accepts any direction or passes quick-time events by itself. Button Mash can ask for holding or a single press instead of mashing. Don't Breathe and Aim Assist can be set to Auto.

### Mod Keys

| Keyboard | Gamepad                  | Action                                           |
| -------- | ------------------------ | ------------------------------------------------ |
| F5       | Back + A                 | Repeat the last message                          |
| F6       | Back + X                 | Read the current screen                          |
| F7       | Back + B                 | Stop speech                                      |
| F8       | Back + Y                 | Help for the current situation                   |
| F9       | Back + RT                | Turn subtitle reading on or off                  |
| F4       | Back + RB                | Repeat the last subtitle                         |
| F3       | Back + right stick click | Switch speech between the screen reader and SAPI |
| F2       | Back + left stick click  | Switch the verbosity level                       |

On the gamepad, hold Back (View) and press the second button. The game does not see buttons pressed while Back is held.

These keys work while you walk and during fights:

| Keyboard | Gamepad     | Action                                  |
| -------- | ----------- | --------------------------------------- |
| N        | D-pad right | Next target                             |
| P        | D-pad left  | Previous target                         |
| H        | D-pad up    | Where the target is                     |
| G        | X           | Walk to the target, press again to stop |
| T        | D-pad down  | Beacon or aim sound on or off           |
| K        | RB          | Tarot card places in the list on or off |

All keys can be changed in `SMG026\Binaries\Win64\ue4ss\Mods\QuarryAccess\QuarryAccess.ini` in the game folder. The same file has the volume of the mod's sounds and other options, each described there.

### Verbosity Level

The mod has two verbosity levels, detailed and brief. F2, or Back and the left stick click, switches between them and says which one is now on. The level is kept for the next session.

Everything the game itself shows is read at either level: menus, prompts, subtitles, notifications. Brief leaves out only what the mod adds of its own — the place of an item in a list, which option a choice was settled on, whether a quick-time event or another mechanic was passed, and the words that go with a sound you already hear, such as "Fire!" and "In frame". The sounds stay.

### Two-Option Choices

The mod reads the question and both options, left and right. Hold A for the left option, or D for the right one. On the gamepad, push a stick or the D-pad left or right. Keep holding until a two-note tone plays, then release.

### Four-Option Choices

The options are in the corners of the screen, and the mod reads each with its corner. Move the mouse to a corner and click, or push a stick toward a corner and hold it until the tone. The option under the mouse or the stick is read as it lights up.

### Prompts and Interruptions

When the game shows a button prompt or an interruption, the mod reads it with its key. Press the key to act. An interruption lasts only a moment.

### Quick-Time Events

The mod says the direction and plays a tone: rising for up, falling for down, in the left or right ear for the sides. Press the direction with WASD, or push a stick or the D-pad. The tone plays again when the game starts to accept the press, and a press before that is ignored. The direction is repeated every second until the event ends.

### Button Mashing

Press the key the mod names, usually the left mouse button or A, the way the game's Button Mash setting asks: repeatedly, held or once. Rising blips mean you keep up, falling blips mean you fall behind.

### Don't Breathe

Hold the left mouse button or A to hold your breath, and keep holding while there is danger. Blips follow your breath and fall as it runs out. A rising tone with "The danger has passed" means you can release. A falling tone with "Danger" means the danger is coming back.

### Aiming

> Aiming is accessible and voiced, but it is very hard: most fights last only a few seconds. It is recommended to set Aim Assist to Auto in the game's Accessibility settings. The game then aims and optionally shoots for you.

The game has no crosshair. Your aim is the torch beam on the weapon. Move it with the mouse or the right stick. A blip sounds on the side of the target. It is higher when the target is above the beam and lower when it is below, and it gets faster as the beam gets closer. A double ping means a shot would hit, and the mod says "Fire!" when the hit is certain. Shoot with the left mouse button or the right trigger. With Aim Assist set to On, the game also pulls the aim onto a nearby target.

### Exploration

Walk with WASD or the left stick, and hold left Shift or LB to walk faster. The mouse or the right stick turns the camera. When the game offers an interaction, the mod reads it with its key, usually the left mouse button or A. A soft rising tone plays when you can walk, and a falling one when you no longer can.

When you get control of the character, the mod names the nearest place to go. N and P or left and right D-pad keys choose another one, and F6 or Back + X lists all of them with distance and direction. "Way on" is where the scene continues. You can get to the chosen place in two ways:

- Press G or X on the gamepad to walk there automatically. Press G or X again or move to stop.
- Follow the beacon. It sounds on the side of the next turn, rises as you get closer and drops an octave when the place is behind you. H or D-pad up says the distance and direction, and T or D-pad down turns the beacon off and on.

K or RB turns on hints for the tarot cards: the spots of the cards you would not pass on the way to any listed place then appear in the list as plain "Way on" places, and the choice is kept for the next start.

Many objects have no name in the game, so the mod reads their internal names, which are in English. In most cases they still make clear what the object is.

### Looking Around

Some scenes ask you to look around or to take a photo. The aim sound from the fights leads you there: move the mouse or the right stick toward it. "In frame" means the point is in view. Take the photo with the key the game names. T turns the sound off and on.

### Notes

Notes and letters are read page by page. Enter or A turns the page, and Backspace or B closes the note.

### Couch Co-op

Couch Co-op shares the playable characters between people at one console. Add a player for each of you, then give every character to somebody: the game starts only when no character is left over.

Move to a player in the list with the arrow keys, and step through the characters with the square bracket keys or the shoulder buttons. Enter or A gives the character you are on to that player, and pressing it again takes the character back. The mod says the new owner after each press, and names the owner with the character as you step through them. Quick Start shares them all out at random.

A player may be left with no characters. The game asks whether you meant it and names them, and it starts if you say yes.

During the game, whenever the next scene belongs to somebody else, the game stops and says whose turn it is. Hand over the controller or the keyboard and confirm. Everything else plays as it does alone.

## Reporting Problems

[Open an issue](https://github.com/Futyn-Maker/the_quarry_access/issues) and attach the log, `SMG026\Binaries\Win64\ue4ss\Mods\QuarryAccess\QuarryAccess.log` in the game folder. If something on screen is not read, press Ctrl+F9 while it is shown and attach the file it saves in the `dumps` folder next to the log.

If the game crashed, attach the crash dump as well. Where it is depends on the window the crash showed:

- "The SMG026 Game has crashed and will close": copy the text of that window into the issue. The dump is in `%LOCALAPPDATA%\TheQuarry\Saved\Crashes`: attach the newest `UE4CC-Windows-...` folder there, zipped; it holds `UE4Minidump.dmp`.
- "Fatal Error!": the window names the dump, a `crash_` file in `SMG026\Binaries\Win64\ue4ss` in the game folder. Attach that file.

The `UE4CC-Windows-...` folders in `Saved\Config\CrashReportClient` are made at every start of the game and hold no dump.

If the crash window says "Retry was NOT sucessful", part of the game's data on the disk is damaged. In Steam, open the game's Properties, then Installed Files, and choose "Verify integrity of game files"; reinstall the game if that finds nothing. The check does not touch the mod, which only adds files of its own.

## Development

### Prerequisites

1. Visual Studio 2022 Build Tools with the C++ workload, which includes CMake and Ninja:

    ```
    winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended"
    ```

    Visual Studio 2022 version 17.13 or newer with "Desktop development with C++" works too.

2. Rust:

    ```
    winget install --id Rustlang.Rustup
    ```

3. A GitHub account linked to an Epic Games account. UE4SS uses a private repository of Epic Games. Link the accounts on the [Connections page](https://www.epicgames.com/account/connections) of your Epic Games account, then accept the invitation to the EpicGames organization that GitHub sends by email. Git asks you to sign in to GitHub the first time it downloads that repository.

### Building from Source

Clone the repository into a short path. It must be 78 characters or shorter, because UE4SS has long file names. Then run the build:

```
git clone https://github.com/Futyn-Maker/the_quarry_access.git
scripts\package.cmd
```

The first run downloads the dependencies and builds everything, which takes about 10 minutes. Later runs rebuild only what changed.

The result is in `dist\TheQuarryAccess-<version>`. Copy its contents into the game folder, as in the installation. `dist\TheQuarryAccess-<version>-symbols` holds the debug symbols of the same build.

Before a pull request, run `scripts\format.cmd` to format the C++ code. It needs the "C++ Clang tools for Windows" component of Visual Studio.

### Translation

The mod's own messages are in `mod\lang`, one file per game language: `ru_RU.ini`, `de_DE.ini` and so on. `en_US.ini` is the reference. The descriptions of the tarot visions are in `mod\lang\tarot`, in the same way.

Each line is `key=value`. Translate the value and keep the key. Keep `{0}`, `{1}` and so on: the mod puts names and keys there. Leave "The Quarry Access" as it is.

To try a change without building, edit the same file in the game folder, in `SMG026\Binaries\Win64\ue4ss\Mods\QuarryAccess\lang`, and restart the game. The mod uses the file of the game's language. `Language` in `QuarryAccess.ini` can choose another one.

Before a pull request, check from the repository folder that every file has the same keys as the English one:

```
powershell -ExecutionPolicy Bypass -File scripts\check-lang.ps1
```

## License

MIT. The mod is built on [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) (MIT) and uses [Prism](https://github.com/ethindp/prism) (MPL-2.0) for speech. Their licenses are included in the release.
