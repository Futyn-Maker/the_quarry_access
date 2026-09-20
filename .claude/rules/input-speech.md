---
paths:
    - "src/input/**"
    - "src/hotkeys/**"
    - "src/speech/**"
---

# Input, hotkeys, speech

- Keyboard hotkeys go through a `WH_KEYBOARD_LL` hook on the mod's own thread, installed after the screen reader's so that it runs first (an NVDA add-on takes F3 otherwise). It takes the mod's keys only while the game is in front, ignores injected events and counts a held key once. The letters N, P, H, G and T are taken only during exploration, a fight or a look-around (`keyhook::SetContext`); otherwise they reach the game and its text fields.
- The game reads gamepads only through XInput, from its own `XINPUT1_3.dll` import (ordinals 2 and 3). The mod swaps that import slot: it reads the pad there (the player controller's `IsInputKeyDown` stops answering while a menu owns the input), hides everything pressed while Back is held until release, so a chord never reaches the game, and pushes the left stick for a walk. The game rescans pads only after `WM_DEVICECHANGE`.
- Escape is watched by the hook and not taken (`keyhook::Watch`): the press stops what is being said and the game still gets the key, which is its `Pause` and `RemapCaptureCancel`. It has to be the hook and not a poller: the hook runs before the game is given the key, so the stop lands before the new screen is announced, while a polled key could arrive afterwards and silence the announcement instead.
- `CurrentControlScheme` has no setter and follows the last device used; synthetic keys flip every prompt to the keyboard. Help is worded for the device of the last mod hotkey.
- Key names come from the game's remap rows first (`Default__UISettingsSMG026.KeyBindingSettingsData`), then from `GetKeysFromActionMapping` on the screen on display, then from `Default__InputSettings` by reflection, never from a cached widget. `key <action>` in the command file shows which source answered.
- Keys free in `DefaultInput.ini`: F1 to F9 (bound only to the placeholder action `IgnoreForRemap`), B, F, G, H, I, J, K, L, M, N, O, P, T, U. On the pad, Back is bound only to debug actions, and the D-pad and X are unused while the character walks.
- Speech goes through Prism (`prism.dll`, loaded from the mod's own `dlls\` folder by its full path so the instance is nobody else's, with `prism_registry_create` rather than the shared `acquire`). `speech/Outputs` keeps one thread that owns both backends and every call into the library: a backend instance is not thread-safe, and the COM proxies behind the Windows backends answer only in the apartment that made them, so a call from the game thread to a backend created elsewhere would fail. The rest of the mod queues lines and never waits.
- The screen reader is whichever backend in Prism's priority-ordered registry is both a reader and running (`speech/Outputs` keeps the list; SAPI and OneCore are not readers). The fallback voice is `PRISM_BACKEND_SAPI` by name, never "the best backend": the player's Windows speech settings are what they have already set up. SAPI speaks when there is no reader or when `[Speech] PreferSapi=1` (toggled with F3), and the reader still gets the braille while SAPI speaks.
- Prism never reconnects a backend: when a screen reader closes, its backend is dead for good and only a new one helps. So the mod drops and re-picks on a "lost" error (`prism::Lost`) and when Prism's watch thread reports a backend coming or going, which is also how a screen reader started mid-game is noticed. Never probe the readers per line: `prism_backend_get_features` does live COM and RPC lookups.
- Prism reports every SAPI voice as `en-us`: it reads the token's own `Language` value, and SAPI keeps that under the token's `Attributes` key. `speech/SapiVoices` reads the attribute itself and the two lists are matched by voice name. Check this against a fresh Prism before trusting its language.
- NVDA cannot report whether it is speaking, so the reader does all the queueing. Only the player's input interrupts (`input::MsSinceInput`).
- Tones play through the sound card at `[Sounds] Volume`, 80 by default: quieter cues were inaudible under the game.
