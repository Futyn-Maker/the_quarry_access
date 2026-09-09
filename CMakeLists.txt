cmake_minimum_required(VERSION 3.22)

# The Quarry Accessibility Mod (QuarryAccess) — a UE4SS C++ mod that voices
# The Quarry's UI and gameplay prompts through Tolk (NVDA / JAWS / SAPI).
project(QuarryAccessMod LANGUAGES C CXX)

set(QA_VERSION "0.6.0")

# Where the RE-UE4SS source tree lives. Defaults to the pinned git submodule;
# developers with an existing checkout (and its UEPseudo submodule) can point
# here to avoid a second full UE4SS build, e.g.
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DQA_UE4SS_SOURCE_DIR=D:/QuarryTools/RE-UE4SS
set(QA_UE4SS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/RE-UE4SS" CACHE PATH "Path to the RE-UE4SS source tree")

# Optional: game root. When set, a post-build step copies main.dll into
# <game>/SMG026/Binaries/Win64/Mods/QuarryAccess/dlls/ (scripts/deploy.ps1 does the full deploy).
set(QA_GAME_DIR "" CACHE PATH "The Quarry install directory (optional, enables post-build copy)")

if(NOT EXISTS "${QA_UE4SS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "RE-UE4SS source not found at '${QA_UE4SS_SOURCE_DIR}'. "
        "Run 'git submodule update --init --recursive' (requires a GitHub account linked to Epic Games for the UEPseudo submodule) "
        "or pass -DQA_UE4SS_SOURCE_DIR=<path to an existing RE-UE4SS checkout>.")
endif()

add_subdirectory("${QA_UE4SS_SOURCE_DIR}" RE-UE4SS)
add_subdirectory(src)
