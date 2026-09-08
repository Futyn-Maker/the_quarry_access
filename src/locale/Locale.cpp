#include "locale/Locale.hpp"

#include "core/Config.hpp"
#include "core/Log.hpp"
#include "core/Strings.hpp"

#include <map>
#include <mutex>
#include <set>

namespace qa::locale
{
    namespace
    {
        std::mutex g_mutex;
        std::map<std::wstring, std::wstring> g_fallback;
        std::map<std::wstring, std::wstring> g_current;
        std::wstring g_code = L"en_US";
        std::set<std::wstring> g_reportedMissing;
        std::vector<std::wstring> g_missingInOverlay;

        bool LoadTable(const std::wstring& path, std::map<std::wstring, std::wstring>& out)
        {
            cfg::Ini ini;
            std::wstring error;
            if (!ini.Load(path, &error))
            {
                log::Verbose(L"locale: {}", error);
                return false;
            }
            const auto* section = ini.Section(L"strings");
            if (!section) return false;
            for (const auto& [k, v] : *section)
            {
                out[k] = str::UnescapeIni(v);
            }
            return true;
        }

        std::wstring LookupLocked(std::wstring_view key)
        {
            const std::wstring k = str::ToLower(key);
            if (const auto it = g_current.find(k); it != g_current.end()) return it->second;
            if (const auto it = g_fallback.find(k); it != g_fallback.end()) return it->second;
            if (g_reportedMissing.insert(k).second)
            {
                log::Error(L"locale: missing string '{}'", key);
            }
            return L"[" + std::wstring(key) + L"]";
        }
    }

    bool LoadTables(const std::wstring& langDir, std::wstring_view localeCode)
    {
        std::lock_guard lock(g_mutex);
        g_fallback.clear();
        g_current.clear();
        g_missingInOverlay.clear();
        g_reportedMissing.clear();

        const bool okFallback = LoadTable(langDir + L"\\en_US.ini", g_fallback);
        if (!okFallback)
        {
            log::Error(L"locale: cannot load {}\\en_US.ini", langDir);
        }
        g_code = L"en_US";
        // The game reports locales as "ru-RU"; tables are named "ru_RU".
        std::wstring code = str::ReplaceAll(str::Trim(localeCode), L"-", L"_");
        if (!code.empty() && !str::EqualsNoCase(code, L"en_US"))
        {
            if (LoadTable(langDir + L"\\" + code + L".ini", g_current))
            {
                g_code = code;
                for (const auto& [k, v] : g_fallback)
                {
                    if (!g_current.contains(k)) g_missingInOverlay.push_back(k);
                }
                if (!g_missingInOverlay.empty())
                {
                    log::Info(L"locale: {} keys missing in {} (English used)", g_missingInOverlay.size(), code);
                }
            }
            else
            {
                log::Info(L"locale: no table for {}, using en_US", code);
            }
        }
        log::Info(L"locale: loaded {} strings, language {}", g_fallback.size(), g_code);
        return okFallback;
    }

    std::wstring CurrentCode()
    {
        std::lock_guard lock(g_mutex);
        return g_code;
    }

    std::wstring Mod(std::wstring_view key)
    {
        std::lock_guard lock(g_mutex);
        return LookupLocked(key);
    }

    std::wstring Mod(std::wstring_view key, const std::vector<std::wstring>& args)
    {
        std::wstring pattern;
        {
            std::lock_guard lock(g_mutex);
            pattern = LookupLocked(key);
        }
        return str::Format(pattern, args);
    }

    std::wstring Mod(std::wstring_view key, std::wstring_view arg0)
    {
        return Mod(key, std::vector<std::wstring>{std::wstring(arg0)});
    }

    std::wstring Mod(std::wstring_view key, std::wstring_view arg0, std::wstring_view arg1)
    {
        return Mod(key, std::vector<std::wstring>{std::wstring(arg0), std::wstring(arg1)});
    }

    bool Has(std::wstring_view key)
    {
        std::lock_guard lock(g_mutex);
        const std::wstring k = str::ToLower(key);
        return g_current.contains(k) || g_fallback.contains(k);
    }

    size_t Count()
    {
        std::lock_guard lock(g_mutex);
        return g_fallback.size();
    }

    std::vector<std::wstring> MissingKeys()
    {
        std::lock_guard lock(g_mutex);
        return g_missingInOverlay;
    }
}
