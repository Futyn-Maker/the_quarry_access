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
| `Hook::RegisterLoadMapPostCallback`                                    | Cache reset on map change; the last subtitle line is forgotten. |

## Blueprint functions

| Declaring class                                                                                                                                       | Function | Feature | Meaning                                                    |
| ----------------------------------------------------------------------------------------------------------------------------------------------------- | -------- | ------- | ---------------------------------------------------------- |
| `MenuBaseWidget_C`, `PopupScreenBaseWidget_C`, `CouchCo-opHandover_C`, `PauseTabCollectablesBase_C`, `PauseTabRelationship_C`, `RewindPause_C`, `RewindUnlocked_C` | `Show`   | Menus   | A screen opened; starts the arrival readout. Each class that overrides `Show` declares its own function and is routed on its own. |
| `NestedContentMenu_C`                                                                                                                                 | `ShowNestedContent`, `PopNestedContent` | Menus | A section opened or closed inside a screen; the screen is read again (title only if it changed). |
| `MenuBaseWidget_C` (instances of `MainMenuWidget_C`)                                                                                                  | `Show`   | Subtitles | The main menu is back: the last subtitle line is forgotten. |
| `InputActionInteractionPromptWidgetSMG026_C`                                                                                                          | `SetButtonPrompt` | Prompts | The input action and style a button prompt is set up with, kept for naming its key. |

## Pollers

| Poller                                                                                                                                    | Feature     | Meaning                                                                                       |
| ----------------------------------------------------------------------------------------------------------------------------------------- | ----------- | --------------------------------------------------------------------------------------------- |
| Focus watcher: `bIsHighlighted` edges of `UIInteractableWidgetBaseSMG026` controls and `LastFocusedWidget` of `SMGUIWidget` screens, kept to screens whose `IsCurrentScreen()` is true | Menus       | Current screen and focused control.                                                           |
| `menus.arrival`                                                                                                                           | Menus       | Reads a newly opened screen as title, selection, prompts once it has settled.                 |
| `menus.prompts`                                                                                                                           | Menus       | Re-reads the prompt bar when it changes, after the selection has stopped moving.              |
| `menus.value`                                                                                                                             | Menus       | Speaks a changed value of the focused selector or slider with its description.                |
| `menus.carousel`: `CurrentCarouselItem` of the shown `CharacterCarousel_C`                                                                | Menus       | Reads the character card when the carousel is turned.                                         |
| `pause`: `bIsActive` and `SelectedTab` of the visible `PauseTabSystemSMG026`                                                              | Pause       | The pause menu opening and its tab changing; both re-read the screen with the tab as heading. |
| `hud.text`: text of the HUD elements the HUD instance watcher reported                                                                    | Hud         | Speaks a caption, notification, alert or act title once its text has settled.                 |
| `subtitles`: `ForegroundLines` of the subtitle widget the HUD instance watcher reported, every frame; the focused widget every 30 frames  | Subtitles   | Reads each subtitle line once, the frame it appears, and keeps the last line for its hotkey until the main menu is back. |
| `prompts`: the prompt slots of the interaction prompt widget the HUD instance watcher reported, every 2 frames                             | Prompts     | Reads a button, mouse or stick prompt once its labels and key have settled, again when it changes. |
| `choices`: the option widgets the choice HUD elements point at, key hints, highlight and chosen flags, time remaining and countdown message of the choice widgets the HUD instance watcher reported, every 2 frames | Choices | Reads a choice once its options have settled, the key hints when shown, the option held, the option chosen (after the key is released), the seconds left and the countdown. Widgets faded out by their opacity count as not shown. |
| Text watcher                                                                                                                              | Watchers    | Diffs registered text blocks.                                                                 |
| HUD instance watcher (`*Instance` pointers, strong and weak, on the HUD components)                                                       | Watchers    | Appearance of HUD widgets: loading screen, saving icon, scene details, notifications, alerts, act display, the subtitle widget, the interaction prompt widget, the choice widgets and the timer bar; also the screen readout and dumps. |
| `input.activity`                                                                                                                          | Speech      | Keyboard, mouse and gamepad activity, so only the player's own input interrupts speech.       |
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
| `GFSubtitleWidget::GetSubtitleSetting`              | The subtitle setting, written to the log.            |
| `PlayerController::IsInputKeyDown`                  | The gamepad hotkey chord.                            |
