#pragma once
// Real-time combat: while a fight is on, a sound tells where the target stands against the
// aim, which the game shows as the torch beam on the weapon (the ear names the side, the
// pitch above or below, the pace how near, and a quick double ping that the beam is on the
// target); each shot is answered with a hit or a miss, and running out of time is said.
// Where several targets are offered the next and previous keys switch between them and the
// where key says where the current one is.

#include "features/Feature.hpp"

#include <Unreal/UObject.hpp>

#include <string>
#include <vector>

namespace qa::features
{
    class CombatFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Combat";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // Records that the game has shown the prompt to fire: the sign of a fight.
    void NoteCombatPrompt(RC::Unreal::UObject* prompt);
    // True while a fight is on.
    bool CombatActive();
    // Leads the aim sound to the next or the previous target and says which; says where the
    // current one stands when there is only one.
    void NextCombatTarget();
    void PreviousCombatTarget();
    // Says where the target stands against the aim.
    void WhereIsCombatTarget();
    // Switches the aim sound off and on, and says which.
    void ToggleAimSound();
}
