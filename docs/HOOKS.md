# Hook routing table

This file lists the game functions the mod listens to and the state it polls. Blueprint functions
are matched by the class that declares them (subclasses inherit the match); native functions are
hooked by full name. Every hook has a polling counterpart, so announcements do not depend on a
hook firing.

## Global callbacks

| Callback                                                               | Used for                                          |
| ---------------------------------------------------------------------- | ------------------------------------------------- |
| `Hook::RegisterEngineTickPostCallback`                                 | Game-thread pump and pollers.                     |
| `Hook::RegisterProcessLocalScriptFunctionPostCallback` / `PreCallback` | Routing of Blueprint function executions.         |
| `Hook::RegisterStaticConstructObjectPostCallback`                      | Object-construction listeners (widget discovery). |
| `Hook::RegisterLoadMapPostCallback`                                    | Cache reset on map change.                        |

## Blueprint functions

| Declaring class           | Function | Feature | Meaning                                                        |
| ------------------------- | -------- | ------- | -------------------------------------------------------------- |
| `MenuBaseWidget_C`        | `Show`   | Menus   | A frontend or pause screen opened; starts the arrival readout. |
| `PopupScreenBaseWidget_C` | `Show`   | Menus   | A popup opened; starts the arrival readout.                    |

## Pollers

| Poller                                                                                                                                    | Feature     | Meaning                                                                                       |
| ----------------------------------------------------------------------------------------------------------------------------------------- | ----------- | --------------------------------------------------------------------------------------------- |
| Focus watcher: `bIsHighlighted` edges of `UIInteractableWidgetBaseSMG026` controls and `LastFocusedWidget` of `SMGUIWidget` screens, kept to screens whose `IsCurrentScreen()` is true | Menus       | Current screen and focused control.                                                           |
| `menus.arrival`                                                                                                                           | Menus       | Reads a newly opened screen as title, selection, prompts once it has settled.                 |
| `menus.prompts`                                                                                                                           | Menus       | Re-reads the prompt bar when it changes, after the selection has stopped moving.              |
| `menus.value`                                                                                                                             | Menus       | Speaks a changed value of the focused selector or slider with its description.                |
| Text watcher                                                                                                                              | Watchers    | Diffs registered text blocks.                                                                 |
| HUD instance watcher (`*WidgetInstance` pointers on the HUD components)                                                                   | Watchers    | Live HUD widgets for the screen readout and dumps.                                            |
| `input.activity`                                                                                                                          | Speech      | Keyboard, mouse and gamepad activity, so only the player's own input interrupts speech.       |
| `speech`                                                                                                                                  | Speech      | Drains the bounded queue.                                                                     |
| `hotkeys`                                                                                                                                 | Hotkeys     | Keyboard hotkeys and the gamepad chord.                                                       |
| `commandfile`                                                                                                                             | Diagnostics | `Mods\QuarryAccess\command.txt` developer commands.                                           |

## Native functions called

The mod hooks no native game functions. It calls these through `ProcessEvent`:

| Function                                            | Used for                                             |
| --------------------------------------------------- | ---------------------------------------------------- |
| `UIStaticsQuarry::FormatLocaleString`               | Resolving the game's locale keys to display text.    |
| `UIStaticsQuarry::GetCurrentLocaleString`           | The game's text language.                            |
| `SMGUIWidget::IsCurrentScreen`                      | Which loaded screens are actually on display.        |
| `SMGUIUserWidgetBase::GetKeysFromActionMapping`     | Keys bound to an input action.                       |
| `KismetInputLibrary::Key_GetDisplayName`            | Engine display name of a key.                        |
| `MenuBarBaseSMG026::GetUnlocalisedMenuContext`      | The description line of the bottom menu bar.         |
| `PlayerController::IsInputKeyDown`                  | The gamepad hotkey chord.                            |
