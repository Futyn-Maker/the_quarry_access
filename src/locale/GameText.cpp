#include "locale/GameText.hpp"

#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"

#include <Unreal/FString.hpp>

#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace qa::gametext
{
    using RC::Unreal::FString;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;

    namespace
    {
        std::mutex g_mutex;
        std::unordered_map<std::wstring, std::wstring> g_cache;
        std::wstring g_locale;
        std::chrono::steady_clock::time_point g_localeAt{};

        UObject* Statics()
        {
            static UObject* cdo = nullptr;
            if (cdo && obj::IsLive(cdo)) return cdo;
            cdo = obj::FindObject(L"/Script/SMG026Runtime.Default__UIStaticsQuarry");
            if (!cdo) log::Verbose(L"gametext: UIStaticsQuarry CDO not found yet");
            return cdo;
        }
    }

    std::wstring CurrentLocale()
    {
        {
            std::lock_guard lock(g_mutex);
            if (!g_locale.empty() && std::chrono::steady_clock::now() - g_localeAt < std::chrono::seconds(5)) return g_locale;
        }
        auto* statics = Statics();
        if (!statics) return {};
        auto* fn = obj::FindFunction(statics, L"GetCurrentLocaleString");
        if (!fn) return {};
        std::wstring result;
        obj::Call(statics, fn, nullptr,
                  [&](void* params)
                  {
                      for (auto* prop : fn->ForEachProperty())
                      {
                          if (prop && prop->GetName() == L"ReturnValue")
                          {
                              obj::ReadStringAt(params, prop, result);
                          }
                      }
                  });
        std::lock_guard lock(g_mutex);
        g_locale = str::Trim(result);
        g_localeAt = std::chrono::steady_clock::now();
        return g_locale;
    }

    std::wstring Resolve(std::wstring_view key)
    {
        if (key.empty()) return {};
        const std::wstring k(key);
        {
            std::lock_guard lock(g_mutex);
            const auto it = g_cache.find(k);
            if (it != g_cache.end()) return it->second;
        }
        auto* statics = Statics();
        if (!statics) return {};
        auto* fn = obj::FindFunction(statics, L"FormatLocaleString");
        if (!fn) return {};
        std::wstring result;
        obj::Call(
            statics, fn,
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (!prop) continue;
                    if (prop->GetName() == L"LocaleString")
                    {
                        // FLocaleString { FString Key; }
                        auto* keyString = prop->ContainerPtrToValuePtr<FString>(params);
                        std::construct_at(keyString, k.c_str());
                    }
                }
            },
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (!prop) continue;
                    if (prop->GetName() == L"ReturnValue") obj::ReadStringAt(params, prop, result);
                }
            });
        // The key FString inside the struct parameter is intentionally not released here:
        // results are cached per key, so the number of leaked small buffers is bounded.
        if (result == k) result.clear();
        std::lock_guard lock(g_mutex);
        g_cache[k] = result;
        return result;
    }

    std::wstring ResolveWithFallback(std::wstring_view key, std::wstring_view fallback)
    {
        const auto resolved = Resolve(key);
        return resolved.empty() ? std::wstring(fallback) : resolved;
    }

    std::wstring ReadLocalized(UObject* object, std::wstring_view localeStringProperty, std::wstring_view fallbackStringProperty)
    {
        std::wstring key;
        std::wstring fallback;
        if (!fallbackStringProperty.empty()) obj::ReadString(object, fallbackStringProperty, fallback);
        if (obj::ReadLocaleKey(object, localeStringProperty, key) && !key.empty())
        {
            return ResolveWithFallback(key, fallback);
        }
        return fallback;
    }

    std::wstring SubstitutePrompt(std::wstring_view text, std::wstring_view keyName)
    {
        return str::CollapseWhitespace(str::ReplaceAll(std::wstring(text), L"$(prompt)", keyName));
    }

    void ClearCache()
    {
        std::lock_guard lock(g_mutex);
        g_cache.clear();
        g_locale.clear();
    }
}
