#include "core/Config.hpp"
#include "core/Strings.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

namespace qa::cfg
{
    namespace
    {
        Settings g_settings;
        std::mutex g_mutex;
        std::wstring g_path; // the ini file the settings came from

        std::wstring Key(std::wstring_view s)
        {
            return str::ToLower(str::Trim(s));
        }
    }

    bool Settings::FeatureEnabled(std::wstring_view name) const
    {
        const auto it = features.find(str::ToLower(name));
        return it == features.end() ? true : it->second;
    }

    bool Ini::Load(const std::wstring& path, std::wstring* error)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            if (error) *error = L"cannot open " + path;
            return false;
        }
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
            static_cast<unsigned char>(bytes[2]) == 0xBF)
        {
            bytes.erase(0, 3);
        }
        const std::wstring text = str::Utf8ToWide(bytes);

        std::wstring section;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find(L'\n', start);
            if (end == std::wstring::npos) end = text.size();
            std::wstring line = str::Trim(std::wstring_view(text).substr(start, end - start));
            start = end + 1;
            if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
            if (line.front() == L'[' && line.back() == L']')
            {
                section = Key(std::wstring_view(line).substr(1, line.size() - 2));
                continue;
            }
            const size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            const std::wstring key = Key(std::wstring_view(line).substr(0, eq));
            std::wstring value = str::Trim(std::wstring_view(line).substr(eq + 1));
            // Strip a trailing comment only when it is preceded by whitespace ("value ; note").
            const size_t semi = value.find(L" ;");
            if (semi != std::wstring::npos) value = str::Trim(std::wstring_view(value).substr(0, semi));
            m_sections[section][key] = value;
        }
        return true;
    }

    bool Ini::Has(std::wstring_view section, std::wstring_view key) const
    {
        const auto s = m_sections.find(Key(section));
        return s != m_sections.end() && s->second.contains(Key(key));
    }

    std::wstring Ini::Get(std::wstring_view section, std::wstring_view key, std::wstring_view def) const
    {
        const auto s = m_sections.find(Key(section));
        if (s == m_sections.end()) return std::wstring(def);
        const auto k = s->second.find(Key(key));
        return k == s->second.end() ? std::wstring(def) : k->second;
    }

    int Ini::GetInt(std::wstring_view section, std::wstring_view key, int def) const
    {
        const auto v = Get(section, key);
        if (v.empty()) return def;
        try
        {
            return std::stoi(v);
        }
        catch (...)
        {
            return def;
        }
    }

    bool Ini::GetBool(std::wstring_view section, std::wstring_view key, bool def) const
    {
        const auto v = str::ToLower(Get(section, key));
        if (v.empty()) return def;
        if (v == L"1" || v == L"true" || v == L"yes" || v == L"on") return true;
        if (v == L"0" || v == L"false" || v == L"no" || v == L"off") return false;
        return def;
    }

    std::vector<int> Ini::GetIntList(std::wstring_view section, std::wstring_view key, std::vector<int> def) const
    {
        const auto v = Get(section, key);
        if (v.empty()) return def;
        std::vector<int> out;
        for (const auto& part : str::Split(v, L','))
        {
            if (part.empty()) continue;
            try
            {
                out.push_back(std::stoi(part));
            }
            catch (...)
            {
            }
        }
        return out.empty() ? def : out;
    }

    std::vector<std::wstring> Ini::GetList(std::wstring_view section, std::wstring_view key, std::vector<std::wstring> def) const
    {
        const auto v = Get(section, key);
        if (v.empty()) return def;
        std::vector<std::wstring> out;
        for (auto& part : str::Split(v, L','))
        {
            if (!part.empty()) out.push_back(part);
        }
        return out.empty() ? def : out;
    }

    const std::map<std::wstring, std::wstring>* Ini::Section(std::wstring_view section) const
    {
        const auto s = m_sections.find(Key(section));
        return s == m_sections.end() ? nullptr : &s->second;
    }

    Settings LoadSettings(const std::wstring& path, std::wstring* error)
    {
        {
            std::lock_guard lock(g_mutex);
            g_path = path;
        }
        Settings s;
        Ini ini;
        if (!ini.Load(path, error)) return s;

        const auto verbosity = str::ToLower(ini.Get(L"General", L"Verbosity", L"Full"));
        if (verbosity == L"minimal")
            s.verbosity = Verbosity::Minimal;
        else if (verbosity == L"normal")
            s.verbosity = Verbosity::Normal;
        else
            s.verbosity = Verbosity::Full;
        s.language = ini.Get(L"General", L"Language", L"auto");
        s.speakOnLoad = ini.GetBool(L"General", L"SpeakOnLoad", true);
        s.logLevel = ini.Get(L"General", L"LogLevel", L"Info");
        s.logFile = ini.GetBool(L"General", L"LogFile", true);

        s.keyRepeat = ini.Get(L"Hotkeys", L"Repeat", s.keyRepeat);
        s.keyReadScreen = ini.Get(L"Hotkeys", L"ReadScreen", s.keyReadScreen);
        s.keyStop = ini.Get(L"Hotkeys", L"Stop", s.keyStop);
        s.keyHelp = ini.Get(L"Hotkeys", L"Help", s.keyHelp);
        s.keySubtitles = ini.Get(L"Hotkeys", L"Subtitles", s.keySubtitles);
        s.keyLastSubtitle = ini.Get(L"Hotkeys", L"LastSubtitle", s.keyLastSubtitle);
        s.keyDevDumpTree = ini.Get(L"Hotkeys", L"DevDumpTree", s.keyDevDumpTree);
        s.keyDevTrace = ini.Get(L"Hotkeys", L"DevTrace", s.keyDevTrace);
        s.keyDevLogLevel = ini.Get(L"Hotkeys", L"DevLogLevel", s.keyDevLogLevel);
        s.padChordHold = ini.Get(L"Hotkeys", L"PadChordHold", s.padChordHold);
        s.padRepeat = ini.Get(L"Hotkeys", L"PadRepeat", s.padRepeat);
        s.padReadScreen = ini.Get(L"Hotkeys", L"PadReadScreen", s.padReadScreen);
        s.padStop = ini.Get(L"Hotkeys", L"PadStop", s.padStop);
        s.padHelp = ini.Get(L"Hotkeys", L"PadHelp", s.padHelp);
        s.padSubtitles = ini.Get(L"Hotkeys", L"PadSubtitles", s.padSubtitles);
        s.padLastSubtitle = ini.Get(L"Hotkeys", L"PadLastSubtitle", s.padLastSubtitle);
        s.keyNextTarget = ini.Get(L"Hotkeys", L"NextTarget", s.keyNextTarget);
        s.keyPreviousTarget = ini.Get(L"Hotkeys", L"PreviousTarget", s.keyPreviousTarget);
        s.keyBeacon = ini.Get(L"Hotkeys", L"Beacon", s.keyBeacon);
        s.keyWhere = ini.Get(L"Hotkeys", L"Where", s.keyWhere);
        s.keyWalk = ini.Get(L"Hotkeys", L"Walk", s.keyWalk);
        s.padNextTarget = ini.Get(L"Hotkeys", L"PadNextTarget", s.padNextTarget);
        s.padPreviousTarget = ini.Get(L"Hotkeys", L"PadPreviousTarget", s.padPreviousTarget);
        s.padBeacon = ini.Get(L"Hotkeys", L"PadBeacon", s.padBeacon);
        s.padWhere = ini.Get(L"Hotkeys", L"PadWhere", s.padWhere);
        s.padExploreNext = ini.Get(L"Hotkeys", L"ExploreNext", s.padExploreNext);
        s.padExplorePrevious = ini.Get(L"Hotkeys", L"ExplorePrevious", s.padExplorePrevious);
        s.padExploreWhere = ini.Get(L"Hotkeys", L"ExploreWhere", s.padExploreWhere);
        s.padExploreBeacon = ini.Get(L"Hotkeys", L"ExploreBeacon", s.padExploreBeacon);
        s.padExploreWalk = ini.Get(L"Hotkeys", L"ExploreWalk", s.padExploreWalk);

        s.focusDedupeMs = ini.GetInt(L"Speech", L"FocusDedupeMs", s.focusDedupeMs);
        s.soundVolume = ini.GetInt(L"Sounds", L"Volume", s.soundVolume);
        s.readSubtitles = ini.GetBool(L"Subtitles", L"Read", s.readSubtitles);
        s.beacon = ini.GetBool(L"Exploration", L"Beacon", s.beacon);
        s.beaconIntervalMs = ini.GetInt(L"Exploration", L"BeaconIntervalMs", s.beaconIntervalMs);
        s.exploreRange = ini.GetInt(L"Exploration", L"Range", s.exploreRange);
        s.autoTarget = ini.GetBool(L"Exploration", L"AutoTarget", s.autoTarget);
        s.walkDelayMs = ini.GetInt(L"Exploration", L"WalkDelayMs", s.walkDelayMs);

        if (const auto* features = ini.Section(L"Features"))
        {
            for (const auto& [k, v] : *features)
            {
                s.features[k] = ini.GetBool(L"Features", k, true);
            }
        }

        s.traceClassPrefixes = ini.GetList(L"Diag", L"TraceClassPrefixes", s.traceClassPrefixes);
        return s;
    }

    bool Persist(std::wstring_view section, std::wstring_view key, std::wstring_view value)
    {
        std::lock_guard lock(g_mutex);
        if (g_path.empty()) return false;
        std::ifstream in(g_path, std::ios::binary);
        if (!in) return false;
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        std::string bom;
        if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
            static_cast<unsigned char>(bytes[2]) == 0xBF)
        {
            bom = bytes.substr(0, 3);
            bytes.erase(0, 3);
        }
        const std::string newline = bytes.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        std::vector<std::wstring> lines;
        for (auto& line : str::Split(str::Utf8ToWide(bytes), L'\n', false))
        {
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            lines.push_back(line);
        }
        const std::wstring wanted = std::wstring(key) + L"=" + std::wstring(value);
        std::wstring current;
        size_t sectionEnd = lines.size(); // where a missing key is inserted
        bool inSection = false;
        bool written = false;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const std::wstring line = str::Trim(lines[i]);
            if (!line.empty() && line.front() == L'[' && line.back() == L']')
            {
                if (inSection) break;
                current = Key(std::wstring_view(line).substr(1, line.size() - 2));
                inSection = current == Key(section);
                sectionEnd = i + 1;
                continue;
            }
            if (!inSection) continue;
            if (!line.empty() && line.front() != L';' && line.front() != L'#') sectionEnd = i + 1;
            const size_t eq = line.find(L'=');
            if (eq != std::wstring::npos && Key(std::wstring_view(line).substr(0, eq)) == Key(key))
            {
                lines[i] = wanted;
                written = true;
                break;
            }
        }
        if (!written)
        {
            if (current != Key(section))
            {
                if (!lines.empty() && !str::Trim(lines.back()).empty()) lines.push_back(L"");
                lines.push_back(L"[" + std::wstring(section) + L"]");
                sectionEnd = lines.size();
            }
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(std::min(sectionEnd, lines.size())), wanted);
        }
        std::string out = bom;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            out += str::WideToUtf8(lines[i]);
            if (i + 1 < lines.size()) out += newline;
        }
        std::ofstream file(g_path, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << out;
        return static_cast<bool>(file);
    }

    const Settings& Get()
    {
        return g_settings;
    }

    void Set(Settings settings)
    {
        std::lock_guard lock(g_mutex);
        g_settings = std::move(settings);
    }
}
