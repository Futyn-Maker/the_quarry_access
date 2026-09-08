#pragma once
// Resolves the game's own localized strings (FLocaleString keys) through the
// game's UIStaticsQuarry functions, and reports the game's text language.

#include <Unreal/UObject.hpp>

#include <string>
#include <string_view>

namespace qa::gametext
{
    // Current text locale code as the game reports it ("ru_RU"), empty before the
    // frontend has initialized.
    std::wstring CurrentLocale();

    // Resolves a locale key ("SMG_HUD_MENU_BUTTON_NEWGAME_000001") to display text.
    // Returns an empty string when the key is unknown/empty.
    std::wstring Resolve(std::wstring_view key);

    // Resolves a key, falling back to a non-localized string when the key is empty or unknown.
    std::wstring ResolveWithFallback(std::wstring_view key, std::wstring_view fallback);

    // Reads an FLocaleString property from an object and resolves it.
    std::wstring ReadLocalized(RC::Unreal::UObject* object, std::wstring_view localeStringProperty, std::wstring_view fallbackStringProperty = L"");

    // Replaces "$(prompt)" placeholders with the given key name.
    std::wstring SubstitutePrompt(std::wstring_view text, std::wstring_view keyName);

    // Drops cached resolutions (call when the language changes).
    void ClearCache();
}
