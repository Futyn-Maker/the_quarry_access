#include "hotkeys/HotkeyTable.hpp"

namespace qa::hotkeys
{
    namespace
    {
        constexpr Hotkey kTable[] = {
            {Command::Repeat, L"Repeat", L"F5", L"ChordRepeat", L"Gamepad_FaceButton_Bottom", L"", L"", Scope::Always},
            {Command::ReadScreen, L"ReadScreen", L"F6", L"ChordReadScreen", L"Gamepad_FaceButton_Left", L"", L"", Scope::Always},
            {Command::Stop, L"Stop", L"F7", L"ChordStop", L"Gamepad_FaceButton_Right", L"", L"", Scope::Always},
            {Command::Help, L"Help", L"F8", L"ChordHelp", L"Gamepad_FaceButton_Top", L"", L"", Scope::Always},
            {Command::Subtitles, L"Subtitles", L"F9", L"ChordSubtitles", L"Gamepad_RightTrigger", L"", L"", Scope::Always},
            {Command::LastSubtitle, L"LastSubtitle", L"F4", L"ChordLastSubtitle", L"Gamepad_RightShoulder", L"", L"", Scope::Always},
            {Command::Speech, L"Speech", L"F3", L"ChordSpeech", L"Gamepad_RightThumbstick", L"", L"", Scope::Always},
            {Command::Verbosity, L"Verbosity", L"F2", L"ChordVerbosity", L"Gamepad_LeftThumbstick", L"", L"", Scope::Always},
            {Command::NextTarget, L"NextTarget", L"N", L"", L"", L"ExploreNext", L"Gamepad_DPad_Right", Scope::Context},
            {Command::PreviousTarget, L"PreviousTarget", L"P", L"", L"", L"ExplorePrevious", L"Gamepad_DPad_Left", Scope::Context},
            {Command::Where, L"Where", L"H", L"", L"", L"ExploreWhere", L"Gamepad_DPad_Up", Scope::Context},
            {Command::Walk, L"Walk", L"G", L"", L"", L"ExploreWalk", L"Gamepad_FaceButton_Left", Scope::Context},
            {Command::Beacon, L"Beacon", L"T", L"", L"", L"ExploreBeacon", L"Gamepad_DPad_Down", Scope::Context},
            {Command::TarotCards, L"TarotCards", L"K", L"", L"", L"ExploreTarotCards", L"Gamepad_RightShoulder", Scope::Context},
            {Command::DevDumpTree, L"DevDumpTree", L"Ctrl+F9", L"ChordDevDumpTree", L"Gamepad_LeftShoulder", L"", L"", Scope::Dev},
            {Command::DevTrace, L"DevTrace", L"Ctrl+F10", L"", L"", L"", L"", Scope::Dev},
            {Command::DevLogLevel, L"DevLogLevel", L"Ctrl+F11", L"ChordDevLogLevel", L"Gamepad_LeftTrigger", L"", L"", Scope::Dev},
        };
    }

    std::span<const Hotkey> Table()
    {
        return kTable;
    }

    const Hotkey& Of(Command command)
    {
        for (const auto& hotkey : kTable)
        {
            if (hotkey.command == command) return hotkey;
        }
        // Every command has its row; the tests hold the table to that.
        return kTable[0];
    }

    std::wstring_view CommandName(Command command)
    {
        return Of(command).name;
    }
}
