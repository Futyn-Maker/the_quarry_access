#pragma once
// The pause menu: announces that the game is paused, the selected tab and its place
// among the tabs, and explains the tab keys in the help.

#include "features/Feature.hpp"

namespace qa::features
{
    class PauseFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Pause";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };
}
