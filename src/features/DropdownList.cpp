#include "features/DropdownList.hpp"

#include <algorithm>

namespace qa::features::dropdown
{
    int64_t Opened(int64_t shown, int64_t count)
    {
        if (count <= 0) return -1;
        return std::clamp<int64_t>(shown, 0, count - 1);
    }

    int64_t Moved(int64_t index, Step step, int64_t count)
    {
        if (count <= 0) return -1;
        int64_t next = index;
        switch (step)
        {
        case Step::Previous: next = index - 1; break;
        case Step::Next: next = index + 1; break;
        case Step::First: next = 0; break;
        case Step::Last: next = count - 1; break;
        }
        return std::clamp<int64_t>(next, 0, count - 1);
    }
}
