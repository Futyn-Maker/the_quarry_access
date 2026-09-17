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
2. Press F8: the help text is spoken, naming the F keys.
3. Press F5: the help text is repeated.
4. Press F8 again and immediately F7: speech stops.
5. Press F6: the current screen is described.
6. Press Ctrl+F9 to write a screen dump for bug reports.
7. Press F3 with NVDA running, once: "Речь: SAPI." is spoken by the SAPI voice, and so is
   everything after it, whole sentences included; press F3 again: "Речь: NVDA." through NVDA.
   Quit with SAPI chosen and start the game again: the greeting comes through SAPI
   (`PreferSapi=1` in the ini). With no screen reader running F3 says "Программа чтения
   экрана не запущена; говорит SAPI." The log names the voice: `sapi: the voice ... speaks
   ru_RU`, or the voice picked for the language when the default one speaks another.
8. On a gamepad, in the main menu: hold Back and press Y: the help is spoken, naming the
   chords ("удерживайте кнопку Back и нажмите кнопку A ..."). Hold Back and press A, X and B:
   the last message is repeated, the screen is read, speech stops; the menu does not react
   to A, B, X or Y while Back is held, and the D-pad still moves the selection when nothing is
   held. The same chords work while a scene plays. Back with the right stick pressed in
   switches the speech as F3 does.

Expected log lines: `Tolk loaded; screen reader: NVDA`, `game locale: ru-RU; mod language: ru_RU`,
`self-check: SMG_HUD_MENU_BUTTON_NEWGAME_000001 = "Новая игра"`, `self-check: control scheme MouseKeyboard`,
`ready`, `keyboard hook installed: the mod's keys are taken ahead of the screen reader and the
game`, one `gamepad Gamepad_Special_Left + ... = ...` line per chord at start, one `hotkey:
<command> (keyboard)` or `(gamepad)` line per press, every press included, no `ERROR`.

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
2. Press Escape: "Пауза. Персонаж, вкладка 1 из 6" followed by what the tab shows and its prompts,
   ending with "[ и ] переключают вкладки." (the bumpers after a gamepad press).
3. Press ] and [: each tab is announced with its position; on the clues, evidence and tarot tabs
   the objective line, the selected entry with its description and the prompts follow. The
   character tab is named after the character it shows; it only displays the character, so
   nothing but the objective and the prompts is read there and the arrows do nothing.
4. On a collectables tab arrow through the entries: each is read with its position and
   description; press right to enter the details list and each entry is read.
5. Press F6 in the pause menu: paused, the tab, the screen content and the prompts. Press F8: the
   key that returns to the game, then the prompts with the tab keys.
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
   субтитров включено" and the reading resumes. Quit with the reading off and start the game
   again: it stays off (`subtitles: reading off at start` in the log) until F9 turns it on.
3. Press F5 during a scene: the last message is repeated. Press F6: the lines on screen are
   read. Press F8: the help names F9 and F4; after a gamepad press it names Back with the
   right trigger and the right bumper instead, and those chords do the same.
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
2. At the first choice you hear: "Выбор на время." (or "Выбор." without a timer) the moment
   the choice appears, then the question if one is shown, "Влево: <label>, <line>", "Вправо:
   <label>, <line>", and the option of not responding only in a choice that offers it. The
   game fades the line under each label in half a second after the label; the options are
   read once, with their lines, and never twice. A choice whose options carry no lines is
   read whole at once. Nothing cuts anything off. The game shows the keys only in the choices
   it decides to: when its key caps appear you hear them, "A, D", with the keys as bound.
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
ChoiceCommitLeft`, `choices: ... announced, its phrases still to come` and then `choices: ...
read` for a choice with lines under its labels (`choices: ... read` alone for one without), one
`SAY announce` for the heading and one for the options, `SAY focus` for the option held, and no
`ERROR`.

## Quick-time events, button mash, Don't Breathe

Tester script (Russian UI). Chapter 1 has the first quick-time events; a button mash and a
Don't Breathe come later in the story. Settings → Доступность shows the modes in play: Простые
QTE, Быстрое нажатие, Не дышать.

1. At the first quick-time event the direction is said the moment the marker appears, with
   its tone ("Вверх, W."), and the same tone sounds again when the marker has settled and the
   game begins to take the press, about 0.8 s later: a press before that tone is ignored by
   the game, for everyone. The direction is repeated every second until the event resolves,
   and a rising or falling tone marks the result.
2. Press F6 during an event: the direction is read again. Press F8: the four keys.
3. At a button mash you hear the button the way Настройки → Доступность → Быстрое нажатие asks:
   "Быстро нажимайте левая кнопка мыши." by default, "Удерживайте ..." in the Hold mode,
   "Нажмите ..." in the Tap mode, "..., автоматически" in the Auto mode. While the ring fills
   with your presses, blips rise in pitch; when you stop they fall as the ring shrinks. The
   outcome is a tone plus "Успех." or "Неудача.", followed by whatever message the game shows.
4. At a Don't Breathe you hear the prompt as displayed, "УДЕРЖИВАЙТЕ левая кнопка мыши ЧТОБЫ
   ЗАДЕРЖАТЬ ДЫХАНИЕ" (the key as bound; a glyph without a printed name is named from the
   binding). Hold the key: the prompt changes to "ОТПУСТИТЕ ... КОГДА ОКАЖЕТЕСЬ В БЕЗОПАСНОСТИ"
   and blips follow the breath bars twice a second while you hold, falling as the breath
   runs out. A rising tone with "Опасность миновала." says the danger has passed and the key
   may be let go; a falling tone with "Опасность." says the next stretch is about to begin, and
   "Дыхание кончается." says the bars are nearly empty. Release in a gap between the stretches:
   a tone plus "Успех." or "Неудача.", then the game's own message.
5. Bonus → Обучение: the Don't Breathe and QTE tutorials show the same widgets and are read the
   same way, except that a Don't Breathe there opens with "Показ: игра проходит его сама." The
   game runs those on a fixed clock and always ends them the same way, so nothing is asked of
   you and no advice is given.

Expected log lines: `qte: ... angle N action "DirectionQTE..." -> dir.... key "..."` for every
event (the angle and the action must agree: 0 up, 90 right, 180 down, 270 left), `qte: ... hit`
or `missed`/`timed out`, `mash: ... action "ButtonMash" ... mode N`, `mash: ... succeeded`,
`breathe: prompt "..."`, `breathe: N stretch(es) of danger` with a line per stretch,
`breathe: state 1`, one `breathe: holding at N s, bar X` per blip, `breathe: the danger has
passed at N s`,
`breathe: ... succeeded`, and no `ERROR`.

## Exploration

Tester script (Russian UI). Start the prologue: the first scene the player walks in is the
crashed car at night.

1. When the character comes under your control the game's own prompt about the keys or the
   stick is read, then, once a scene, the mod's keys: "N и P выбирают цель, G идёт к ней, H
   говорит, где она, T включает и выключает маяк." (after a gamepad press the D-pad and X are
   named instead), and then the thing the beacon leads to, the nearest not yet reached:
   "Макс, 3 м, впереди". Nothing else is read by itself: F6 lists everything that can be
   walked to, nearest first, each as the game names it with its distance in metres and its
   direction from the camera, "Багажник: Изучить, 5 м, впереди слева". Everything the scene has opened up is
   there from the start, however far away it is. A use location standing at a place is one
   entry named "Листовка: Осмотреть"; where the scene gives an interaction no name of its
   own, the name it carries in the level is read instead ("Camp Map: Взаимодействовать").
   The beacon starts on the target: it sounds in the ear on the side of the next turn of the
   walkable route, rises in pitch as you get closer and drops an octave when the way is
   behind you. Walk toward it with W, A, S, D. T turns the beacon off and on, and the switch is
   kept: quit with it off and the next start begins with it off (`Beacon=0` in the ini), as the
   aim sound switch of a fight is kept under `[Combat] AimSound`.
2. Press N: the next thing is said with its distance along the route and the beacon follows
   it; P goes back; the order stays the same however you move. H says the target again with
   its current distance and direction. T turns the beacon off ("Маяк выключен.") and on. F6
   lists everything again; F8 explains the keys. When a scene moves on only by walking
   somewhere, that somewhere is in the list as "Путь дальше" with its distance and direction,
   and G walks there. On a gamepad, while the character walks
   freely, the same keys are the D-pad (right, left, up, down) and X, with no chord, and F8
   names those instead.
3. Press G (X on the gamepad): you hear "Иду.", half a second passes, and the character
   walks the route to the target on its own, past the trees and around the corners, and stops
   there with the arrival tone. On a keyboard it walks by holding the game's own movement
   keys, so the game window must be the one in front; on a gamepad it pushes the left stick
   the game reads, and the prompts keep naming gamepad buttons all the way there rather than
   turning into mouse and keyboard prompts halfway. Press G again, or push the movement keys
   or the stick, and it stops at once with "Остановка." A cutscene, a dialogue, a choice, a
   quick-time event or any screen ends it in the same frame it appears, and the keys are let
   go of with it.
4. Reaching a target is announced once, "<name>, на месте", with a tone; a use location then
   shows the game's own glyph, read as "<name>, левая кнопка мыши" with the same name the list
   gave it. Where several things stand together, the game offers one of them at a time and the
   glyph says which; each has its own patch of floor, so walking to another of them with G
   moves the offer and the glyph says the new one. Two things can share a patch of floor: in
   Laura's cell in Chapter 7 the hiding place in the wall and the cot beside it have
   overlapping patches. A walk to one of them goes on to that thing's own spot until the game
   offers it, "на месте" is said only then, and if at the end of the way the game still
   offers the other you hear "<target>: рядом, но игра предлагает <other>." and a press would
   use that other one. Walk away and back and the beacon leads again. Things the scene adds
   later are announced as "Новое: ...". A short exchange in the middle of a scene does not
   start the list again. Only what the scene has turned on is listed: a thing the scene keeps
   switched off is not in the list even when the character stands on it, and the way out of a
   room joins the list the moment the scene turns it on, a few seconds after entering.
5. If the game ever offers its own places (a prompt "Осмотреться X" appears), F8 also names
   X, Z, C and V; in the prologue scenes it did not.
6. In a scene that asks you to look around (the timer bar with "Используйте мышь, чтобы
   осмотреться"), the beacon leads the camera toward the nearest glint; when something is
   found you hear a tone and "Найдено."
7. Opening a note or a letter reads its title, its page and the page count; the next page is
   read when it turns. F8 names the page and close keys.
8. Write `explore` into `Mods\QuarryAccess\command.txt` while standing in a scene: the log
   receives the character, the camera, every use location and place with its label and
   position, and the glints. `key DirectionQTEUp` writes where the keys of an action come
   from and what is spoken. `walkkeys` writes the four movement keys and says whether a walk
   would push the gamepad stick or press those keys.
9. "Путь дальше" is offered only where the walk to something else does not already go through
   it. Collecting firewood in chapter 2 and following Abigail into the woods in chapter 3 each
   offer one. On the treehouse scene and on the walk to the radio hut the ways sit on the route
   to things already in the list, so they drop out. The log gives the measurement for each way,
   in centimetres, so a way that is offered or dropped unexpectedly can be read rather than
   argued about.
10. A way the scene has already used drops out. In the Chapter 7 police station, walking into
    the way that sets off the creature's scare plays the scare, and when exploring resumes that
    "Путь дальше" is no longer in the list. The log says
    `explore: the scene has moved past T_Maxscare (b_maxscaredone is set)`.
11. A thing on a wall is reached from the floor it hangs over. In the same police station, in
    the office, the beacon and the walk both lead to the board and the game offers it. Each walk
    logs
    `explore: the ground for "..." is N cm across from it and +0 cm in height; the way there is complete`,
    and a way that says partial or a large height is worth sending in.
12. A way on is entered, not approached. At the Chapter 9 scrapyard, after the gate, walking to
    "Путь дальше" at the crane goes into the trigger, "на месте" is said once the character is
    inside it, and the scene moves on. Each walk to a way logs
    `explore: that ground is inside the volume of the way`.
13. A chosen "Путь дальше" stays in the list while it is walked to, and a way that comes back
    to the list after being hidden is not announced as new again.
14. Press F8 in the main menu after leaving a game, and again while exploring: the help is
    read both times and the game keeps running.
15. Prologue, the crash site: the trunk is one entry, "Багажник: Изучить", with no second
    "Багажник" beside it, before and after the toolbox. The woods after the flyer: "Клетка:
    Осмотреть" is said at about 30 m and "Сундук: Изучить" at about 22 m, the mesh's roads to
    the front of their boxes; G walks either road, past the bushes or over the near ground, and
    arrives with the game's offer, and the scene does not move on. "Путь дальше" is not in the
    list while "Идти по тропе" is, since the road there leads through it, and the intercut on
    the trail is never listed. The log names a road through a waiting place with
    `explore: the road to "..." runs through ..., which the scene is waiting on`, and a better
    spot with `explore: "..." is better reached at another spot of its box`.
16. Chapter 2, Kaitlyn at the campfire with the phone camera: the aim sound of the fights leads
    the right stick to the treehouse, on the side it lies, higher or lower as it stands above or
    below the view, faster as the view nears it, and the double ping while it is in frame; "В
    кадре, снимайте!" is said once as it comes into frame, and after A the picture is judged,
    "Снимок сделан, цель в кадре." or "Снимок сделан, цель не в кадре."; the campfire talk
    that follows differs with it. Chapter 3, the binoculars at the boathouse: the same sound
    leads to the point on the island and "В кадре." is said as the game zooms in; the office
    billboard and the camp arrival window behave alike. The sound follows the aim sound
    setting of the fights, not the walk's beacon: T during the look-around switches it, and F8
    names the key. At the campfire the treehouse stands about 23 degrees left of the view as
    the camera comes up, outside the 20 degrees the flow asks for, so nothing is said until the
    stick brings it in. The log names the points at the start, `explore: looking around begins
    ... with N point(s): L_TakeTreehousePhoto (30 by 20 deg, the flow judges it within 20
    deg)`, every second `explore: look at "...": N deg across, N up, N off the view, in frame`
    or `off`, the picture's verdict, and the flow's own: `explore: the flow's bool
    "b_kaitlynislookingattreehouse" is now true, the view N deg across, N up, N off
    "L_TakeTreehousePhoto"` as the treehouse comes within the flow's 20 degrees, and after the
    press `explore: the flow's flag "f_kaitlynsawtreehouselight" is raised`, on which the
    honesty choice at the campfire hangs.

Expected log lines: `explore: ... under the player's control`, one `explore: use location
"..."` or `explore: destination "..."` per thing listed, `prompts: ... set up with <action>
style <n>` for every prompt the game shows (the destination prompts carry the
`ExporationDestination...` actions), `explore: reached "..."`, `explore: looking around begins`,
`explore: point of interest N found`, `explore: reading pane opened`,
`input: the gamepad the game reads through ... is shared with the mod`, one
`explore: a way ... is missed by the nearest road by ... cm and is offered/not offered` per
way, and no `ERROR`.

## Real-time combat

Tester script (Russian UI). The first fight is the shooting range in chapter 2 (Nick with a
pump-action shotgun, bottles and melons, no time limit); the story fights come later and last
between 1.6 and 8 seconds each. Settings → Доступность → Помощь в прицеливании: "Откл." and
"Вкл." both leave the aiming to you, "Автоматический" lets the game aim and fire.

1. When the fight begins you hear the game's own prompt "ВЫСТРЕЛИТЬ, левая кнопка мыши",
   then "Прицеливание." and nothing more; what there is to shoot at is a key away. H says it as it is seen: "Существо, 4 м, слева." for
   a werewolf, "Объект, 6 м, впереди." for a bottle or a table, "Цель, ..." for a person, who is
   never named; with two, "Целей: 2." comes first. In the story fights "Таймер." follows for the
   timer bar. The game draws no crosshair: your aim is the torch beam on the
   weapon. A blip sounds in the ear on the side of the target: higher when the target is above
   the beam, lower when below, faster as the beam nears it. Move the mouse (or the right stick)
   toward the sound. When the shot line is on the target's body the blip becomes a quick double
   ping in both ears: fire. The sound leads to one target at a time: the one the line is on,
   else the one chosen with N or P, else the nearest to the line, which it keeps until another
   is clearly nearer; the double ping and "Огонь!" are for that target only.
2. Each shot is answered: a rising tone with "Попадание." or a falling one with "Промах." When
   a timed fight ends without a hit you hear a falling tone and "Время вышло."
3. Press H during a fight: "Цель: 6 м, справа выше." or "На цели." Press N or P when the
   scene offers two targets: "Существо 2 из 2. Цель: 3 м, слева." and the sound follows that
   one. Press T: "Звук прицела выключен." and the blips stop; T again brings them back. On a
   gamepad the D-pad right, left, up and down do the same without a chord. Outside a fight
   and outside walking the character, N, P, H, G and T do nothing.
4. Press F6 during a fight: aiming and where the target stands. Press F8: the sound explained
   and the fire key.
5. With the aiming setting on "Автоматический" you hear only "Прицеливание, автоматически."
   and the game plays the fight.

Expected log lines: `combat: replicator ... path "...|<state>"`, `combat: begins on the fire
prompt for <character> (<name>) with <weapon> ...; aiming setting N; state "<state>"; K
target(s): <name> (<class> <actor>, by its register) ...; time limit ...; aim read from the
torch`, one `combat: action ... in state "..."` per loaded fight action, the one of the fight
marked `<- the replicator's state` (a name carried by several actors lists them with their
distances and the nearest is taken), and `combat: status ...` per status object,
`combat: aim assist strength ...`, `combat: replicator ... path "..."`, a `combat: aim: aim point yaw ... (torch yaw ..., camera yaw ...); target 0 ... on target/off target (torch N off, on/off; camera N off, on/off) ...; aim point (...) N m off, N deg off the torch`
line twice a second and one more `combat: at the shot: ...` at every shot, `combat: the sound leads to target N` when the sound changes target, `combat: shot N (counted by the game)`,
`combat: target N lost health, 1 to 0` for the target a shot took, `combat: the weapon fires`, `combat: hit (...)` or `combat: miss on shot N`, `combat: over
(the fight's replicator is gone)`, and no `ERROR`. If the fight's targets are reported `(not found)`, the aim is read
from the camera rather than the torch, or the sound leads nowhere, send the log: the lines
above name what the game held.
6. Once the shot line has been on the target the sound leads to for a fifth of a second,
    "Огонь!" is said once, with the aim sound on or off and with aim assist on or off; it comes
    again only after the line has been off the target for a second. In the automatic setting
    nothing is said. The log has `combat: the shot line has been on target N for a fifth of a
    second, the word to fire`.
7. The shooting range, chapter 2: nine targets, six bottles and three melons, all about 13 m
    away, so H says 13 m for every one of them; the character does not walk here. The sound
    leads along the row; with the line between two bottles it leads to the nearer one. Hold
    the double ping on a bottle or a melon and fire, and fire once more with the blips fast but
    no double ping: "Попадание." or "Промах." answers each, and the log at the shot shows the
    three lines against every target with the target that lost health, which is what settles
    the shot line. The scene goes on after the shots whatever they hit.

## Tarot visions

Tester script (Russian UI). After a chapter in which a card was found, Eliza offers to show a
vision; choose "УВИДЕТЬ БОЛЬШЕ" (or the card, when two were found).

1. When the vision begins to play in the crystal ball, its description is read, for example
   after the prologue: "Джейкоб присел у передней части минивэна и отсоединяет топливный
   шланг. Позже двигатель загорается, и минивэн горит." The description is read once, after
   whatever the reader is saying, and says what the vision shows, without a chapter or a moment.
2. Press F6 while the vision plays: the description again. Press F5: the last message.
3. Open the pause menu, the tarot tab, choose a card whose vision was seen and press
   "Повторить видение": the same description is read as the replay begins.
4. The Hermit after chapter 5, Death, the Sun and Judgement after chapter 9 have several
   visions; the one described is the one the game plays, named by its film.

Expected log lines: `tarot: 2 film player(s): ...`, `tarot: Tarot_BinkMediaPlayer holds
"Tarot/TheFool/TheFool.bk2"` (`BinkMediaPlayer holds ...` for a replay from the pause menu),
`tarot: vision "Tarot/TheFool/TheFool.bk2" plays on Tarot_BinkMediaPlayer (described)`, one
`SAY announce` with the description, and no `ERROR`. A `(no description)` in that line names
a film the tables do not know: send the log.

## Fate cards

Tester script (Russian UI). Play the last chapter to its end.

1. After the closing words of the last chapter the epilogue shows where each character ended
   up: a sunrise, then a scene at each place the night ended for someone (the island, the
   scrapyard, the Hackett house, the woods, outside the lodge, the lodge, its kitchen, the
   roadside, the freak show site), and then the arrival of the police. A card lies over each
   scene, and each card is read as it appears: the name and state in capitals, as the game
   writes them ("ЭБИГЕЙЛ БЛИГ: ЖИВА", "ДЖЕЙКОБ КАСТОС: МЁРТВ"), and the line beneath on how
   it went for that character. One scene can show several cards in turn; each is read once.
2. F6 while a card is on screen reads it again.
3. The podcast and the credits follow, as before.

Expected log lines: `hud: appeared ActionHUDAftermathTextSMG026.AftermathTextInstance ->
AftermathText_C ...` (at the verbose log level), one `SAY announce` per card, and no `ERROR`.

## Overlays

Tester script (Russian UI).

1. When the game puts a phone camera, the binoculars or a rifle scope over the view (Emma's
   phone in Chapter 1, the binoculars at the lodge), its prompt to close it is read once as it
   appears, "ЗАКРЫТЬ, Backspace" (or the gamepad button). The labels of the phone's own
   camera ("PHOTO", "VIDEO", "4:3") are not read; the interaction prompts the game shows
   with the overlay are read as usual.

Expected log lines: `hud: PhoneCameraOverlayLandscape_C shown with the prompt "ЗАКРЫТЬ" ...`
or the same for `BinocularsOverlay_C`, and no `ERROR`.

## Couch co-op, Movie Mode and Wolf Pack

Tester script (Russian UI).

1. Main menu → На одном экране: the character carousel is read as the game names it
   ("ЛОРА. ПРИЛЕЖНАЯ, НЕЗАВИСИМАЯ, РЕШИТЕЛЬНАЯ"), and the screen's prompts end with
   "[ и ] переключают персонажа." (the bumpers after a gamepad press): the carousel's own
   glyphs carry no words, so the mod names what they turn, as it does for the pause menu's
   tab bar ("[ и ] переключают вкладки.") and the Wolf Pack host's mode selector ("[ и ]
   меняют значение."). F8 repeats the prompts. The same line comes in Режим кино at the
   director's chair.
2. Assign characters, start the game: the handover screen is read as the player and the
   character it names and the button under them: "АНДРЕЙ, ЛОРА. Готовы?" The screen has no
   other choice; A or Enter answers it.
3. Волчья стая → Новая игра: the host screen reads its title, the mode selector with its
   description ("Волк-одиночка, селектор, Главный игрок получает право окончательного
   выбора ..."), the members and the prompts. The "test ]" prompt is the game's own label on
   that screen, read as shown. Левый Control ("Профиль") opens the platform's own profile
   overlay outside the game, which the mod cannot read; nothing is missing from the log.
4. In a Wolf Pack game the result the game shows after a member's challenge ("УСПЕХ",
   "НЕУДАЧА") is read as it appears, and the waiting screen reads "Ожидание остальных".

## Credits

Tester script (Russian UI).

1. Start the prologue: over the drive the opening credits show the cast's names one at a time.
   Each name is said the moment it appears, and the last one after the game's word for "and"
   above it: "И Grace Zabriskie". F6 while a name is on screen reads it.
2. At the end of the game, after the podcast, the credits roll. When a roll begins you hear
   "Титры." The first roll, the studio's, has no section titles: press F6 at any moment and every
   line on screen is read from top to bottom, a role and its name together ("Director: Will
   Byles") and a name alone as it is.
3. The second roll begins with "Титры." and "Cast", and every section title is said as it comes up
   from the bottom ("For SoundCuts", "Original Score", ...). For each licensed song only the first
   line of its block is said, the song's name in quotes; F6 reads the whole block. Near the end F6
   reads the logos as "Логотип WWISE", "Логотип SPEEDTREE" and "Логотип UNREAL".
4. Nothing the credits say cuts anything off, and F6 during a roll reads the roll, never the last
   message.

Expected log lines: `credits: MainCredits_C ... shown`, `credits: the roll of CreditsStudio begins`,
`credits: the roll of CreditsOthers begins`, one `credits: section "..."` per title, one
`credits: opening credit "..."` per name, and no `ERROR`.
