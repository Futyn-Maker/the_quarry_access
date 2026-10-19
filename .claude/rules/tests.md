---
paths:
    - "tests/**"
    - "scripts/test.cmd"
---

# Tests

- `tests\` is a CMake project of its own (`tests\CMakeLists.txt`), configured and built by `scripts\test.cmd` into `build\tests` with the same MSVC and Ninja as the mod, and run there. It compiles the engine-free sources straight from `src\` (`core/Strings`, `core/Config`, `locale/Locale`, `hotkeys/KeySpec`, `hotkeys/HotkeyTable`, `features/ExploreText`) against `tests\LogStub.cpp`, which stands in for `core/Log` and keeps the lines for assertions, and links nothing of UE4SS. Compiling them standalone is what keeps them engine-free: a module that cannot be compiled this way touches the game and is tested in it.
- The framework is doctest, fetched by CMake at the first configure (`FetchContent`, the tag pinned in `tests\CMakeLists.txt`); `tests\Main.cpp` provides the main. One file per module, named `<Module>Tests.cpp`; a `TEST_CASE` is named for the behaviour it holds, in the words of the module's header.
- Tests go through the module's public functions as the mod calls them, never through its internals, so that a rewrite keeps the test. Expected strings are compared as UTF-8 (`U8()` in `tests\Support.hpp`) so that a failure prints readably.
- Files a test needs go under `std::filesystem::temp_directory_path()` in a folder of their own, written in binary with the endings the test chooses and removed at the end (`TempDir` in `tests\Support.hpp`); `cfg::Persist` and `locale::LoadTables` are tested on such files. The settings and the locale keep process-wide state, so a case loads what it needs itself rather than relying on the case before it.
- `HotkeyTableTests.cpp` is the repository's own check: every command once in the table, every default spelling parsed, and the shipped `mod\QuarryAccess.ini` and `README.md` (found through `QA_REPO_ROOT`, which CMake defines) carrying each key, the ini its settings at the table's defaults and the README a row for each key a player gets. A failure there is answered in the ini or the README, not in the test.
- A change to a tested module carries its test in the same commit; a bug gets the test that shows it before the fix. The command is `scripts\test.cmd`, which ends with `TESTS_OK` or `TESTS_FAILED`.
