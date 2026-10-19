#pragma once
// QuarryAccess.ini reader and the typed settings structure filled from it.

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace qa::cfg
{
    // How much the mod says of its own. What the game itself shows is read either way.
    enum class Verbosity
    {
        Brief,
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

        // [Hotkeys]: every setting of the section by its name in lower case, as it was
        // written. The defaults are the hotkey table's (hotkeys/HotkeyTable), which asks for
        // a setting through Hotkey().
        std::map<std::wstring, std::wstring> hotkeys;
        // Keys of earlier versions found in the file, for a line in the log.
        std::vector<std::wstring> obsoleteKeys;

        // [Speech]
        int focusDedupeMs = 150;
        bool preferSapi = false; // SAPI speaks even while a screen reader is running

        // [Sounds]
        int soundVolume = 80;

        // [Subtitles]
        bool readSubtitles = true;

        // [Exploration]
        bool beacon = true;
        int beaconIntervalMs = 500;
        int exploreRange = 100; // metres
        bool autoTarget = true;
        int walkDelayMs = 500;   // between the word and the first step
        bool tarotCards = false; // the places of the tarot cards listed as ways on

        // [Combat]
        bool aimSound = true;

        // [Features]
        std::map<std::wstring, bool> features;

        // [Diag]
        std::vector<std::wstring> traceClassPrefixes{L"Menu", L"Pause",   L"Choice", L"Prompt",       L"Subtitle", L"QTE",
                                                     L"Mash", L"Breathe", L"Popup",  L"Notification", L"Reading"};

        bool FeatureEnabled(std::wstring_view name) const;
        // A [Hotkeys] setting by name, or the default given when the file does not set it.
        std::wstring Hotkey(std::wstring_view name, std::wstring_view def) const;
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

    // Loads QuarryAccess.ini into Settings (missing keys keep defaults) and remembers the
    // path for Persist.
    Settings LoadSettings(const std::wstring& path, std::wstring* error = nullptr);

    // Writes one value back into the ini file that was loaded, keeping every other line as
    // it is; the key is added to its section when missing. False when the file cannot be
    // written.
    bool Persist(std::wstring_view section, std::wstring_view key, std::wstring_view value);

    const Settings& Get();
    void Set(Settings settings);

    // True while the mod says everything it has to say. The game's own words are read
    // whatever this answers.
    bool Detailed();

    // Turns the mod's own messages between brief and detailed, keeping the choice for the
    // next session. Returns true when it is detailed from now on.
    bool ToggleVerbosity();
}
