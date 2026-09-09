#pragma once
// Text the game puts on the HUD by itself: scene captions, notifications with their
// hot-link prompt, alerts, act titles, and the loading and saving indicators.

#include "features/Feature.hpp"

namespace qa::features
{
    class HudFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Hud";
        }
        void Install() override;
    };
}
