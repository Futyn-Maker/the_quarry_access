#include "core/Strings.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>

namespace qa::str
{
    std::wstring Utf8ToWide(std::string_view utf8)
    {
        if (utf8.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (n <= 0) return {};
        std::wstring out(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
        return out;
    }

    std::string WideToUtf8(std::wstring_view wide)
    {
        if (wide.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string out(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
        return out;
    }

    static bool IsSpace(wchar_t c)
    {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\f' || c == L'\v' || c == 0x00A0;
    }

    std::wstring Trim(std::wstring_view s)
    {
        size_t b = 0, e = s.size();
        while (b < e && IsSpace(s[b]))
            ++b;
        while (e > b && IsSpace(s[e - 1]))
            --e;
        return std::wstring(s.substr(b, e - b));
    }

    std::wstring ToLower(std::wstring_view s)
    {
        if (s.empty()) return {};
        std::wstring out(s.size(), L'\0');
        const int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.data(), static_cast<int>(s.size()), out.data(), static_cast<int>(out.size()),
                                    nullptr, nullptr, 0);
        if (n <= 0) return std::wstring(s);
        out.resize(static_cast<size_t>(n));
        return out;
    }

    std::vector<std::wstring> Split(std::wstring_view s, wchar_t sep, bool trim)
    {
        std::vector<std::wstring> parts;
        size_t start = 0;
        while (start <= s.size())
        {
            const size_t pos = s.find(sep, start);
            const auto piece = s.substr(start, pos == std::wstring_view::npos ? std::wstring_view::npos : pos - start);
            parts.push_back(trim ? Trim(piece) : std::wstring(piece));
            if (pos == std::wstring_view::npos) break;
            start = pos + 1;
        }
        return parts;
    }

    bool StartsWith(std::wstring_view s, std::wstring_view prefix)
    {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    bool EndsWith(std::wstring_view s, std::wstring_view suffix)
    {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool EqualsNoCase(std::wstring_view a, std::wstring_view b)
    {
        if (a.size() != b.size()) return false;
        return ToLower(a) == ToLower(b);
    }

    std::wstring ReplaceAll(std::wstring s, std::wstring_view from, std::wstring_view to)
    {
        if (from.empty()) return s;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::wstring::npos)
        {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        return s;
    }

    std::wstring Format(std::wstring_view pattern, const std::vector<std::wstring>& args)
    {
        std::wstring out;
        out.reserve(pattern.size() + 16);
        for (size_t i = 0; i < pattern.size(); ++i)
        {
            const wchar_t c = pattern[i];
            if (c == L'{' && i + 2 < pattern.size() && iswdigit(pattern[i + 1]))
            {
                size_t j = i + 1;
                int idx = 0;
                while (j < pattern.size() && iswdigit(pattern[j]))
                {
                    idx = idx * 10 + (pattern[j] - L'0');
                    ++j;
                }
                if (j < pattern.size() && pattern[j] == L'}')
                {
                    if (idx >= 0 && static_cast<size_t>(idx) < args.size()) out += args[static_cast<size_t>(idx)];
                    i = j;
                    continue;
                }
            }
            out += c;
        }
        return out;
    }

    std::wstring StripMarkup(std::wstring_view s)
    {
        std::wstring out;
        out.reserve(s.size());
        bool inTag = false;
        for (const wchar_t c : s)
        {
            if (c == L'<')
            {
                inTag = true;
                continue;
            }
            if (c == L'>' && inTag)
            {
                inTag = false;
                out += L' ';
                continue;
            }
            if (!inTag) out += c;
        }
        return CollapseWhitespace(out);
    }

    std::wstring CollapseWhitespace(std::wstring_view s)
    {
        std::wstring out;
        out.reserve(s.size());
        bool pendingSpace = false;
        for (const wchar_t c : s)
        {
            if (IsSpace(c))
            {
                pendingSpace = !out.empty();
                continue;
            }
            if (pendingSpace)
            {
                out += L' ';
                pendingSpace = false;
            }
            out += c;
        }
        return out;
    }

    std::wstring Join(const std::vector<std::wstring>& parts, std::wstring_view sep)
    {
        std::wstring out;
        for (const auto& p : parts)
        {
            if (p.empty()) continue;
            if (!out.empty()) out += sep;
            out += p;
        }
        return out;
    }

    std::wstring JoinWords(const std::vector<std::wstring>& parts)
    {
        std::wstring out;
        for (const auto& part : parts)
        {
            if (part.empty()) continue;
            const wchar_t first = part.front();
            const bool punctuation = first == L',' || first == L'.' || first == L'!' || first == L'?' || first == L':' || first == L';';
            if (!out.empty() && !punctuation) out += L' ';
            out += part;
        }
        return CollapseWhitespace(out);
    }

    std::wstring JoinSentences(const std::vector<std::wstring>& parts)
    {
        std::wstring out;
        for (const auto& raw : parts)
        {
            const std::wstring p = Trim(raw);
            if (p.empty()) continue;
            if (!out.empty())
            {
                const wchar_t last = out.back();
                const bool punctuated =
                    last == L'.' || last == L'!' || last == L'?' || last == L'。' || last == L'！' || last == L'？' || last == L':' || last == L';';
                if (!punctuated) out += L'.';
                out += L' ';
            }
            out += p;
        }
        return out;
    }

    std::wstring UnescapeIni(std::wstring_view s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == L'\\' && i + 1 < s.size())
            {
                const wchar_t n = s[i + 1];
                if (n == L'n')
                {
                    out += L'\n';
                    ++i;
                    continue;
                }
                if (n == L't')
                {
                    out += L'\t';
                    ++i;
                    continue;
                }
                if (n == L'\\')
                {
                    out += L'\\';
                    ++i;
                    continue;
                }
            }
            out += s[i];
        }
        return out;
    }
}
