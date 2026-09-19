---
paths:
    - "src/core/**"
    - "src/hooks/**"
    - "src/watch/**"
    - "src/dllmain.cpp"
---

# Core: reflection, hooks, pollers

- UE4SS API: `FindAllOf` returns subclasses too; `obj::FindProperty` is cached per class; `obj::FindObject` is `StaticFindObject` and never loads anything; `obj::StructMember` must walk the property chain, since members often live in a base struct (`FStringReferenceSMG`); a `TArray` returned by `ProcessEvent` must be destroyed (`std::destroy_at`) after reading; an `FName` argument is built with `std::construct_at` and `FNAME_Add`.
- The ProcessLocalScriptFunction callback sees only functions a Blueprint executes, including C++ events a Blueprint implements, delegates and animation events. `OnNative` fires only when the game calls through the UFunction; direct C++ calls bypass it. So every hook has a polling twin.
- `IsLive` compares the object-array item back to the pointer, because freed and reused memory passes a plain validity check. `obj::Call` refuses non-live targets. Never call `ProcessEvent` on arbitrary scanned widgets.
- `IsLive` is not "still the one on screen". The game rebuilds a menu at every opening and leaves the last set alive until the collector runs, so a cached widget must be checked against being drawn; otherwise everything read from it belongs to a menu that closed half a minute ago.
- The text walk (`obj::DescendantTexts`) reports what the widgets hold, and tests visibility only. It does not test opacity: every list and panel fades in, and a walk during the fade would come back empty. Text a screen keeps drawn but unreadable is left out where that screen is read, by what the game says about it.
- String reads from game memory are guarded: `FStringToWide` checks the length (0 to 65536) and a canonical, even pointer and copies under `SafeInvoke`. A garbage `FActorReference` string read right after a death rewind once crashed the game.
- `SafeInvokeLogged` writes at most one ERROR per 5 seconds. A silently swallowed fault once hid a whole feature: its poller faulted every frame.
- LoadMap resets the watchers' caches, but it never marks leaving a game: the frontend and all scenes share one persistent map.
- The focus watcher stores the new state before calling its listeners, so inside a listener `CurrentFocused()` is already the new widget.
- The focus watcher keeps its last focused control only while `obj::IsWidgetShown`. A closed screen leaves its widgets alive with their flags as they were, and with no screen current `OnCurrentScreen` accepts anything, so a check of the control's own flag kept the clue viewer's card, and the viewer as the current screen, until garbage collection removed it tens of seconds later.
- The focus is the game's, not the mod's: it is given to nothing at all while a screen is taken down, and given back to the screen underneath a popup that is closing, including one the game is already leaving. So a screen that closes on top of another leaves nothing to react to, and whoever knew the screen changed arranges the reading instead.
- A screen stops being the current one the moment it is no longer on display, or its prompts and its text go on being read as it fades, and opening it again is taken for the screen never having left. What "no longer on display" means is the game's answer where there is one, not the widget's flags.
- A poller that must not miss an event reads its state at every tick. Looking only every so often because the lookup is expensive turns an event that opens and closes in between into no event at all: rate-limit the scan that finds the object, never the reading of its state, and keep an answer that costs an object scan for the frame it was found in.
- Blackboard values: `/Script/SMGGameFlow.Default__GFBlackboardBlueprintLibrary` `GetGlobalBlackboardBool/Flag(WorldContextObject, {Variable})` with the live `GFBlackboardVariableBool/Flag` object (`core/Flow`). The variable objects themselves hold only `InitialValue`.
- Only the pinned source build of UE4SS with its official The Quarry config (custom `StaticConstructObject` signature, `VTableLayout.ini`, engine version override, UObject array cache off) is known to work with this game. Keep that config as shipped.
