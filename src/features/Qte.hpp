#pragma once
// Quick-time events: the direction the arrow asks for, with the key when the game shows
// a key, said and played as a cue the moment the event appears; a tone tells whether it
// was hit or missed.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace qa::features
{
    class QteFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Qte";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };
}
