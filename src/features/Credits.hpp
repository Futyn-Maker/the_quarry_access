#pragma once
// The credits: the names shown one by one early in the game, and the rolls at its end, whose
// section titles are said as they come up; the read-screen readout reads the lines on screen.

#include "features/Feature.hpp"

namespace qa::features
{
    class CreditsFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Credits";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
    };
}
