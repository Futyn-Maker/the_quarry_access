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
| `DirectionQTEViewportWidgetSMG026_C`                                                                                                                  | `Construct` | Qte | A quick-time event widget is up (the HUD instance watcher reports it too); its direction is read on the next frame. |
| `DirectionQTEViewportWidgetSMG026_C`                                                                                                                  | `WidgetAnimationEvt_SuccessAnim_K2Node_WidgetAnimationEvent_8`, `WidgetAnimationEvt_FailureAnim_K2Node_WidgetAnimationEvent_9` | Qte | The result animations start: the event was hit or missed. |
| `ButtonMashWidgetSMG026_C`                                                                                                                            | `Construct` | ButtonMash | A button mash widget is up, in play or in a tutorial. |
| `DontBreatheWidgetSMG026_C`                                                                                                                           | `Construct` | DontBreathe | A Don't Breathe widget is up, in play or in a tutorial. |
| `ReadingPaneWidget_C`                                                                                                                                 | `AddLines` | Exploration | The reading pane filled a page: it is read again once it has settled. |

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
| `qte`: `GetDirectionQTESMG026Info`, `GetActionMappingSuccess` and the result animations of the quick-time event widgets, every frame | Qte | Reads the direction (and the key on the keyboard) and plays its cue when the event appears and every second until it is resolved, then plays the result. A second widget of the same event stays quiet. |
| `mash`: `GetButtonMashInfo`, the glyph and `InfoDataInstance` (`ButtonMashState`, `CommitFraction`) of the button mash widgets, every 2 frames | ButtonMash | Reads the button with the game's button mash mode once, blips as the ring moves, says and plays the outcome. |
| `breathe`: the prompt text blocks, the glyph and `InfoDataInstance` (`DontBreatheState`, `HoldingBreathTimeScale`) of the Don't Breathe widgets, every 2 frames | DontBreathe | Reads the prompt once it has settled and again when it changes, blips along the breath bars while the breath is held, says and plays the outcome. |
| `explore`: the pawn's `AvailableUseLocations` and `OverlappingUseLocations`, `IsInLoco`, the `ExplorationDestinationSMG026` actors (every 2 s), positions through `K2_GetActorLocation` and the camera yaw through `PlayerCameraManager::GetCameraRotation`, every 10 frames; the `StaticExplorationGlintActorSMG` actors while a static exploration widget is up, every 5 frames; the reading pane's title, lines and page count, every 6 frames | Exploration | Lists what can be walked to with label, distance and direction, follows the target with the beacon, leads the camera to the glints, reads the pane. |
| Text watcher                                                                                                                              | Watchers    | Diffs registered text blocks.                                                                 |
| HUD instance watcher (`*Instance` pointers, strong and weak, on the HUD components)                                                       | Watchers    | Appearance of HUD widgets: loading screen, saving icon, scene details, notifications, alerts, act display, the subtitle widget, the interaction prompt widget, the choice widgets and the timer bar; also the screen readout and dumps. |
| `input.activity`                                                                                                                          | Speech      | Keyboard, mouse and gamepad activity, so only the player's own input interrupts speech.       |
| `hotkeys`                                                                                                                                 | Hotkeys     | Keyboard hotkeys and the gamepad chord.                                                       |
| `commandfile`                                                                                                                             | Diagnostics | `Mods\QuarryAccess\command.txt` developer commands.                                           |

## Native functions called

## Native functions hooked

| Function | Feature | Meaning |
| --- | --- | --- |
| `/Script/SMG026Runtime.StaticExplorationSMG026_Replicator:MulticastPOIFound` | Exploration | A point of interest was found while looking around. |

## Native functions called

The mod calls these through `ProcessEvent`:

| Function                                            | Used for                                             |
| --------------------------------------------------- | ---------------------------------------------------- |
| `UIStaticsQuarry::FormatLocaleString`               | Resolving the game's locale keys to display text.    |
| `UIStaticsQuarry::GetCurrentLocaleString`           | The game's text language.                            |
| `SMGUIWidget::IsCurrentScreen`                      | Which loaded screens are actually on display.        |
| `SMGUIUserWidgetBase::GetKeysFromActionMapping`     | Keys bound to an input action, asked of the screen on display when the game's own remap rows (`UISettingsSMG026.KeyBindingSettingsData`, read by reflection) do not list the action; with no screen the engine's `InputSettings` mappings are read by reflection instead. |
| `KismetInputLibrary::Key_GetDisplayName`            | Engine display name of a key.                        |
| `MenuBarBaseSMG026::GetUnlocalisedMenuContext`      | The description line of the bottom menu bar.         |
| `TypeWriterTextBlockSMG026::GetText`                | The full text of a message that is typed out.        |
| `GFSubtitleWidget::GetSubtitleSetting`              | The subtitle setting, written to the log.            |
| `GFDirectionQTEWidgetSMG026::GetDirectionQTESMG026Info`, `GetActionMappingSuccess` | The angle, timer and accepted action of a quick-time event. |
| `UserWidget::IsAnimationPlaying`                    | Whether a result animation of a quick-time event plays. |
| `GFButtonMashWidgetSMG026::GetButtonMashInfo`       | The action, type and time limit of a button mash.    |
| `DontBreatheWidgetSMG026::GetDontBreatheInfo`       | The action that holds the breath.                    |
| `SMGGameSettingEnum::GetCurrentEnumValueAsInt`      | The game's button mash mode.                         |
| `NavigationSystemV1::FindPathToLocationSynchronously`, `K2_ProjectPointToNavigation`, `NavigationPath::IsPartial` | The walkable route to the exploration target. |
| `InputSettings.AxisMappings` (read by reflection)   | The keys the game walks the character with, which the mod holds down while walking to a target. |
| `UseLocationSMG026::ClientStateChanged`             | The scene turning an interaction on or off, which is what the exploration list follows. |
| `Actor::K2_GetActorLocation`, `K2_GetActorRotation` | Positions of the character, the use locations, the destinations and the glints. |
| `PlayerCameraManager::GetCameraRotation`             | The camera's heading, which the movement keys follow. |
| `CharacterBaseSMG::IsInLoco`                          | Whether the character is under the player's control. |
| `PlayerController::IsInputKeyDown`                  | The gamepad hotkey chord.                            |
