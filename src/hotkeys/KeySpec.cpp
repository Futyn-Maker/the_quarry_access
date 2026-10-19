#include "hotkeys/KeySpec.hpp"

#include "core/Strings.hpp"

#include <cwctype>
#include <map>
#include <string>

namespace qa::hotkeys
{
    namespace
    {
        const std::map<std::wstring, int> kNamedKeys = {
            {L"space", 0x20},  {L"enter", 0x0D},  {L"return", 0x0D}, {L"escape", 0x1B}, {L"esc", 0x1B},      {L"tab", 0x09},      {L"backspace", 0x08},
            {L"insert", 0x2D}, {L"delete", 0x2E}, {L"home", 0x24},   {L"end", 0x23},    {L"pageup", 0x21},   {L"pagedown", 0x22}, {L"up", 0x26},
            {L"down", 0x28},   {L"left", 0x25},   {L"right", 0x27},  {L"pause", 0x13},  {L"capslock", 0x14}, {L"numlock", 0x90},  {L"scrolllock", 0x91},
        };
    }

    bool ParseKeySpec(std::wstring_view spec, KeySpec& out)
    {
        out = KeySpec{};
        const auto lower = str::ToLower(str::Trim(spec));
        if (lower.empty() || lower == L"none") return false;
        for (auto& token : str::Split(lower, L'+'))
        {
            if (token == L"ctrl" || token == L"control")
            {
                out.ctrl = true;
                continue;
            }
            if (token == L"alt")
            {
                out.alt = true;
                continue;
            }
            if (token == L"shift")
            {
                out.shift = true;
                continue;
            }
            int vk = 0;
            if (token.size() >= 2 && token[0] == L'f' && iswdigit(token[1]))
            {
                const int n = std::stoi(token.substr(1));
                if (n >= 1 && n <= 24) vk = 0x70 + (n - 1);
            }
            else if (token.size() == 1 && token[0] >= L'a' && token[0] <= L'z')
            {
                vk = 0x41 + (token[0] - L'a');
            }
            else if (token.size() == 1 && iswdigit(token[0]))
            {
                vk = 0x30 + (token[0] - L'0');
            }
            else if (str::StartsWith(token, L"numpad") && token.size() == 7 && iswdigit(token[6]))
            {
                vk = 0x60 + (token[6] - L'0');
            }
            else if (const auto it = kNamedKeys.find(token); it != kNamedKeys.end())
            {
                vk = it->second;
            }
            if (vk != 0) out.vk = vk;
        }
        return out.vk != 0;
    }
}
