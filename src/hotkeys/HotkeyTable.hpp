#pragma once
// Every key the mod takes, declared once: the command, the names of its settings in
// [Hotkeys] of QuarryAccess.ini, its defaults on the keyboard, under the gamepad chord and
// on the pad's own buttons, and where it works. The settings reader, the bindings, the help
// and the tests all read this table, and the tests hold the shipped ini and the README to
// it, so a key is added or moved here and nowhere else in the code.

#include <span>
#include <string_view>

namespace qa::hotkeys
{
    enum class Command
    {
        Repeat,
        ReadScreen,
        Stop,
        Help,
        Subtitles,
        LastSubtitle,
        Speech,
        Verbosity,
        NextTarget,
        PreviousTarget,
        Where,
        Walk,
        Beacon,
        TarotCards,
        DevDumpTree,
        DevTrace,
        DevLogLevel,
    };

    // Where a key works.
    enum class Scope
    {
        Always,  // wherever the game is in front
        Context, // only while the player walks the character, fights or looks around; the game's otherwise
        Dev,     // a developer's key: everywhere, and not in the README
    };

    struct Hotkey
    {
        Command command;
        const wchar_t* name;      // the keyboard setting in [Hotkeys], and the command's name in the log
        const wchar_t* key;       // its default, a spelling such as "F5" or "Ctrl+F9"; empty for none
        const wchar_t* chordName; // the setting of the button pressed under the chord hold; empty when the command has no chord
        const wchar_t* chord;     // its default, an engine key name such as "Gamepad_FaceButton_Bottom"
        const wchar_t* padName;   // the setting of the pad button answered without the chord in the key's context; empty when none
        const wchar_t* pad;       // its default
        Scope scope;
    };

    // The setting of the button held for the chords, and its default.
    constexpr std::wstring_view kChordHoldSetting = L"ChordHold";
    constexpr std::wstring_view kChordHoldDefault = L"Gamepad_Special_Left";

    // The table, in the order of the help and of the README's tables.
    std::span<const Hotkey> Table();
    const Hotkey& Of(Command command);
    std::wstring_view CommandName(Command command);
}
