# Changelog

## 0.6.0

- Quick-time events: the direction the arrow asks for is said the moment the event appears, with the key when the game shows a key cap, and played as a cue at the same time (notes climbing for up, falling for down, a note in the left or the right ear for the sides); both are repeated every second while the arrow stays on screen; a rising tone marks a hit, a falling one a miss.
- The cues play louder by default (`[Sounds] Volume` 80).
- Button mash: the button is said the way the game's Button Mash setting wants it pressed (mashed, held, tapped, or automatic); blips of rising and falling pitch follow the ring as it fills and empties; the outcome is said and played.
- Don't Breathe: the prompt to hold the breath and the prompt to release it are read as the game shows them, with the key; blips follow the breath bars while the breath is held; the outcome is said and played.
- F6 reads the event, the button or the prompt on screen; F8 names the keys of a quick-time event, the button to mash and the key to hold.

## 0.5.0

- Choices: a choice is read as a timed or plain choice with its question, each option with its place and its two labels, and the option of not responding when the choice offers it; the key hints are read when the game shows them; the option the player is holding is announced as the game highlights it, the option chosen once the key is released; the seconds left are read once the game shows them.
- Four-way choices are read option by option with their places; the countdown that warns of a coming choice is read when it appears and when its wording changes; the timer bar announces itself.
- Button prompts (interactions, interruptions, use locations, combat) are read with their label and key as they appear; prompts for the mouse or the stick name the device.
- A short tone marks the moment a choice commits (`[Sounds] Volume`), so the key can be released; the chosen option is spoken once the key is released.
- The subtitle reading switch (F9) is saved to the ini, so the game starts the way it was left.
- F6 reads the choice or the prompts on screen; F8 names the keys of a choice.

## 0.4.0

- Subtitles: each line is read once as it appears, exactly as the game displays it under its subtitle settings (character names, hyphens and closed captions included, nothing when subtitles are off); a subtitle wrapped over two lines is read as one; lines are spoken after whatever the reader is already saying and are never cut short or dropped; F6 reads the lines on screen.
- Hotkeys: F9 turns subtitle reading off and on during play (`[Subtitles] Read` sets the start value), F4 repeats the last subtitle line even after it has gone, until the main menu is back; on a gamepad, Back with the right or the left stick click.

## 0.3.0

- Pause menu: opening it announces that the game is paused, the selected tab and its place among the tabs; switching tabs reads the new tab, its objective line, the selected entry and the prompts; clues, evidence, tarot cards and tutorials are read with their descriptions.
- Popups and dialogs read their message after the title; rewind and couch co-op screens are covered.
- Sections opened inside a screen are read like a screen; a control placed in a titled row (the director's chair switches) takes the row's title and position; the character carousel reads the character it shows.
- HUD: scene captions, notifications with their hot-link prompt, alerts and act titles are read when they appear; loading and saving are announced.
- Read-screen (F6) reads everything a screen shows when nothing on it is selected.

## 0.2.0

- Menus: the focused item is announced with its label, control type, state, position and description line; a screen that opens is read as title, selection, then prompt bar, and the prompt line always closes the readout.
- Settings: selector and slider rows announce their row name, changing a value speaks the new value, and key-binding and tutorial rows are read like every other row.
- Speech only interrupts what the player has overtaken with their own key, button or mouse input; anything the game says on its own waits its turn.
- Read-screen (F6) and help (F8) include the menu state and the available prompts.
- Developer command file `Mods\QuarryAccess\command.txt` (trace, dump, log level, say).

## 0.1.0

- UE4SS C++ mod foundation with Tolk speech (NVDA, JAWS, SAPI fallback; speech and braille).
- Configuration file `QuarryAccess.ini` and string tables for all 20 game languages.
- Hotkeys: F5 repeat, F6 read screen, F7 stop, F8 help; gamepad chord Back + D-pad.
- Diagnostics: Ctrl+F9 screen dump, Ctrl+F10 function tracer, Ctrl+F11 log level.
- Game-thread pump, hook dispatcher, HUD/text/focus watchers, game-string resolver, key-name resolver.
