#pragma once
// Button mash and button burst: the button to press, said the way the game's button
// mash setting wants it pressed; the ring that fills with the presses is played as blips
// of rising pitch; the outcome is said and played.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace qa::features
{
    class ButtonMashFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"ButtonMash";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };
}
