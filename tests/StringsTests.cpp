// core/Strings: the string helpers every module shares.
#include "Support.hpp"
#include "core/Strings.hpp"

using namespace qa;
using tests::U8;

TEST_CASE("Trim takes the whitespace off both ends, the no-break space among it")
{
    CHECK(U8(str::Trim(L"  a b \t\r\n")) == "a b");
    CHECK(U8(str::Trim(L" x ")) == "x");
    CHECK(str::Trim(L"   ").empty());
    CHECK(str::Trim(L"").empty());
}

TEST_CASE("ToLower lowers Latin and Cyrillic letters alike")
{
    CHECK(U8(str::ToLower(L"Ctrl+F9")) == "ctrl+f9");
    CHECK(U8(str::ToLower(L"ЖЁЛТЫЙ")) == "жёлтый");
    CHECK(str::ToLower(L"").empty());
}

TEST_CASE("Split cuts at the separator and trims the pieces, keeping the empty ones")
{
    const auto parts = str::Split(L" a, b ,,c", L',');
    REQUIRE(parts.size() == 4);
    CHECK(U8(parts[0]) == "a");
    CHECK(U8(parts[1]) == "b");
    CHECK(parts[2].empty());
    CHECK(U8(parts[3]) == "c");

    const auto raw = str::Split(L" a ,b", L',', false);
    REQUIRE(raw.size() == 2);
    CHECK(U8(raw[0]) == " a ");

    CHECK(str::Split(L"", L',').size() == 1);
}

TEST_CASE("StartsWith, EndsWith and EqualsNoCase")
{
    CHECK(str::StartsWith(L"Gamepad_DPad_Up", L"Gamepad_"));
    CHECK_FALSE(str::StartsWith(L"Gamepad", L"Gamepad_"));
    CHECK(str::EndsWith(L"ChoiceWidgetInstance", L"Instance"));
    CHECK_FALSE(str::EndsWith(L"Instance", L"WidgetInstance"));
    CHECK(str::EqualsNoCase(L"ru_RU", L"RU_ru"));
    CHECK_FALSE(str::EqualsNoCase(L"ru_RU", L"ru_R"));
}

TEST_CASE("ReplaceAll replaces every occurrence and leaves a text without the pattern alone")
{
    CHECK(U8(str::ReplaceAll(L"ru-RU-x", L"-", L"_")) == "ru_RU_x");
    CHECK(U8(str::ReplaceAll(L"aaa", L"a", L"aa")) == "aaaaaa");
    CHECK(U8(str::ReplaceAll(L"abc", L"", L"x")) == "abc");
    CHECK(U8(str::ReplaceAll(L"abc", L"z", L"x")) == "abc");
}

TEST_CASE("Format fills the numbered places and leaves everything else as it is")
{
    CHECK(U8(str::Format(L"{0}, {1} m, {2}.", {L"Door", L"12", L"ahead"})) == "Door, 12 m, ahead.");
    CHECK(U8(str::Format(L"{1} before {0}", {L"a", L"b"})) == "b before a");
    CHECK(U8(str::Format(L"{0} and {5}", {L"a"})) == "a and ");
    CHECK(U8(str::Format(L"{x} {0", {L"a"})) == "{x} {0");
    CHECK(U8(str::Format(L"{10}", {L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"ten"})) == "ten");
    CHECK(U8(str::Format(L"plain", {})) == "plain");
}

TEST_CASE("StripMarkup drops the tags and collapses the whitespace they leave")
{
    CHECK(U8(str::StripMarkup(L"<b>Hold</> E")) == "Hold E");
    CHECK(U8(str::StripMarkup(L"a <span color=\"red\">b</span>")) == "a b");
    CHECK(U8(str::StripMarkup(L"a <unclosed")) == "a");
    CHECK(U8(str::StripMarkup(L"5 > 3")) == "5 > 3");
    CHECK(U8(str::StripMarkup(L"  two\n lines ")) == "two lines");
    CHECK(str::StripMarkup(L"<img/>").empty());
}

TEST_CASE("CollapseWhitespace makes one space of any run, none at the ends")
{
    CHECK(U8(str::CollapseWhitespace(L"  a \n\t b  ")) == "a b");
    CHECK(U8(str::CollapseWhitespace(L"a  b")) == "a b");
    CHECK(str::CollapseWhitespace(L" \n ").empty());
}

TEST_CASE("Join skips the empty parts")
{
    CHECK(U8(str::Join({L"a", L"", L"b"}, L", ")) == "a, b");
    CHECK(str::Join({L"", L""}, L", ").empty());
}

TEST_CASE("JoinSentences ends a part with a period unless it already ends with punctuation")
{
    CHECK(U8(str::JoinSentences({L"Choice", L"Go left?", L"", L"Timer"})) == "Choice. Go left? Timer");
    CHECK(U8(str::JoinSentences({L"字幕。", L"次"})) == "字幕。 次");
    CHECK(U8(str::JoinSentences({L" spaced ", L"next"})) == "spaced. next");
    CHECK(str::JoinSentences({}).empty());
}

TEST_CASE("JoinWords puts spaces between words and none before punctuation")
{
    CHECK(U8(str::JoinWords({L"Hold", L"E", L", then", L"go"})) == "Hold E, then go");
    CHECK(U8(str::JoinWords({L"", L"Use", L"", L"F"})) == "Use F");
    CHECK(U8(str::JoinWords({L"Interrupt", L"?"})) == "Interrupt?");
}

TEST_CASE("UnescapeIni turns the escapes of a value into the characters they name")
{
    CHECK(U8(str::UnescapeIni(L"a\\nb\\tc\\\\d")) == "a\nb\tc\\d");
    CHECK(U8(str::UnescapeIni(L"\\x\\")) == "\\x\\");
}

TEST_CASE("UTF-8 and UTF-16 round-trip, Cyrillic, CJK and emoji included")
{
    const std::string utf8 = "Привет, мир. 日本語 🎵";
    CHECK(str::WideToUtf8(str::Utf8ToWide(utf8)) == utf8);
    CHECK(str::Utf8ToWide("").empty());
    CHECK(str::WideToUtf8(L"").empty());
}
