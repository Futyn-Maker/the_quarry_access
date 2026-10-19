// hotkeys/HotkeyTable: the one declaration of every key, and the repository's own check that
// the shipped ini and the README carry each of them.
#include "Support.hpp"
#include "hotkeys/HotkeyTable.hpp"
#include "hotkeys/KeySpec.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>

using namespace qa::hotkeys;
using tests::U8;

namespace
{
    std::filesystem::path Repo()
    {
        return std::filesystem::path(QA_REPO_ROOT);
    }

    // A text file as lines with LF endings and no BOM.
    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
        std::string out;
        for (const char c : text)
        {
            if (c != '\r') out += c;
        }
        return out;
    }

    bool HasLine(const std::string& text, const std::string& line)
    {
        return ("\n" + text + "\n").find("\n" + line + "\n") != std::string::npos;
    }

    std::string EscapeRegex(const std::string& text)
    {
        std::string out;
        for (const char c : text)
        {
            if (std::string("+.*?()[]{}|^$\\").find(c) != std::string::npos) out += '\\';
            out += c;
        }
        return out;
    }
}

TEST_CASE("Every command has one row, and every setting name is used once")
{
    std::set<Command> commands;
    std::set<std::wstring> names;
    for (const auto& hotkey : Table())
    {
        INFO(U8(hotkey.name));
        CHECK(commands.insert(hotkey.command).second);
        CHECK(names.insert(hotkey.name).second);
        if (*hotkey.chordName) CHECK(names.insert(hotkey.chordName).second);
        if (*hotkey.padName) CHECK(names.insert(hotkey.padName).second);
        CHECK(Of(hotkey.command).command == hotkey.command);
    }
    CHECK(commands.size() == Table().size());
    CHECK_FALSE(names.contains(std::wstring(kChordHoldSetting)));
}

TEST_CASE("Every default spelling parses, and a chord or pad default is a gamepad key of the engine")
{
    for (const auto& hotkey : Table())
    {
        INFO(U8(hotkey.name));
        KeySpec key;
        if (*hotkey.key) CHECK(ParseKeySpec(hotkey.key, key));
        if (*hotkey.chordName)
            CHECK(std::wstring_view(hotkey.chord).starts_with(L"Gamepad_"));
        else
            CHECK(std::wstring_view(hotkey.chord).empty());
        if (*hotkey.padName)
            CHECK(std::wstring_view(hotkey.pad).starts_with(L"Gamepad_"));
        else
            CHECK(std::wstring_view(hotkey.pad).empty());
    }
    CHECK(kChordHoldDefault.starts_with(L"Gamepad_"));
}

TEST_CASE("The mod's switch is one key of the table, on the keyboard and under the chord")
{
    int switches = 0;
    for (const auto& hotkey : Table())
    {
        if (hotkey.scope != Scope::Switch) continue;
        ++switches;
        CHECK(hotkey.command == Command::ModOnOff);
        CHECK(*hotkey.key);
        CHECK(*hotkey.chordName);
    }
    CHECK(switches == 1);
}

TEST_CASE("The shipped ini sets every key of the table to its default, with the chord hold")
{
    const auto ini = ReadText(Repo() / "mod" / "QuarryAccess.ini");
    REQUIRE_FALSE(ini.empty());
    for (const auto& hotkey : Table())
    {
        INFO(U8(hotkey.name));
        CHECK(HasLine(ini, U8(hotkey.name) + "=" + U8(hotkey.key)));
        if (*hotkey.chordName) CHECK(HasLine(ini, U8(hotkey.chordName) + "=" + U8(hotkey.chord)));
        if (*hotkey.padName) CHECK(HasLine(ini, U8(hotkey.padName) + "=" + U8(hotkey.pad)));
    }
    CHECK(HasLine(ini, U8(kChordHoldSetting) + "=" + U8(kChordHoldDefault)));
}

TEST_CASE("The README's key tables have a row for every key a player gets")
{
    const auto readme = ReadText(Repo() / "README.md");
    REQUIRE_FALSE(readme.empty());
    for (const auto& hotkey : Table())
    {
        if (hotkey.scope == Scope::Dev) continue;
        INFO(U8(hotkey.name));
        // A row of a key table begins with the keyboard key in its first column.
        const std::regex row("(^|\\n)\\| *" + EscapeRegex(U8(hotkey.key)) + " *\\|");
        CHECK(std::regex_search(readme, row));
    }
}
