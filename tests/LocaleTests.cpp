// locale/Locale: the mod's own strings, in the game's language over English.
#include "Support.hpp"
#include "locale/Locale.hpp"

using namespace qa;
using tests::TempDir;
using tests::U8;

namespace
{
    // A language folder with English, a Russian overlay that lacks some keys, and the tarot
    // tables of both.
    void WriteTables(const TempDir& dir)
    {
        dir.Write("lang/en_US.ini",
                  "\xEF\xBB\xBF; English\n[strings]\ngreeting=Hello {0}.\nonly.english=Only here.\nkey.up=Up arrow\nlines=One\\nTwo\\tThree\\\\four\n");
        dir.Write("lang/ru_RU.ini", "\xEF\xBB\xBF[strings]\nGreeting=Привет, {0}.\nlines=Раз\n");
        dir.Write("lang/tarot/en_US.ini", "[strings]\ntarot.fool=The Fool.\n");
        dir.Write("lang/tarot/ru_RU.ini", "[strings]\ntarot.fool=Шут.\n");
    }
}

TEST_CASE("LoadTables lays the game's language over English and falls back key by key")
{
    TempDir dir;
    WriteTables(dir);
    REQUIRE(locale::LoadTables((dir.Path() / "lang").wstring(), L"ru-RU"));
    CHECK(U8(locale::CurrentCode()) == "ru_RU");
    CHECK(U8(locale::Mod(L"greeting", L"Andrey")) == "Привет, Andrey.");
    CHECK(U8(locale::Mod(L"GREETING", L"x")) == "Привет, x.");
    CHECK(U8(locale::Mod(L"only.english")) == "Only here.");
    CHECK(U8(locale::Mod(L"tarot.fool")) == "Шут.");
    CHECK(locale::Has(L"key.up"));
    CHECK_FALSE(locale::Has(L"nothing"));
    CHECK(locale::Count() == 5);
    const auto missing = locale::MissingKeys();
    CHECK(missing.size() == 2);
}

TEST_CASE("A key missing everywhere is said in brackets and logged once")
{
    TempDir dir;
    WriteTables(dir);
    REQUIRE(locale::LoadTables((dir.Path() / "lang").wstring(), L"ru_RU"));
    tests::ClearLog();
    CHECK(U8(locale::Mod(L"no.such")) == "[no.such]");
    CHECK(U8(locale::Mod(L"no.such", L"arg")) == "[no.such]");
    CHECK(tests::LogLinesWith(L"missing string 'no.such'") == 1);
}

TEST_CASE("A language without a table keeps English and says so")
{
    TempDir dir;
    WriteTables(dir);
    tests::ClearLog();
    REQUIRE(locale::LoadTables((dir.Path() / "lang").wstring(), L"xx_XX"));
    CHECK(U8(locale::CurrentCode()) == "en_US");
    CHECK(U8(locale::Mod(L"greeting", L"you")) == "Hello you.");
    CHECK(tests::LogLinesWith(L"no table for xx_XX") == 1);
    CHECK(locale::MissingKeys().empty());
}

TEST_CASE("The escapes of a value become the characters they name, and English stays English")
{
    TempDir dir;
    WriteTables(dir);
    REQUIRE(locale::LoadTables((dir.Path() / "lang").wstring(), L"en_US"));
    CHECK(U8(locale::Mod(L"lines")) == "One\nTwo\tThree\\four");
    CHECK(U8(locale::CurrentCode()) == "en_US");
}

TEST_CASE("Without an English table nothing loads, and the log says so")
{
    TempDir dir;
    tests::ClearLog();
    CHECK_FALSE(locale::LoadTables((dir.Path() / "lang").wstring(), L"ru_RU"));
    CHECK(tests::LogLinesWith(L"cannot load") == 1);
}
