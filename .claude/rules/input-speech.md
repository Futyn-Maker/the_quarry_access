---
paths:
    - "src/input/**"
    - "src/hotkeys/**"
    - "src/speech/**"
---

# Input, hotkeys, speech

- Keyboard hotkeys go through a `WH_KEYBOARD_LL` hook on the mod's own thread, installed after the screen reader's so that it runs first (an NVDA add-on takes F3 otherwise). It takes the mod's keys only while the game is in front, ignores injected events and counts a held key once. The letters N, P, H, G and T are taken only during exploration, a fight or a look-around (`keyhook::SetContext`); otherwise they reach the game and its text fields.
- The game reads gamepads only through XInput, from its own `XINPUT1_3.dll` import (ordinals 2 and 3). The mod swaps that import slot: it reads the pad there (the player controller's `IsInputKeyDown` stops answering while a menu owns the input), hides everything pressed while Back is held until release, so a chord never reaches the game, and pushes the left stick for a walk. The game rescans pads only after `WM_DEVICECHANGE`.
- `CurrentControlScheme` has no setter and follows the last device used; synthetic keys flip every prompt to the keyboard. Help is worded for the device of the last mod hotkey.
- Key names come from the game's remap rows first (`Default__UISettingsSMG026.KeyBindingSettingsData`), then from `GetKeysFromActionMapping` on the screen on display, then from `Default__InputSettings` by reflection, never from a cached widget. `key <action>` in the command file shows which source answered.
- Keys free in `DefaultInput.ini`: F1 to F9 (bound only to the placeholder action `IgnoreForRemap`), B, F, G, H, I, J, K, L, M, N, O, P, T, U. On the pad, Back is bound only to debug actions, and the D-pad and X are unused while the character walks.
- Speech: Tolk runs with `Tolk_TrySAPI(false)`. SAPI runs on the mod's own thread, with its own COM apartment and a voice for the game's language, when there is no screen reader or when `[Speech] PreferSapi=1` (toggled with F3). NVDA cannot report whether it is speaking, so the reader does all the queueing. Only the player's input interrupts (`input::MsSinceInput`).
- Tones play through the sound card at `[Sounds] Volume`, 80 by default: quieter cues were inaudible under the game.
