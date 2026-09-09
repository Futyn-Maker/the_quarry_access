#pragma once
// Choices: the two-option choice with its question, options, timeout option, keys and
// timer; the four-way choice; the countdown that warns of a coming choice; the timer
// bar. The option the player holds and the option chosen are announced as the game
// highlights them.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace qa::features
{
    class ChoicesFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Choices";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };
}
