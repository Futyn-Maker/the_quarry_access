// core/Config: the ini reader, the settings and the writer that keeps a choice in the file.
#include "Support.hpp"
#include "core/Config.hpp"

using namespace qa;
using tests::TempDir;
using tests::U8;

TEST_CASE("Ini reads sections and keys without regard to case, past comments and a BOM")
{
    TempDir dir;
    const auto path = dir.Write("a.ini", "\xEF\xBB\xBF; comment\n[General]\nVerbosity = Brief ; note\n# hash\nLanguage=ru_RU\n\n"
                                         "[Hotkeys]\nRepeat=F5\nList=a, b,,c\nNumbers=1,2,x,3\nFlag=yes\nOff=false\nnoequals\n");
    cfg::Ini ini;
    std::wstring error;
    REQUIRE(ini.Load(path.wstring(), &error));
    CHECK(error.empty());
    CHECK(U8(ini.Get(L"general", L"VERBOSITY")) == "Brief");
    CHECK(U8(ini.Get(L"General", L"Language")) == "ru_RU");
    CHECK(U8(ini.Get(L"General", L"Missing", L"def")) == "def");
    CHECK(U8(ini.Get(L"Nowhere", L"Missing", L"def")) == "def");
    CHECK(ini.Has(L"Hotkeys", L"repeat"));
    CHECK_FALSE(ini.Has(L"Hotkeys", L"noequals"));
    CHECK(ini.GetBool(L"Hotkeys", L"Flag", false));
    CHECK_FALSE(ini.GetBool(L"Hotkeys", L"Off", true));
    CHECK(ini.GetBool(L"Hotkeys", L"Repeat", true));
    CHECK(ini.GetInt(L"Hotkeys", L"Repeat", 7) == 7);
    CHECK(ini.GetInt(L"Hotkeys", L"Missing", 7) == 7);
    const auto list = ini.GetList(L"Hotkeys", L"List", {});
    REQUIRE(list.size() == 3);
    CHECK(U8(list[1]) == "b");
    const auto numbers = ini.GetIntList(L"Hotkeys", L"Numbers", {});
    REQUIRE(numbers.size() == 3);
    CHECK(numbers[2] == 3);
    const auto* section = ini.Section(L"HOTKEYS");
    REQUIRE(section != nullptr);
    CHECK(section->contains(L"repeat"));
    CHECK(ini.Section(L"Nowhere") == nullptr);
}

TEST_CASE("Ini says which file it could not open")
{
    TempDir dir;
    cfg::Ini ini;
    std::wstring error;
    CHECK_FALSE(ini.Load((dir.Path() / "missing.ini").wstring(), &error));
    CHECK(error.find(L"missing.ini") != std::wstring::npos);
}

TEST_CASE("LoadSettings keeps the defaults for what the file does not set and reads what it does")
{
    TempDir dir;
    const auto path =
        dir.Write("QuarryAccess.ini", "[General]\nVerbosity=brief\n[Hotkeys]\nRepeat=Ctrl+F5\nPadRepeat=X\n[Features]\nMenus=0\n[Exploration]\nRange=250\n");
    std::wstring error;
    const auto s = cfg::LoadSettings(path.wstring(), &error);
    CHECK(error.empty());
    CHECK(s.verbosity == cfg::Verbosity::Brief);
    CHECK(U8(s.Hotkey(L"Repeat", L"F5")) == "Ctrl+F5");
    CHECK(U8(s.Hotkey(L"repeat", L"F5")) == "Ctrl+F5");
    CHECK(U8(s.Hotkey(L"ReadScreen", L"F6")) == "F6");
    REQUIRE(s.obsoleteKeys.size() == 1);
    CHECK(U8(s.obsoleteKeys[0]) == "PadRepeat");
    CHECK_FALSE(s.FeatureEnabled(L"Menus"));
    CHECK_FALSE(s.FeatureEnabled(L"menus"));
    CHECK(s.FeatureEnabled(L"Qte"));
    CHECK(s.exploreRange == 250);
    CHECK(s.beaconIntervalMs == 500);
    CHECK(U8(s.language) == "auto");
}

TEST_CASE("LoadSettings on a missing file keeps every default and says so")
{
    TempDir dir;
    std::wstring error;
    const auto s = cfg::LoadSettings((dir.Path() / "QuarryAccess.ini").wstring(), &error);
    CHECK_FALSE(error.empty());
    CHECK(s.verbosity == cfg::Verbosity::Full);
    CHECK(U8(s.Hotkey(L"Repeat", L"F5")) == "F5");
    CHECK(s.features.empty());
}

TEST_CASE("Verbosity takes the names of earlier versions as brief and anything else as full")
{
    TempDir dir;
    std::wstring error;
    for (const char* name : {"minimal", "normal", "Brief"})
    {
        const auto path = dir.Write("v.ini", std::string("[General]\nVerbosity=") + name + "\n");
        CHECK(cfg::LoadSettings(path.wstring(), &error).verbosity == cfg::Verbosity::Brief);
    }
    for (const char* name : {"Full", "detailed", ""})
    {
        const auto path = dir.Write("v.ini", std::string("[General]\nVerbosity=") + name + "\n");
        CHECK(cfg::LoadSettings(path.wstring(), &error).verbosity == cfg::Verbosity::Full);
    }
}

TEST_CASE("Persist rewrites one value and leaves every other line, the comments, the BOM and the endings as they were")
{
    TempDir dir;
    const std::string original = "\xEF\xBB\xBF; head\r\n[General]\r\n; which level\r\nVerbosity=Full\r\nLanguage=auto\r\n\r\n[Speech]\r\nPreferSapi=0\r\n";
    const auto path = dir.Write("QuarryAccess.ini", original);
    std::wstring error;
    cfg::LoadSettings(path.wstring(), &error);
    REQUIRE(cfg::Persist(L"General", L"Verbosity", L"Brief"));
    CHECK(dir.Read("QuarryAccess.ini") ==
          "\xEF\xBB\xBF; head\r\n[General]\r\n; which level\r\nVerbosity=Brief\r\nLanguage=auto\r\n\r\n[Speech]\r\nPreferSapi=0\r\n");
}

TEST_CASE("Persist adds a missing key at the end of its section and a missing section at the end of the file")
{
    TempDir dir;
    const auto path = dir.Write("QuarryAccess.ini", "[General]\nVerbosity=Full\n\n[Speech]\nPreferSapi=0\n");
    std::wstring error;
    cfg::LoadSettings(path.wstring(), &error);
    REQUIRE(cfg::Persist(L"General", L"Language", L"ru_RU"));
    CHECK(dir.Read("QuarryAccess.ini") == "[General]\nVerbosity=Full\nLanguage=ru_RU\n\n[Speech]\nPreferSapi=0\n");
    REQUIRE(cfg::Persist(L"Exploration", L"Beacon", L"0"));
    CHECK(dir.Read("QuarryAccess.ini") == "[General]\nVerbosity=Full\nLanguage=ru_RU\n\n[Speech]\nPreferSapi=0\n\n[Exploration]\nBeacon=0");
}

TEST_CASE("ToggleVerbosity turns the level over and keeps it in the file")
{
    TempDir dir;
    const auto path = dir.Write("QuarryAccess.ini", "[General]\nVerbosity=Full\n");
    std::wstring error;
    cfg::Set(cfg::LoadSettings(path.wstring(), &error));
    CHECK(cfg::Detailed());
    CHECK_FALSE(cfg::ToggleVerbosity());
    CHECK_FALSE(cfg::Detailed());
    CHECK(dir.Read("QuarryAccess.ini") == "[General]\nVerbosity=Brief\n");
    CHECK(cfg::ToggleVerbosity());
    CHECK(cfg::Detailed());
    CHECK(dir.Read("QuarryAccess.ini") == "[General]\nVerbosity=Full\n");
}
