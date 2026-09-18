---
paths:
    - "mod/lang/**"
---

# Language tables

- `mod/lang/<code>.ini` exists for each of the game's 20 text languages, named by the game's locale codes (`da_DK` to `zh_CHT`): `[strings]`, then `key=value` lines. `en_US.ini` is the master, and every other file carries exactly its keys (`scripts\check-lang.ps1`). A missing key falls back to English and is logged.
- `mod/lang/tarot/<code>.ini` holds one description per vision film, keyed `tarot.<film file name in lower case>`: dry and factual, no chapter numbers, with character and place names written as the game's own locale table writes them in that language.
- `{0}`, `{1}` and so on are filled in by the mod: keep them. "The Quarry Access" stays untranslated. The `key.*` entries are the spoken names of keys.
- Write a new string in all 20 languages at once, in natural wording for each.
