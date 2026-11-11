// features/DropdownList: the option the mod's list for a dropdown opens on, and where each of
// its keys takes it.
#include "Support.hpp"
#include "features/DropdownList.hpp"

using namespace qa::features::dropdown;

TEST_CASE("The list opens on the option the dropdown shows, or on the first when it shows none")
{
    CHECK(Opened(2, 5) == 2);
    CHECK(Opened(0, 5) == 0);
    CHECK(Opened(-1, 5) == 0);
    CHECK(Opened(9, 5) == 4);
}

TEST_CASE("A step moves one option, and the first and the last hold")
{
    CHECK(Moved(2, Step::Next, 5) == 3);
    CHECK(Moved(2, Step::Previous, 5) == 1);
    CHECK(Moved(0, Step::Previous, 5) == 0);
    CHECK(Moved(4, Step::Next, 5) == 4);
    CHECK(Moved(0, Step::Next, 1) == 0);
}

TEST_CASE("First and Last go to the two ends")
{
    CHECK(Moved(2, Step::First, 5) == 0);
    CHECK(Moved(2, Step::Last, 5) == 4);
    CHECK(Moved(0, Step::Last, 1) == 0);
}

TEST_CASE("A dropdown with no options has no list")
{
    CHECK(Opened(0, 0) == -1);
    CHECK(Moved(0, Step::Next, 0) == -1);
    CHECK(Moved(0, Step::Last, 0) == -1);
}
