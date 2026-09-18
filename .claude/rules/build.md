---
paths:
    - "scripts/**"
    - "CMakeLists.txt"
    - "src/CMakeLists.txt"
    - "CMakePresets.json"
---

# Build, package, deploy

- There is one configuration, `Game__Shipping__Win64` with Ninja (preset `shipping`), and two build targets: `QuarryAccess` (main.dll, which links UE4SS) and `proxy` (dwmapi.dll). UE4SS cannot build as Debug. `build.cmd` configures again when the cache's build type, the UE4SS tree (`build\ue4ss-source.txt`) or the preset (`build\presets-used.json`) differ, which also undoes an IDE that configured `build\` its own way.
- `FETCHCONTENT_UPDATES_DISCONNECTED` is on: the libraries UE4SS downloads are fetched once per build folder. After moving the submodule to a UE4SS commit that changes their versions, delete `build\`.
- `setup.cmd` fetches the submodules over HTTPS, since RE-UE4SS names its own by SSH address; UEPseudo needs a GitHub account linked to Epic Games. `env.cmd` finds Visual Studio through vswhere, puts vswhere's folder on PATH (vcvarsall calls it by name), takes CMake and Ninja from PATH and cargo from `CARGO_HOME`, and refuses a repository path over 78 characters, because UE4SS's sources and build reach 181 characters below it.
- The release's file list lives once, in `Get-QaInstallFiles` (`scripts\common.ps1`), used by both `package.ps1` and `deploy.ps1`: `dwmapi.dll` and Tolk next to the game's exe, UE4SS with `assets\CustomGameConfigs\The Quarry` in `ue4ss\`, the mod with its licenses in `ue4ss\Mods\QuarryAccess`. There is no `mods.txt` and no UE4SS Lua mod: `enabled.txt` starts the mod, and UE4SS starts any mod folder that has one, whatever `mods.txt` says.
- The DLLs record no path of the building machine: `/PDBALTPATH:%_PDB%` on all three, and the sources of UE4SS's Rust scanner remapped to `cargo\...` and `RE-UE4SS\...` (skipped for a path with a space, since RUSTFLAGS splits at spaces).
- UE4SS and main.dll use the dynamic CRT, so players need the Visual C++ 2015-2022 x64 runtime.
- `.gitattributes` checks `.cmd`, `.bat` and `.ps1` files out with CRLF, because cmd.exe misreads labels in LF batch files; keep CRLF when editing them. Scripts must run in Windows PowerShell 5.1 (no `??`, no ternaries, no three-argument `Join-Path`), and there `$ErrorActionPreference = "Stop"` turns a native tool's stderr into a terminating error, so set it only after the build has run.
- Shell commands can collapse doubled backslashes in their text: write backslash-heavy content with the file tools.
