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

    // True from the moment the player answers the pause menu's "return to the main menu"
    // popup until the game has arrived somewhere. Answering it takes the game apart around
    // the player, handing the character and its prompts back for a few seconds on the way
    // out, and none of that world is worth saying.
    bool LeavingTheGame();
}
