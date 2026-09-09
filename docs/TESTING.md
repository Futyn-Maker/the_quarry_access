# Testing QuarryAccess

Every change is tested in the real game by a blind tester (NVDA, Russian UI) and, before that, by the
developer with the scripts below. A change is accepted when the tester confirms the script, the logs
contain no `ERROR` lines, and the automated transcript matches expectations.

## Where things are

- Mod log: `<game>\SMG026\Binaries\Win64\Mods\QuarryAccess\QuarryAccess.log` (also mirrored into `UE4SS.log`).
  Speech lines look like `SAY focus "Новая игра, 1 из 7, Начать новую игру."`.
- Screen dumps (Ctrl+F9): `<game>\SMG026\Binaries\Win64\Mods\QuarryAccess\dumps\screen-<timestamp>.txt`.
- Config: `<game>\SMG026\Binaries\Win64\Mods\QuarryAccess\QuarryAccess.ini`.

## Developer scripts

| Script                                     | Purpose                                                                |
| ------------------------------------------ | ---------------------------------------------------------------------- |
| `scripts\build.cmd`                        | Build `main.dll`.                                                      |
| `scripts\format.cmd`                       | Format the C++ sources with clang-format.                              |
| `scripts\deploy.ps1`                       | Copy the mod into the game (game must be closed).                      |
| `scripts\run-game.ps1 -AutoDismissCrash`   | Launch through Steam, wait for `[QuarryAccess] ready`, detect crashes. |
| `scripts\sendkeys.ps1 -Keys Enter,Down,F6` | Drive the game with keystrokes (the game window must be in front).     |
| `scripts\screenshot.ps1`                   | Capture the screen for a visual cross-check.                           |
| `scripts\tail-log.ps1`                     | Print the speech transcript and errors.                                |
| `scripts\stop-game.ps1`                    | Close the game.                                                        |
| `scripts\check-lang.ps1`                   | Verify that all 20 language tables have the same keys.                 |

The developer always closes the game at the end of an automated session.

## Startup, speech and hotkeys

Tester script (Russian UI):

1. Start the game. Within a few seconds after the splash videos you should hear:
   "The Quarry Access <версия> загружен. Программа чтения экрана: NVDA. Язык: ru_RU."
2. Press F8: the help text is spoken (hotkeys and the gamepad chord).
3. Press F5: the help text is repeated.
4. Press F8 again and immediately F7: speech stops.
5. Press F6: the current screen is described.
6. Press Ctrl+F9 to write a screen dump for bug reports.

Expected log lines: `Tolk loaded; screen reader: NVDA`, `game locale: ru-RU; mod language: ru_RU`,
`self-check: SMG_HUD_MENU_BUTTON_NEWGAME_000001 = "Новая игра"`, `self-check: control scheme MouseKeyboard`,
`ready`, one `hotkey:` line per key press, no `ERROR`.

## Menus

Tester script (Russian UI):

1. Start the game and wait for the title screen: "НАЖМИТЕ Enter, ЧТОБЫ НАЧАТЬ" (the key name
   follows your control scheme).
2. Press Enter. When the main menu opens you hear, in this order and without one cutting the
   other: the screen title, the focused item with its position and description, then the
   prompts, for example "Главное меню. Новая игра, 1 из 8, Начать новую игру. Отладка Z,
   Настройки E."
3. Arrow through the items: each is announced once with its position and description line.
4. Press E for the settings, then enter a category. The category's own prompts, such as
   "Сброс R", are announced after the focused row.
5. On a settings row you hear the row name, the control type, the current value, the position
   and the description. Press left or right: the new value is spoken together with the
   description of that value.
6. Check the "Мышь/клавиатура" and "Обучение" sections: their rows are announced like any other.
7. Open a section such as "Режим кино" or "На одном экране": the prompts are heard once, after
   the title and the focused item, never before them and never twice.
8. Let the mod talk without touching anything: nothing cuts anything else off. Then press a key
   while it is still speaking: that is answered at once.
9. Режим кино → Режиссёрское кресло: the character shown is read, each switch is announced with
   its title, the current value and its position among the switches, and turning the character
   carousel reads the next character.
10. Press F6 anywhere: title, focused item and prompts. Press F8: the menu help plus the prompts.
11. Move between screens (settings, podcasts, back to the main menu) and press F6: it always
    describes the screen you are actually on.

Expected log lines: verbose `focus: screen=... focused=...` and `menus: screen via ...`, one
`SAY` per navigation key, and no `ERROR`.

## Pause menu and HUD

Tester script (Russian UI). Start a new game in a free slot and let the prologue begin.

1. When the scene starts you hear its caption, for example "22:30 - Лора. Хэкеттс Куори | Дорога"
   (time, character, place), and "Загрузка" while the game loads.
2. Press Escape: "Пауза. Персонаж, вкладка 1 из 6" followed by what the tab shows and its prompts.
3. Press ] and [: each tab is announced with its position; on the clues, evidence and tarot tabs
   the objective line, the selected entry with its description and the prompts follow. The
   character tab is named after the character it shows; it only displays the character, so
   nothing but the objective and the prompts is read there and the arrows do nothing.
4. On a collectables tab arrow through the entries: each is read with its position and
   description; press right to enter the details list and each entry is read.
5. Press F6 in the pause menu: paused, the tab, the screen content and the prompts. Press F8: the
   tab keys and the key that returns to the game.
6. Return to the game and play until a clue is found: the notification is read with its title,
   text and the key that opens it. A save shows as "Сохранение".
7. Open the settings from the pause menu: they read as in the main menu.

Expected log lines: `pause: opened, tab ...`, `pause: tab ...`, `hud: appeared ...` for each caption
or notification, one `SAY` per element, and no `ERROR`.

## Subtitles

Tester script (Russian UI). Continue or start a game and let a scene with dialogue play.

1. Every subtitle line is read once, in order, the moment it appears, exactly as the game
   displays it, including the hyphen or the character name when Настройки → Субтитры → Начало
   строки asks for one; a subtitle shown on two lines is read as one. Lines never cut each
   other or anything else off, and when speech falls behind every line is still read to the
   end.
2. Press F9: "Озвучивание субтитров выключено" and the dialogue goes silent. Press F4: the
   last line that appeared is repeated, even though it is gone. Press F9 again: "Озвучивание
   субтитров включено" and the reading resumes.
3. Press F5 during a scene: the last message is repeated. Press F6: the lines on screen are
   read. Press F8: the help names F9 and F4 with their gamepad chords.
4. Pause the game: F4 still repeats the last line. Leave to the main menu: F4 now says
   "Субтитров ещё не было", and so it does after starting a game until the first line.
5. Settings → Субтитры → Субтитры: "Выкл." makes the dialogue silent and F4 repeats only what
   was displayed before; "Скрытые субтитры" adds sound cues such as "(скрип двери)", read as
   displayed.

Expected log lines: `subtitles: SubtitleWidgetQuarry_C shown`, `subtitles: setting On`, one
`subtitles: line "..."` the moment each line appears and one `SAY announce` per line while the
reading is on, `hotkey: Subtitles` and `hotkey: LastSubtitle`, `subtitles: last line forgotten at
the main menu`, and no `ERROR`.

## Choices and prompts

Tester script (Russian UI). Play the prologue from the start.

1. The first button prompt of the drive (an interruption or an interaction) is read with its
   label and key as it appears, for example "ПЕРЕБИТЬ? A" or "Используйте E, чтобы идти".
2. At the first choice you hear: "Выбор на время." (or "Выбор." without a timer), then the
   question if one is shown, "Влево: <label>, <line>", "Вправо: <label>, <line>", and the
   option of not responding only in a choice that offers it. Nothing cuts anything off. The
   game shows the keys only in the choices it decides to: when its key caps appear you hear
   them, "A, D", with the keys as bound.
3. Hold A or D: the option you are committing is read as it lights up (NVDA cuts speech while
   a letter key repeats, so it may come out short). Hold until a short two-note tone marks the
   commit, then release the key: "<option>, выбрано." follows the release.
4. When the game starts showing the seconds left, each value is read. When the time runs out
   with nothing held, the game shows nothing more and nothing is said.
5. Before a choice on the road you may hear "Выбор через 5 с" and then "Скоро придётся сделать
   выбор!": the countdown is read when it appears and when its wording changes, not every second.
6. At the tarot or any four-way choice: "Выбор." then each option with its place ("Вверх: ...",
   "Вправо: ...", "Вниз: ...", "Влево: ..."). Press F8: the keys of the four places.
7. Press F6 during a choice: the choice is read again; during a prompt: the prompt.
8. After a choice, pause and return to the game: the choice is not read again.

Expected log lines: `hud: appeared ... ChoiceContainerWidgetInstance`, `prompts: ... set up with
ChoiceCommitLeft`, one `SAY announce` per choice and per prompt, `SAY focus` for the option held,
and no `ERROR`.
