#pragma once
// QuarryAccess.ini reader and the typed settings structure filled from it.

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace qa::cfg
{
    enum class Verbosity
    {
        Minimal,
        Normal,
        Full,
    };

    struct Settings
    {
        // [General]
        Verbosity verbosity = Verbosity::Full;
        std::wstring language = L"auto";
        bool speakOnLoad = true;
        std::wstring logLevel = L"Info";
        bool logFile = true;

        // [Hotkeys]
        std::wstring keyRepeat = L"F5";
        std::wstring keyReadScreen = L"F6";
        std::wstring keyStop = L"F7";
        std::wstring keyHelp = L"F8";
        std::wstring keySubtitles = L"F9";
        std::wstring keyLastSubtitle = L"F4";
        std::wstring keyDevDumpTree = L"Ctrl+F9";
        std::wstring keyDevTrace = L"Ctrl+F10";
        std::wstring keyDevLogLevel = L"Ctrl+F11";
        std::wstring padChordHold = L"Gamepad_Special_Left";
        std::wstring padRepeat = L"Gamepad_DPad_Up";
        std::wstring padReadScreen = L"Gamepad_DPad_Right";
        std::wstring padStop = L"Gamepad_DPad_Down";
        std::wstring padHelp = L"Gamepad_DPad_Left";
        std::wstring padSubtitles = L"Gamepad_RightThumbstick";
        std::wstring padLastSubtitle = L"Gamepad_LeftThumbstick";

        // [Speech]
        int focusDedupeMs = 150;

        // [Sounds]
        int soundVolume = 30;

        // [Subtitles]
        bool readSubtitles = true;

        // [Features]
        std::map<std::wstring, bool> features;

        // [Diag]
        std::vector<std::wstring> traceClassPrefixes{L"Menu", L"Pause",   L"Choice", L"Prompt",       L"Subtitle", L"QTE",
                                                     L"Mash", L"Breathe", L"Popup",  L"Notification", L"Reading"};

        bool FeatureEnabled(std::wstring_view name) const;
    };

    // Generic INI document: section -> key -> value (keys are case-insensitive).
    class Ini
    {
    public:
        bool Load(const std::wstring& path, std::wstring* error = nullptr);
        bool Has(std::wstring_view section, std::wstring_view key) const;
        std::wstring Get(std::wstring_view section, std::wstring_view key, std::wstring_view def = L"") const;
        int GetInt(std::wstring_view section, std::wstring_view key, int def) const;
        bool GetBool(std::wstring_view section, std::wstring_view key, bool def) const;
        std::vector<int> GetIntList(std::wstring_view section, std::wstring_view key, std::vector<int> def) const;
        std::vector<std::wstring> GetList(std::wstring_view section, std::wstring_view key, std::vector<std::wstring> def) const;
        const std::map<std::wstring, std::wstring>* Section(std::wstring_view section) const;

    private:
        std::map<std::wstring, std::map<std::wstring, std::wstring>> m_sections;
    };

    // Loads QuarryAccess.ini into Settings (missing keys keep defaults).
    Settings LoadSettings(const std::wstring& path, std::wstring* error = nullptr);

    const Settings& Get();
    void Set(Settings settings);
}
