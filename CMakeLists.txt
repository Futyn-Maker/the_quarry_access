cmake_minimum_required(VERSION 3.22)

# The Quarry Access (QuarryAccess) — a UE4SS C++ mod that voices
# The Quarry's UI and gameplay prompts through Tolk (NVDA / JAWS / SAPI).
project(QuarryAccessMod LANGUAGES C CXX)

# The mod's version is the first line of the VERSION file and is written nowhere else: the
# mod reports it at start and the release folder carries it in its name.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" QA_VERSION LIMIT_COUNT 1)
string(STRIP "${QA_VERSION}" QA_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

# The RE-UE4SS source tree. The mod and UE4SS.dll are built from the same tree, since the mod
# links against it; the default is the pinned submodule.
set(QA_UE4SS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/RE-UE4SS" CACHE PATH "Path to the RE-UE4SS source tree")

if(NOT EXISTS "${QA_UE4SS_SOURCE_DIR}/deps/first/Unreal/CMakeLists.txt")
    message(FATAL_ERROR
        "The RE-UE4SS source tree at '${QA_UE4SS_SOURCE_DIR}' is incomplete. "
        "Run scripts\\setup.cmd to fetch the submodules (UE4SS's UEPseudo submodule needs a "
        "GitHub account linked to Epic Games), or pass -DQA_UE4SS_SOURCE_DIR=<an RE-UE4SS checkout>.")
endif()

add_subdirectory("${QA_UE4SS_SOURCE_DIR}" RE-UE4SS)
add_subdirectory(src)
