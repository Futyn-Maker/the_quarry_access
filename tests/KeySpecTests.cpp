// hotkeys/KeySpec: the spelling of a keyboard key in the ini.
#include "Support.hpp"
#include "hotkeys/KeySpec.hpp"

using namespace qa::hotkeys;

TEST_CASE("ParseKeySpec reads function keys, letters, digits, the numeric pad and named keys")
{
    KeySpec k;
    REQUIRE(ParseKeySpec(L"F5", k));
    CHECK(k.vk == 0x74);
    CHECK_FALSE(k.ctrl);
    CHECK_FALSE(k.alt);
    CHECK_FALSE(k.shift);
    REQUIRE(ParseKeySpec(L"f12", k));
    CHECK(k.vk == 0x7B);
    REQUIRE(ParseKeySpec(L"N", k));
    CHECK(k.vk == 0x4E);
    REQUIRE(ParseKeySpec(L"7", k));
    CHECK(k.vk == 0x37);
    REQUIRE(ParseKeySpec(L"numpad5", k));
    CHECK(k.vk == 0x65);
    REQUIRE(ParseKeySpec(L"Space", k));
    CHECK(k.vk == 0x20);
    REQUIRE(ParseKeySpec(L" backspace ", k));
    CHECK(k.vk == 0x08);
}

TEST_CASE("ParseKeySpec takes the modifiers in any order and any case")
{
    KeySpec k;
    REQUIRE(ParseKeySpec(L"Ctrl+F9", k));
    CHECK(k.vk == 0x78);
    CHECK(k.ctrl);
    CHECK_FALSE(k.alt);
    CHECK_FALSE(k.shift);
    REQUIRE(ParseKeySpec(L"shift+ALT+control+x", k));
    CHECK(k.vk == 0x58);
    CHECK(k.ctrl);
    CHECK(k.alt);
    CHECK(k.shift);
}

TEST_CASE("ParseKeySpec refuses nothing, none, modifiers alone and keys it does not know")
{
    KeySpec k;
    CHECK_FALSE(ParseKeySpec(L"", k));
    CHECK_FALSE(ParseKeySpec(L"none", k));
    CHECK_FALSE(ParseKeySpec(L"Ctrl+", k));
    CHECK_FALSE(ParseKeySpec(L"F25", k));
    CHECK_FALSE(ParseKeySpec(L"mouse1", k));
    CHECK(k.vk == 0);
    // Of two keys named, the last wins.
    REQUIRE(ParseKeySpec(L"F1+F2", k));
    CHECK(k.vk == 0x71);
}
