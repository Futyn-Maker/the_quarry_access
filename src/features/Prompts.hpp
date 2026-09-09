#pragma once
// Interaction prompts: the button prompts the game puts on screen during play (an
// interaction, an interruption, a use location, a combat prompt) with their label and
// key, and the prompts that ask for the mouse or the stick.

#include "features/Feature.hpp"

#include <Unreal/UObject.hpp>

#include <string>
#include <vector>

namespace qa::features
{
    class PromptsFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Prompts";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
    };

    // The input action a button prompt widget was last set up with, empty when unknown.
    std::wstring PromptAction(RC::Unreal::UObject* promptWidget);
}
