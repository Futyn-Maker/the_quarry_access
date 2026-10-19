// features/ExploreText: the exploration's wording that needs no game.
#include "Support.hpp"
#include "features/ExploreText.hpp"

using namespace qa;
using tests::U8;

TEST_CASE("NameWords drops the prefix of the kind and the copy number and parts the words at the capitals")
{
    CHECK(U8(explore::NameWords(L"UL_CampMap_2")) == "Camp Map");
    CHECK(U8(explore::NameWords(L"BP_letter")) == "letter");
    CHECK(U8(explore::NameWords(L"EDI_FirePitSurrounding_12")) == "Fire Pit Surrounding");
    CHECK(U8(explore::NameWords(L"UL_TVRemote")) == "TV Remote");
    CHECK(U8(explore::NameWords(L"trigger_backtopath")) == "trigger backtopath");
    CHECK(U8(explore::NameWords(L"Ul_Stream")) == "Ul Stream");
    CHECK(explore::NameWords(L"UL_").empty());
    CHECK(explore::NameWords(L"").empty());
}

TEST_CASE("NormalizeDegrees brings any angle into the half-open range from -180 to 180")
{
    CHECK(explore::NormalizeDegrees(190.0) == doctest::Approx(-170.0));
    CHECK(explore::NormalizeDegrees(-190.0) == doctest::Approx(170.0));
    CHECK(explore::NormalizeDegrees(360.0) == doctest::Approx(0.0));
    CHECK(explore::NormalizeDegrees(180.0) == doctest::Approx(-180.0));
    CHECK(explore::NormalizeDegrees(-180.0) == doctest::Approx(-180.0));
    CHECK(explore::NormalizeDegrees(45.0) == doctest::Approx(45.0));
}

TEST_CASE("SectorKey names the eight sectors, each 45 degrees wide and centred on its direction")
{
    CHECK(std::wstring_view(explore::SectorKey(0.0)) == L"dir8.ahead");
    CHECK(std::wstring_view(explore::SectorKey(20.0)) == L"dir8.ahead");
    CHECK(std::wstring_view(explore::SectorKey(-20.0)) == L"dir8.ahead");
    CHECK(std::wstring_view(explore::SectorKey(45.0)) == L"dir8.aheadright");
    CHECK(std::wstring_view(explore::SectorKey(90.0)) == L"dir8.right");
    CHECK(std::wstring_view(explore::SectorKey(135.0)) == L"dir8.behindright");
    CHECK(std::wstring_view(explore::SectorKey(180.0)) == L"dir8.behind");
    CHECK(std::wstring_view(explore::SectorKey(-180.0)) == L"dir8.behind");
    CHECK(std::wstring_view(explore::SectorKey(-135.0)) == L"dir8.behindleft");
    CHECK(std::wstring_view(explore::SectorKey(-90.0)) == L"dir8.left");
    CHECK(std::wstring_view(explore::SectorKey(-45.0)) == L"dir8.aheadleft");
    CHECK(std::wstring_view(explore::SectorKey(359.0)) == L"dir8.ahead");
}

TEST_CASE("Metres rounds centimetres to whole metres and never says less than one")
{
    CHECK(U8(explore::Metres(0.0)) == "1");
    CHECK(U8(explore::Metres(49.0)) == "1");
    CHECK(U8(explore::Metres(150.0)) == "2");
    CHECK(U8(explore::Metres(1234.0)) == "12");
}
