# Changelog

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
