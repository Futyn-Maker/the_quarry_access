# QuarryAccess architecture

QuarryAccess is a UE4SS C++ mod (`Mods\QuarryAccess\dlls\main.dll`) that voices The Quarry's
menus, prompts, subtitles and mechanics through [Tolk](https://github.com/dkager/tolk)
(NVDA, JAWS, other readers, SAPI fallback). Speech and braille go through one call,
`Tolk_Output`.

## Threads

UE4SS runs `CppUserModBase::on_update()` and key events on its own event-loop thread.
Every access to a game object therefore happens inside an engine callback:

- `core/GameThread` registers an engine-tick post callback (`Hook::RegisterEngineTickPostCallback`).
  `Post()` enqueues work from any thread; the tick drains the queue and runs the registered pollers.
- Blueprint and native hooks fire on the game thread already.
- `StaticConstructObject` callbacks may fire on loading threads; they only record the object and
  the notification is delivered on the next tick.

## Modules

| Module                 | Role                                                                                                                                                                                                                            |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `core/Log`             | `[QuarryAccess]` lines into `UE4SS.log` plus `Mods\QuarryAccess\QuarryAccess.log`; every utterance is logged as `SAY <policy> "<text>"`.                                                                                        |
| `core/Config`          | Reads `QuarryAccess.ini` (own INI parser) into typed settings.                                                                                                                                                                  |
| `core/ObjectUtil`      | Name-based reflection: properties, functions (`ProcessEvent`), widget tree walking, visibility, cached well-known objects. No hard-coded offsets.                                                                               |
| `core/ParamReader`     | Reads hooked function parameters by name from the `FFrame` locals.                                                                                                                                                              |
| `speech/TolkBridge`    | Dynamic binding to `Tolk.dll`.                                                                                                                                                                                                  |
| `speech/Speech`        | Policies: `Focus` (interrupts only what the player has overtaken with a new key press), `Announce` (spoken after what the reader is already saying), `Now` (hard interrupt), `Stop`, `Repeat`.                                  |
| `locale/Locale`        | Mod strings from `lang\<code>.ini` with English fallback.                                                                                                                                                                       |
| `locale/GameText`      | Resolves the game's `FLocaleString` keys via `UIStaticsQuarry::FormatLocaleString`; reports the game's text locale.                                                                                                             |
| `input/InputNames`     | Action name → spoken key name for the active control scheme (`APlayerControllerSMGBase::CurrentControlScheme`, `GetKeysFromActionMapping`, `Key_GetDisplayName`, curated names in the language tables); watches keyboard, mouse and gamepad activity so speech knows what the player asked for. |
| `hooks/HookDispatcher` | One `ProcessLocalScriptFunction` post/pre callback routed by (declaring class, function name) with per-`UFunction` caches; native hooks via `UObjectGlobals::RegisterHook`; object-construction listeners; the function tracer. |
| `watch/Watchers`       | Pollers: HUD `*WidgetInstance` pointers on the player controller's HUD components, text-block diffs, current screen and `LastFocusedWidget`.                                                                                    |
| `features/*`           | One feature per game area, each installing hooks/pollers and contributing to the read-screen (F6) and help (F8) readouts: `Menus` (screens, focus, values, prompts), `Pause` (the pause tab bar), `Hud` (captions, notifications, alerts, loading/saving), `Subtitles` (subtitle lines as they appear). |
| `hotkeys/Hotkeys`      | Keyboard hotkeys through UE4SS key events; gamepad chord polled with `IsInputKeyDown`.                                                                                                                                          |
| `diag/Diagnostics`     | Screen dump (`dumps\screen-*.txt`), tracer toggle, log level cycling, F6/F8 composition.                                                                                                                                        |

## Detection strategy

HUD show/hide is native in this game (the `UActionHUD*` classes have no reflected functions), so
detection combines three sources:

1. Blueprint function executions (focus changes, `Show`/`Hide`, `SetPrompt`, `SetLabel`, ...).
2. Native function hooks (`UObjectGlobals::RegisterHook`) for functions the game invokes through the reflection system.
3. Per-frame polling of widget instance pointers and text values, with change detection and dedupe.

Polling is the authoritative path; hooks make announcements earlier and cleaner.

## Build

`scripts\build.cmd` configures with CMake/Ninja (`Game__Shipping__Win64`) against the RE-UE4SS source tree
(`third_party/RE-UE4SS` submodule, or `QA_UE4SS_SOURCE_DIR`) and builds `main.dll`.
`scripts\deploy.ps1` copies the DLL, `mod\` files and the Tolk runtime into the game.
