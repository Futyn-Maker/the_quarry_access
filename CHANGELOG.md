# Changelog

## Unreleased

- A way on is reached by walking into it. The place a scene watches is a volume, and a volume's pivot can stand metres away from the space it fills: at the Chapter 9 scrapyard the crane's pivot is more than two metres outside its trigger, so the walk and the beacon stopped at the pivot and said the way was reached while the scene was still waiting. A way is now placed at the middle of the box its collision fills, the walk heads for the floor inside that box, and arrival is the engine's own test of the character's position against the volume's collision, so it is said exactly when the character stands in the trigger. Whether a way lies above or below is taken from that box as well.
- A way the player has chosen stays in the list while it is walked to, even when the character's new position puts it on the road to something else; before, it could drop out halfway and end the walk. A way that returns to the list after the road filter hid it is not announced as new again.
- The way to a thing ends on the floor it is used from. The engine's navigation settles on the ground nearest across, whatever its height within the search, so a thing hung on a wall above lower ground could send the beacon and the walk to the ground behind the wall, and the route stopped short where nothing could be reached; the board in the Chapter 7 police station office was one. The thing's own floor is now looked for first, within the step the scene's navigation mesh lets a character climb, and only then further up and down.
- A way on that the scene has already used is no longer offered. The scene keeps its own record of what has happened, and a way it has moved past is closed by one of its own flags, so walking there again sets nothing off; each way is checked against those flags as the scene holds them, and one whose flag says its moment has passed drops out. Ways that set off an intermediate scene and then return to the same exploration drop out only once that has happened, the others stay, and things the player can see and use are never removed this way. A way whose conditions the game does not let anyone read stays on offer rather than being dropped on a guess.
- A walk on a gamepad pushes the stick the game itself reads instead of pressing the movement keys, so the device in the player's hands stays the device the game draws its prompts for. The game reads its gamepad through its own copy of XInput; the mod puts itself in the middle of that reading, fills in the left stick while a walk is on, and passes everything else through untouched, so a walk is the ordinary one and the player's own hands are still told apart from it. Where the game is reading no gamepad the walk uses the movement keys as before, and the log says which of the two it is.
- A way on is offered only where it leads somewhere of its own: a way the character walks through on its road to something the scene already names is left out, because that journey sets it off anyway. The test is the game's own, with no distance chosen for it: the walking route from the navigation mesh against the box the trigger occupies, widened by the character's own width, so a way is dropped only where walking to something else really does enter it. A way standing to one side of every route, or beyond where they all end, is a journey in its own right and is still offered, and a way whose box cannot be read stays on offer rather than being dropped on a guess.
- A target standing on another floor is said to be higher up or lower down, and the way there is looked for at its own height rather than on the floor below it, so walking to something on a balcony no longer ends underneath it.
- A walk that stops getting nearer ends instead of circling the spot for ever.
- A Don't Breathe shown by a tutorial is called what it is: the game plays those out on a fixed clock and ends them the same way whatever the player does, so the moment one begins it is said to be a demonstration, and no advice is given during it.
- Running out of air while the creature is still near is said differently from running out once it has moved off, because the two want opposite things of the player.
- Don't Breathe says where the danger stands, which is what the game shows in its picture and plays in its sound as the creature draws near: a rising tone when a stretch of danger has passed and the breath may be let go, a falling tone when the next one is about to begin, and a word when the breath is nearly out. The blips still follow what is left of it.

## 0.7.0

- Exploration: while the character is under the player's control, the things the scene lets the player walk to (its use locations and the places it shows) are listed as they come into reach, each with its label as the game names it, its distance and its direction from the camera's point of view; a use location standing at a place takes the place's name ("Листовка: Осмотреть"), twins are listed once, and the list keeps its order so N and P cycle predictably; the nearest is targeted by itself, N and P move the target and H says it again; a beacon leads along the walkable route from the navigation mesh, sounding on the side of the next turn, rising as the target comes closer and dropping an octave when the way is behind (T turns it off and on, `[Exploration]` in the ini tunes it); the distance said for the target is the length of the route; reaching a target is announced once, and the beacon resumes if the player walks away again.
- The keys of the actions the game shows are named the way its keyboard remapping shows them: an action in a remap row (confirm, interact, cancel, movement) is named by the row's key, so a prompt bound to Confirm says the left mouse button, as the game draws it.
- The glyph the game floats over a use location is read with the name that use location has among the things to walk to, so the thing the game is offering is called what it was called a moment ago; a prompt whose words have faded is no longer read as a bare key. Where several things stand together, each takes the place nearest to it, closest pair first, rather than whichever the character happened to meet first. Reaching a use location now means the game is offering it or the character is standing within its trigger, rather than being a metre and a half away, so it is announced exactly when a press would reach it, however the character got there; walking to one of several things on a desk carries on until that one is the thing a press would use.
- Everything the scene has opened up is listed, not only what is within a few steps: the game names the use locations it turns on, so they are followed from the moment the scene allows them. Each is named after the thing rather than the action, taking the place beside it ("Листовка: Осмотреть"), the text the scene gave it, or the name it carries in the level read as words ("Camp Map: Взаимодействовать"), because the game itself has only four words for every interaction in it.
- A short exchange in the middle of a scene no longer starts the list again: what was around stays, and only what the scene adds is announced.
- The ways on that a scene is waiting for are offered alongside the things to use, as "Путь дальше" with a distance and a direction. Many scenes move forward only when the character walks into a place their own logic watches, and those places carry no marker, no glint and no name on screen for anyone; they are found by asking the scene which ones it is watching right now, and they are named by where they are, never by what they are called, so nothing they hold is given away. Where a scene offers nothing at all, how far the walkable ground goes is said instead of silence.
- The gamepad reaches the exploration keys on buttons of their own while the player walks the character freely, where the game leaves them unused: the D-pad chooses what to walk to (right and left), says where it is (up) and turns the beacon on and off (down), and X walks there. The same four also answer the Back chord anywhere, and the context help names the buttons of the device in the player's hands.
- The character can walk to the chosen target by itself: G on the keyboard, X on the gamepad, following the same route the beacon leads along and stopping on arrival, on the next press, or as soon as the player takes the controls back. It holds down the game's own movement keys, the ones its input settings list for the character's movement, so the walk is the ordinary one in every respect: the same speed, the same collisions, the same triggers, and the game takes the keys back the instant it wants them. Where the way ahead yields nothing for a moment the heading leans to one side and then the other, the way a player feels around a doorway. Half a second passes between the word and the first step, so the reader is heard before anything moves (`WalkDelayMs` in the ini).
- A scene that asks the player to look around is guided the same way: the beacon leads the camera to the nearest glint, and finding something is announced.
- Notes and letters opened in the reading pane are read with their title and page count, again when the page turns; F8 names the page and close keys.
- F6 lists what is nearby; F8 explains the beacon and names the game's own keys for looking around and picking a place.
- Quick-time events name the key of the direction again when the widget names no action.
- The keys of an action are taken from the engine's input settings whenever no menu screen is on display, so nothing is asked of a widget the game has already torn down; a hotkey or a developer command that still runs into a memory fault is abandoned and logged instead of taking the game down, and a poller that faults says so in the log.
- A popup button is read when the game lights up the panel inside it rather than the button itself.
- Prompt events (the input action and style each prompt is set up with) are written to the log.

## 0.6.0

- Quick-time events: the direction the arrow asks for is said the moment the event appears, with the key when the game shows a key cap, and played as a cue at the same time (notes climbing for up, falling for down, a note in the left or the right ear for the sides); both are repeated every second while the arrow stays on screen; a rising tone marks a hit, a falling one a miss.
- The cues play louder by default (`[Sounds] Volume` 80).
- Button mash: the button is said the way the game's Button Mash setting wants it pressed (mashed, held, tapped, or automatic); blips of rising and falling pitch follow the ring as it fills and empties; the outcome is said and played.
- Don't Breathe: the prompt to hold the breath and the prompt to release it are read as the game shows them, with the key; blips follow the breath bars while the breath is held; the outcome is said and played.
- F6 reads the event, the button or the prompt on screen; F8 names the keys of a quick-time event, the button to mash and the key to hold.

## 0.5.0

- Choices: a choice is read as a timed or plain choice with its question, each option with its place and its two labels, and the option of not responding when the choice offers it; the key hints are read when the game shows them; the option the player is holding is announced as the game highlights it, the option chosen once the key is released; the seconds left are read once the game shows them.
- Four-way choices are read option by option with their places; the countdown that warns of a coming choice is read when it appears and when its wording changes; the timer bar announces itself.
- Button prompts (interactions, interruptions, use locations, combat) are read with their label and key as they appear; prompts for the mouse or the stick name the device.
- A short tone marks the moment a choice commits (`[Sounds] Volume`), so the key can be released; the chosen option is spoken once the key is released.
- The subtitle reading switch (F9) is saved to the ini, so the game starts the way it was left.
- F6 reads the choice or the prompts on screen; F8 names the keys of a choice.

## 0.4.0

- Subtitles: each line is read once as it appears, exactly as the game displays it under its subtitle settings (character names, hyphens and closed captions included, nothing when subtitles are off); a subtitle wrapped over two lines is read as one; lines are spoken after whatever the reader is already saying and are never cut short or dropped; F6 reads the lines on screen.
- Hotkeys: F9 turns subtitle reading off and on during play (`[Subtitles] Read` sets the start value), F4 repeats the last subtitle line even after it has gone, until the main menu is back; on a gamepad, Back with the right or the left stick click.

## 0.3.0

- Pause menu: opening it announces that the game is paused, the selected tab and its place among the tabs; switching tabs reads the new tab, its objective line, the selected entry and the prompts; clues, evidence, tarot cards and tutorials are read with their descriptions.
- Popups and dialogs read their message after the title; rewind and couch co-op screens are covered.
- Sections opened inside a screen are read like a screen; a control placed in a titled row (the director's chair switches) takes the row's title and position; the character carousel reads the character it shows.
- HUD: scene captions, notifications with their hot-link prompt, alerts and act titles are read when they appear; loading and saving are announced.
- Read-screen (F6) reads everything a screen shows when nothing on it is selected.

## 0.2.0

- Menus: the focused item is announced with its label, control type, state, position and description line; a screen that opens is read as title, selection, then prompt bar, and the prompt line always closes the readout.
- Settings: selector and slider rows announce their row name, changing a value speaks the new value, and key-binding and tutorial rows are read like every other row.
- Speech only interrupts what the player has overtaken with their own key, button or mouse input; anything the game says on its own waits its turn.
- Read-screen (F6) and help (F8) include the menu state and the available prompts.
- Developer command file `Mods\QuarryAccess\command.txt` (trace, dump, log level, say).

## 0.1.0

- UE4SS C++ mod foundation with Tolk speech (NVDA, JAWS, SAPI fallback; speech and braille).
- Configuration file `QuarryAccess.ini` and string tables for all 20 game languages.
- Hotkeys: F5 repeat, F6 read screen, F7 stop, F8 help; gamepad chord Back + D-pad.
- Diagnostics: Ctrl+F9 screen dump, Ctrl+F10 function tracer, Ctrl+F11 log level.
- Game-thread pump, hook dispatcher, HUD/text/focus watchers, game-string resolver, key-name resolver.
