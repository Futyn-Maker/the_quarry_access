#pragma once
// Small string helpers shared by every module. Wide strings (UTF-16) are the
// working representation because both UE4SS and Tolk use wchar_t.

#include <string>
#include <string_view>
#include <vector>

namespace qa::str
{
    std::wstring Utf8ToWide(std::string_view utf8);
    std::string WideToUtf8(std::wstring_view wide);

    std::wstring Trim(std::wstring_view s);
    std::wstring ToLower(std::wstring_view s);
    std::vector<std::wstring> Split(std::wstring_view s, wchar_t sep, bool trim = true);
    bool StartsWith(std::wstring_view s, std::wstring_view prefix);
    bool EndsWith(std::wstring_view s, std::wstring_view suffix);
    bool EqualsNoCase(std::wstring_view a, std::wstring_view b);
    std::wstring ReplaceAll(std::wstring s, std::wstring_view from, std::wstring_view to);

    // Replaces "{0}", "{1}", ... with the given arguments.
    std::wstring Format(std::wstring_view pattern, const std::vector<std::wstring>& args);

    // Removes rich-text tags such as "<span color=...>" and "</>" and collapses
    // runs of whitespace into single spaces.
    std::wstring StripMarkup(std::wstring_view s);
    std::wstring CollapseWhitespace(std::wstring_view s);

    // Joins non-empty parts with the separator.
    std::wstring Join(const std::vector<std::wstring>& parts, std::wstring_view sep);

    // Joins non-empty parts into spoken sentences: a part that does not already end
    // with punctuation gets a period before the next part is appended.
    std::wstring JoinSentences(const std::vector<std::wstring>& parts);

    // Unescapes "\n" and "\t" sequences from INI values.
    std::wstring UnescapeIni(std::wstring_view s);
}
