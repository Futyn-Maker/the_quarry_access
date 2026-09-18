---
paths:
    - "src/ui/**"
    - "src/features/{Menus,Pause,Hud,Subtitles,Credits,Screens,Tarot}.*"
    - "mod/screens.ini"
---

# Menus, pause menu, HUD, subtitles, credits, screens, tarot

- The menu framework is Blueprint (`MenuButton_C`, `MenuCarousel_C`, `TextSlider_C`, `KeyRemap_C`, `NestedContentMenu_C`, popups built on `PopupScreenBaseWidget_C`); the native `USMGUIButton*` classes are unused.
- Row labels by type, in this order: sliders and carousels read their `TitleText` block (the rendered order puts the value first); `MenuButton_C` resolves its `LocalisedText` locale key (a deep walk finds the highlight decoration twice); `KeyRemap_C` and `TutorialButton_C` have no key and nest deeper, so only they get the deep walk.
- Buttons, titles and prompts are added at runtime: find them among live instances and walk up through `Slot->Parent` (`obj::IsDescendantOf`) rather than down from the screen. Prompt widgets keep a hidden placeholder text on their unused side. A screen's description line comes from `UMenuBarBaseSMG026::GetUnlocalisedMenuContext()`; `MenuContext.Text` stays a placeholder forever.
- Focus can land on a part inside a control (`PopupButtonPanel_C` inside `BasicPopupButton_C`); `ui::Interactable` prefers the ancestor of a specific kind.
- The pause menu is several sibling top-level widgets: the tab bar `PauseTabSystemWidget_C`, one content widget per tab and `CharacterCarousel_C`. Read only the selected tab's content, since tabs visited earlier stay loaded and shown for a while, and require `IsWidgetShown`. The character tab shows only a 3D model: nothing beyond the tab, the objective and the prompts is read there.
- Subtitles: `ActionHUDSubtitle.SubtitleWidgetInstance` (`SubtitleWidgetQuarry_C`) holds the lines in `ForegroundLines`, polled every frame; a line absent from the two previous polls is new. The native `SetCurrentText` is not hooked. `GetSubtitleSetting()`: 0 off, 1 on, 2 closed captions, 3 minimal. The last line stays available to F4 until the main menu shows (`MenuBaseWidget_C:Show` on `MainMenuWidget_C`), pauses included.
- HUD elements are read from a whitelist (`kReadableHuds` in `Hud.cpp`) once their text has been stable for two polls. The epilogue's fate cards are `AftermathText_C` under `ActionHUDAftermathTextSMG026.AftermathTextInstance`.
- Credits: the opening names come one by one (`IntroCredit_C`). The end rolls move faster than speech, so only "Credits" and the section titles are said by themselves (the first line of a song block); F6 reads the rows on screen with their roles, from each line's `Credit` struct.
- Screens: some text is a picture on a 3D monitor (textures under `Content/Flipbooks`, set by a cinematic's material track or a prop's material). `mod/screens.ini` maps each material to its English transcription, verbatim, since the game shows it in English in every language; the feature watches known meshes once a scene's marker material is loaded. A search for unread text must cover pictures, material tracks and Bink films, not only widgets.
- Tarot visions are Bink films (`Movies/Tarot/<Card>/*.bk2`, 34 of them: a card plus a variant). Every `BinkMediaPlayer` is found by class and polled; when a tarot URL starts playing, `tarot.<film file name>` is read from `mod/lang/tarot/`. Werewolves are named only where the film shows the transformation.
- Tutorial overlays hold their own widget instances outside the HUD, so their `Construct` hooks are the primary detection there.
- Couch co-op and Wolf Pack widgets exist, hidden, in single player too: never strip or special-case them.
