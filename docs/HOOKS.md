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

| Declaring class                                                                                                                                       | Function | Feature | Meaning                                                    |
| ----------------------------------------------------------------------------------------------------------------------------------------------------- | -------- | ------- | ---------------------------------------------------------- |
| `MenuBaseWidget_C`, `PopupScreenBaseWidget_C`, `CouchCo-opHandover_C`, `PauseTabCollectablesBase_C`, `PauseTabRelationship_C`, `RewindPause_C`, `RewindUnlocked_C` | `Show`   | Menus   | A screen opened; starts the arrival readout. Each class that overrides `Show` declares its own function and is routed on its own. |

## Pollers

| Poller                                                                                                                                    | Feature     | Meaning                                                                                       |
| ----------------------------------------------------------------------------------------------------------------------------------------- | ----------- | --------------------------------------------------------------------------------------------- |
| Focus watcher: `bIsHighlighted` edges of `UIInteractableWidgetBaseSMG026` controls and `LastFocusedWidget` of `SMGUIWidget` screens, kept to screens whose `IsCurrentScreen()` is true | Menus       | Current screen and focused control.                                                           |
| `menus.arrival`                                                                                                                           | Menus       | Reads a newly opened screen as title, selection, prompts once it has settled.                 |
| `menus.prompts`                                                                                                                           | Menus       | Re-reads the prompt bar when it changes, after the selection has stopped moving.              |
| `menus.value`                                                                                                                             | Menus       | Speaks a changed value of the focused selector or slider with its description.                |
| `pause`: `bIsActive` and `SelectedTab` of the visible `PauseTabSystemSMG026`                                                              | Pause       | The pause menu opening and its tab changing; both re-read the screen with the tab as heading. |
| `hud.text`: text of the HUD elements the HUD instance watcher reported                                                                    | Hud         | Speaks a caption, notification, alert or act title once its text has settled.                 |
| Text watcher                                                                                                                              | Watchers    | Diffs registered text blocks.                                                                 |
| HUD instance watcher (`*Instance` pointers, strong and weak, on the HUD components)                                                       | Watchers    | Appearance of HUD widgets: loading screen, saving icon, scene details, notifications, alerts, act display; also the screen readout and dumps. |
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
| `TypeWriterTextBlockSMG026::GetText`                | The full text of a message that is typed out.        |
| `PlayerController::IsInputKeyDown`                  | The gamepad hotkey chord.                            |
