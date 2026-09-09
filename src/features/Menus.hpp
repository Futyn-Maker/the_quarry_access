#pragma once
// Menu accessibility: announces screens when they open (title, message, focused
// item, prompt bar), focus changes between controls, changed values, popups and
// the title screen prompt.

#include "features/Feature.hpp"

#include <string>
#include <vector>

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

    // Reads the current screen again as if it had just opened, with these lines spoken
    // first. Used when something other than a screen change reshapes what is shown,
    // such as switching a pause tab.
    void ArriveWith(std::vector<std::wstring> heading);
    bool ArrivalPending();

    // Forgets every screen and control (after a map change).
    void ResetMenus();
}
