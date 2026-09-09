#include "core/Config.hpp"
#include "core/Strings.hpp"

#include <fstream>
#include <iterator>
#include <mutex>

namespace qa::cfg
{
    namespace
    {
        Settings g_settings;
        std::mutex g_mutex;

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
        s.keyDevDumpTree = ini.Get(L"Hotkeys", L"DevDumpTree", s.keyDevDumpTree);
        s.keyDevTrace = ini.Get(L"Hotkeys", L"DevTrace", s.keyDevTrace);
        s.keyDevLogLevel = ini.Get(L"Hotkeys", L"DevLogLevel", s.keyDevLogLevel);
        s.padChordHold = ini.Get(L"Hotkeys", L"PadChordHold", s.padChordHold);
        s.padRepeat = ini.Get(L"Hotkeys", L"PadRepeat", s.padRepeat);
        s.padReadScreen = ini.Get(L"Hotkeys", L"PadReadScreen", s.padReadScreen);
        s.padStop = ini.Get(L"Hotkeys", L"PadStop", s.padStop);
        s.padHelp = ini.Get(L"Hotkeys", L"PadHelp", s.padHelp);

        s.focusDedupeMs = ini.GetInt(L"Speech", L"FocusDedupeMs", s.focusDedupeMs);
        s.maxQueuedSubtitles = ini.GetInt(L"Speech", L"MaxQueuedSubtitles", s.maxQueuedSubtitles);

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
