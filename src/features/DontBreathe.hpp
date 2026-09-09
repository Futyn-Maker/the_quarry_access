#pragma once
// Don't Breathe: the prompt to hold the breath and the prompt to release it, read as the
// game shows them; the breath bars are played as blips while the breath is held; the
// outcome is said and played.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace qa::features
{
    class DontBreatheFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"DontBreathe";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };
}
