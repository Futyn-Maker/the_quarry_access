#include "speech/SapiVoices.hpp"

#include "core/Strings.hpp"

#include <windows.h>

#include <sapi.h>

namespace qa::sapivoices
{
    namespace
    {
        // The two categories the speech library enumerates, so that both lists hold the same
        // voices: the ordinary SAPI 5 ones and the Speech Platform ones installed beside them.
        const wchar_t* const kCategories[] = {
            SPCAT_VOICES,
            L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech Server\\v11.0\\Voices",
        };

        // A token's default value: the voice's name, which is what the library reports too.
        std::wstring TokenName(ISpObjectToken* token)
        {
            LPWSTR text = nullptr;
            std::wstring name;
            if (token && SUCCEEDED(token->GetStringValue(nullptr, &text)) && text)
            {
                name = text;
                CoTaskMemFree(text);
            }
            return name;
        }

        // The languages a voice lists, as SAPI writes them: "419" or "409;809".
        std::vector<unsigned> TokenLanguages(ISpObjectToken* token)
        {
            std::vector<unsigned> languages;
            ISpDataKey* attributes = nullptr;
            if (!token || FAILED(token->OpenKey(L"Attributes", &attributes)) || !attributes) return languages;
            LPWSTR text = nullptr;
            if (SUCCEEDED(attributes->GetStringValue(L"Language", &text)) && text)
            {
                for (const auto& part : str::Split(text, L';'))
                {
                    try
                    {
                        languages.push_back(static_cast<unsigned>(std::stoul(str::Trim(part), nullptr, 16)));
                    }
                    catch (...)
                    {
                    }
                }
                CoTaskMemFree(text);
            }
            attributes->Release();
            return languages;
        }
    }

    std::unordered_map<std::wstring, std::vector<unsigned>> Languages()
    {
        std::unordered_map<std::wstring, std::vector<unsigned>> languages;
        for (const wchar_t* id : kCategories)
        {
            ISpObjectTokenCategory* category = nullptr;
            if (FAILED(CoCreateInstance(__uuidof(SpObjectTokenCategory), nullptr, CLSCTX_ALL, __uuidof(ISpObjectTokenCategory),
                                        reinterpret_cast<void**>(&category))) ||
                !category)
                continue;
            IEnumSpObjectTokens* tokens = nullptr;
            if (SUCCEEDED(category->SetId(id, FALSE)) && SUCCEEDED(category->EnumTokens(nullptr, nullptr, &tokens)) && tokens)
            {
                ISpObjectToken* token = nullptr;
                while (tokens->Next(1, &token, nullptr) == S_OK && token)
                {
                    std::wstring name = TokenName(token);
                    std::vector<unsigned> spoken = TokenLanguages(token);
                    if (!name.empty() && !spoken.empty()) languages.emplace(std::move(name), std::move(spoken));
                    token->Release();
                    token = nullptr;
                }
                tokens->Release();
            }
            category->Release();
        }
        return languages;
    }
}
