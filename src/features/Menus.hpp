#pragma once
// Menu accessibility: announces screens when they open (title, focused item,
// prompt bar), focus changes between controls, popups, and the title screen prompt.

#include "features/Feature.hpp"

namespace qa::features
{
    class MenusFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Menus";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };
}
