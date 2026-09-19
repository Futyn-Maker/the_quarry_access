---
paths:
    - "mod/lang/**"
    - "src/locale/**"
    - "src/dllmain.cpp"
---

# Language tables

- `mod/lang/<code>.ini` exists for each of the game's 20 text languages, named by the game's locale codes (`da_DK` to `zh_CHT`): `[strings]`, then `key=value` lines. `en_US.ini` is the master, and every other file carries exactly its keys (`scripts\check-lang.ps1`). A missing key falls back to English and is logged.
- `mod/lang/tarot/<code>.ini` holds one description per vision film, keyed `tarot.<film file name in lower case>`: dry and factual, no chapter numbers, with character and place names written as the game's own locale table writes them in that language.
- `{0}`, `{1}` and so on are filled in by the mod: keep them. "The Quarry Access" stays untranslated. The `key.*` entries are the spoken names of keys.
- Write a new string in all 20 languages at once, in natural wording for each.
- The game starts on the Windows locale and applies its own language (Steam's, `GetPlatformLocaleString`) about four seconds after launch; `UIStaticsQuarry::GetCurrentLocaleString` and `FormatLocaleString` both change with it. The mod takes the locale only once it has held for two seconds, then follows it for the whole session: a change clears the resolved game strings (`gametext::ClearCache`), reloads the mod's tables and picks the SAPI voice anew. A cache of resolved game strings must never outlive a language.
