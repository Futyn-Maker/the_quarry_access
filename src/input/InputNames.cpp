#include "input/InputNames.hpp"

#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "locale/Locale.hpp"

#include <Unreal/Core/Containers/Array.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>

#include <map>
#include <memory>
#include <mutex>

namespace qa::input
{
    using RC::Unreal::FName;
    using RC::Unreal::FString;
    using RC::Unreal::FText;
    using RC::Unreal::TArray;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;

    namespace
    {
        std::mutex g_mutex;
        std::map<std::pair<int, std::wstring>, std::wstring> g_actionCache;
        std::map<std::wstring, std::wstring> g_keyNameCache;
        UObject* g_widget = nullptr; // any live USMGUIUserWidgetBase for GetKeysFromActionMapping

        constexpr size_t kFKeySize = 0x18; // FName (8) + TSharedPtr<FKeyDetails> (16) in UE 4.26

        UObject* OwningPlayer(UObject* widget)
        {
            auto* fn = obj::FindFunction(widget, L"GetOwningPlayer");
            if (!fn) return nullptr;
            UObject* result = nullptr;
            obj::Call(widget, fn, nullptr,
                      [&](void* params)
                      {
                          for (auto* prop : fn->ForEachProperty())
                          {
                              if (prop && prop->GetName() == L"ReturnValue") obj::ReadObjectAt(params, prop, result);
                          }
                      });
            return result;
        }

        // A live UI widget that belongs to the local player; the native key-mapping
        // helpers dereference the owning player, so unowned widgets must not be used.
        UObject* AnyUiWidget()
        {
            if (g_widget && obj::IsLive(g_widget) && OwningPlayer(g_widget)) return g_widget;
            g_widget = nullptr;
            for (auto* widget : obj::FindAllLive(L"SMGUIUserWidgetBase"))
            {
                if (!obj::IsWidgetShown(widget)) continue;
                if (OwningPlayer(widget))
                {
                    g_widget = widget;
                    break;
                }
            }
            log::Verbose(L"input: key-mapping widget {} {}", obj::ClassName(g_widget), obj::ObjectName(g_widget));
            return g_widget;
        }

        std::wstring Humanize(std::wstring_view keyName)
        {
            std::wstring s(keyName);
            if (str::StartsWith(s, L"Gamepad_")) s.erase(0, 8);
            // Split CamelCase and underscores into words.
            std::wstring out;
            for (size_t i = 0; i < s.size(); ++i)
            {
                const wchar_t c = s[i];
                if (c == L'_')
                {
                    out += L' ';
                    continue;
                }
                if (i > 0 && iswupper(c) && iswlower(s[i - 1])) out += L' ';
                out += c;
            }
            return str::CollapseWhitespace(out);
        }

        std::wstring EngineDisplayName(std::wstring_view keyName)
        {
            auto* lib = obj::FindObject(L"/Script/Engine.Default__KismetInputLibrary");
            if (!lib) return {};
            auto* fn = obj::FindFunction(lib, L"Key_GetDisplayName");
            if (!fn) return {};
            std::wstring result;
            const std::wstring name(keyName);
            obj::Call(
                lib, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Key")
                        {
                            auto* key = prop->ContainerPtrToValuePtr<uint8_t>(params);
                            std::construct_at(reinterpret_cast<FName*>(key), name.c_str(), RC::Unreal::FNAME_Add);
                        }
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadStringAt(params, prop, result);
                    }
                });
            return result;
        }
    }

    Scheme CurrentScheme()
    {
        auto* pc = obj::LocalPlayerController();
        if (!pc) return Scheme::Unknown;
        int64_t scheme = 0;
        if (!obj::ReadInt(pc, L"CurrentControlScheme", scheme)) return Scheme::Unknown;
        switch (scheme)
        {
        case 1: return Scheme::Gamepad;
        case 2: return Scheme::MouseKeyboard;
        case 3: return Scheme::Touch;
        default: return Scheme::Unknown;
        }
    }

    std::wstring SchemeName(Scheme scheme)
    {
        switch (scheme)
        {
        case Scheme::Gamepad: return L"Gamepad";
        case Scheme::MouseKeyboard: return L"MouseKeyboard";
        case Scheme::Touch: return L"Touch";
        default: return L"Unknown";
        }
    }

    std::wstring KeyDisplayName(std::wstring_view keyName)
    {
        if (keyName.empty() || keyName == L"None") return {};
        const std::wstring name(keyName);
        {
            std::lock_guard lock(g_mutex);
            const auto it = g_keyNameCache.find(name);
            if (it != g_keyNameCache.end()) return it->second;
        }
        // 1. Mod table (localized, curated names).
        const std::wstring localeKey = L"key." + str::ToLower(name);
        std::wstring result;
        if (locale::Has(localeKey)) result = locale::Mod(localeKey);
        // 2. Engine display name.
        if (result.empty()) result = EngineDisplayName(name);
        // 3. Humanized engine key name.
        if (result.empty()) result = Humanize(name);
        std::lock_guard lock(g_mutex);
        g_keyNameCache[name] = result;
        return result;
    }

    std::wstring KeyForAction(std::wstring_view actionName)
    {
        const Scheme scheme = CurrentScheme();
        const std::wstring action(actionName);
        const auto cacheKey = std::make_pair(static_cast<int>(scheme), action);
        {
            std::lock_guard lock(g_mutex);
            const auto it = g_actionCache.find(cacheKey);
            if (it != g_actionCache.end()) return it->second;
        }
        auto* widget = AnyUiWidget();
        if (!widget)
        {
            log::Verbose(L"input: no owned UI widget yet; cannot resolve {}", action);
            return {};
        }
        auto* fn = obj::FindFunction(widget, L"GetKeysFromActionMapping");
        if (!fn) return {};
        log::Verbose(L"input: resolving {} via {} {} (params {} bytes)", action, obj::ClassName(widget), obj::ObjectName(widget), fn->GetPropertiesSize());

        std::vector<std::wstring> keys;
        obj::Call(
            widget, fn,
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (!prop) continue;
                    if (obj::PropertyTypeName(prop) == L"StructProperty" &&
                        !prop->HasAnyPropertyFlags(static_cast<uint64_t>(RC::Unreal::EPropertyFlags::CPF_ReturnParm)))
                    {
                        // FActionMappingReference { FString Value; }
                        auto* value = prop->ContainerPtrToValuePtr<FString>(params);
                        std::construct_at(value, action.c_str());
                    }
                }
            },
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (!prop || prop->GetName() != L"ReturnValue") continue;
                    auto* arr = prop->ContainerPtrToValuePtr<TArray<uint8_t>>(params);
                    if (!arr || arr->Num() <= 0 || !arr->GetData()) continue;
                    const int32_t n = std::min<int32_t>(arr->Num(), 16);
                    for (int32_t i = 0; i < n; ++i)
                    {
                        auto* key = reinterpret_cast<FName*>(arr->GetData() + static_cast<size_t>(i) * kFKeySize);
                        keys.push_back(key->ToString());
                    }
                }
            });

        std::wstring chosen;
        const bool wantPad = scheme == Scheme::Gamepad;
        for (const auto& k : keys)
        {
            const bool isPad = str::StartsWith(k, L"Gamepad_");
            const bool isTouch = str::StartsWith(k, L"Touch");
            if (isTouch) continue;
            if (isPad == wantPad)
            {
                chosen = k;
                break;
            }
        }
        if (chosen.empty() && !keys.empty()) chosen = keys.front();
        const std::wstring display = KeyDisplayName(chosen);
        log::Verbose(L"input: action {} -> keys [{}] -> \"{}\" (scheme {})", action, str::Join(keys, L", "), display, SchemeName(scheme));
        std::lock_guard lock(g_mutex);
        g_actionCache[cacheKey] = display;
        return display;
    }

    std::wstring PointerName()
    {
        return locale::Mod(CurrentScheme() == Scheme::Gamepad ? L"input.rightstick" : L"input.mouse");
    }

    void InvalidateCache()
    {
        std::lock_guard lock(g_mutex);
        g_actionCache.clear();
        g_keyNameCache.clear();
        g_widget = nullptr;
    }
}
