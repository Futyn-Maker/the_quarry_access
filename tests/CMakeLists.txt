cmake_minimum_required(VERSION 3.22)

# The tests of the mod's engine-free modules: a project of its own, so that those modules are
# compiled without UE4SS and stay free of it. scripts\test.cmd configures it into build\tests
# and runs the result.
project(QuarryAccessTests LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

get_filename_component(QA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(QA_SRC "${QA_ROOT}/src")

add_executable(tests
    Main.cpp
    Support.cpp
    LogStub.cpp
    StringsTests.cpp
    ConfigTests.cpp
    LocaleTests.cpp
    KeySpecTests.cpp
    HotkeyTableTests.cpp
    ExploreTextTests.cpp
    "${QA_SRC}/core/Strings.cpp"
    "${QA_SRC}/core/Config.cpp"
    "${QA_SRC}/locale/Locale.cpp"
    "${QA_SRC}/hotkeys/KeySpec.cpp"
    "${QA_SRC}/hotkeys/HotkeyTable.cpp"
    "${QA_SRC}/features/ExploreText.cpp")

target_include_directories(tests PRIVATE "${QA_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tests PRIVATE doctest::doctest)
# The repository's root, for the tests that read the shipped ini and the README.
target_compile_definitions(tests PRIVATE
    "QA_REPO_ROOT=\"${QA_ROOT}\""
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE)
set_target_properties(tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(tests PRIVATE /W4 /utf-8 /permissive-)
endif()

enable_testing()
add_test(NAME tests COMMAND tests)
