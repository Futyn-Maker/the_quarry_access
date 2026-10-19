#include "features/ExploreText.hpp"

#include "core/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace qa::explore
{
    std::wstring NameWords(std::wstring_view objectName)
    {
        std::wstring name(objectName);
        for (const wchar_t* prefix : {L"UL_", L"UI_", L"EDI_", L"EDN_", L"ED_", L"EDG_", L"BP_"})
        {
            if (str::StartsWith(name, prefix)) name.erase(0, wcslen(prefix));
        }
        while (!name.empty() && (iswdigit(name.back()) || name.back() == L'_'))
            name.pop_back();
        std::wstring out;
        for (size_t i = 0; i < name.size(); ++i)
        {
            const wchar_t c = name[i];
            if (c == L'_')
            {
                out += L' ';
                continue;
            }
            if (i > 0 && iswupper(c))
            {
                const wchar_t previous = name[i - 1];
                const bool opensWord = !iswupper(previous);
                const bool closesRun = iswupper(previous) && i + 1 < name.size() && iswlower(name[i + 1]);
                if (opensWord || closesRun) out += L' ';
            }
            out += c;
        }
        return str::CollapseWhitespace(out);
    }

    double NormalizeDegrees(double degrees)
    {
        degrees = std::fmod(degrees + 180.0, 360.0);
        if (degrees < 0) degrees += 360.0;
        return degrees - 180.0;
    }

    const wchar_t* SectorKey(double bearing)
    {
        const int sector = (static_cast<int>(std::lround(bearing / 45.0)) % 8 + 8) % 8;
        static const wchar_t* const keys[] = {L"dir8.ahead",  L"dir8.aheadright", L"dir8.right", L"dir8.behindright",
                                              L"dir8.behind", L"dir8.behindleft", L"dir8.left",  L"dir8.aheadleft"};
        return keys[sector];
    }

    std::wstring Metres(double centimetres)
    {
        return std::to_wstring(std::max<long>(1, std::lround(centimetres / 100.0)));
    }
}
