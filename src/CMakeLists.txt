set(TARGET QuarryAccess)

file(GLOB_RECURSE QA_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

add_library(${TARGET} SHARED ${QA_SOURCES})

# The Prism speech library is not part of the repository: scripts\build.cmd fetches the release
# that third_party\prism.version pins, and only its header is compiled against. prism.dll itself
# ships beside main.dll and is loaded from there (speech/PrismBridge.cpp).
set(QA_PRISM_DIR "${CMAKE_SOURCE_DIR}/third_party/prism")
if(NOT EXISTS "${QA_PRISM_DIR}/include/prism.h")
    message(FATAL_ERROR
        "The Prism speech library is missing from '${QA_PRISM_DIR}'. Run scripts\\build.cmd, which "
        "fetches it, or scripts\\fetch-prism.ps1 on its own.")
endif()

target_include_directories(${TARGET}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
            "${QA_PRISM_DIR}/include")

target_link_libraries(${TARGET} PUBLIC UE4SS PRIVATE winmm ole32)

target_compile_definitions(${TARGET} PRIVATE
    QA_VERSION_STRING="${QA_VERSION}"
    # Takes the dllimport off Prism's prototypes: the mod resolves the entry points itself and
    # links no import library, so a missing prism.dll is an error the mod reports, not a
    # refusal by Windows to start the game.
    PRISM_STATIC
    RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
    RC_UNREAL_DISABLE_CLASS_DEPRECATION_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE)

set_target_properties(${TARGET} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "main")

if(MSVC)
    target_compile_options(${TARGET} PRIVATE /W4 /utf-8 /permissive-)
    # A linker map next to main.dll lets crash offsets from minidumps be resolved to functions.
    # main.dll names its symbol file without the folder it was built in.
    target_link_options(${TARGET} PRIVATE /MAP /PDBALTPATH:%_PDB%)
endif()
