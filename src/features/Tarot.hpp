#pragma once
// The fortune teller's visions: each is a video played in the crystal ball (and again from
// the tarot tab of the pause menu), and its file names the card and, for the cards whose
// vision depends on what has happened, the variant. A description of what the video shows
// is read as it begins to play; the read-screen readout repeats it while it plays.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace qa::features
{
    class TarotFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Tarot";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
    };
}
