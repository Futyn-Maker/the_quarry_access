# Hook routing table

This file lists the game functions the mod listens to. Blueprint functions are matched by the
class that declares them (subclasses inherit the match); native functions are hooked by full name.

## Global callbacks

| Callback                                                               | Used for                                          |
| ---------------------------------------------------------------------- | ------------------------------------------------- |
| `Hook::RegisterEngineTickPostCallback`                                 | Game-thread pump and pollers.                     |
| `Hook::RegisterProcessLocalScriptFunctionPostCallback` / `PreCallback` | Routing of Blueprint function executions.         |
| `Hook::RegisterStaticConstructObjectPostCallback`                      | Object-construction listeners (widget discovery). |
| `Hook::RegisterLoadMapPostCallback`                                    | Cache reset on map change.                        |

## Blueprint functions

The current version routes no Blueprint functions; screen and focus detection uses the pollers.

## Native functions

The current version hooks no native game functions.
