#pragma once
// Subtitles: every line the game puts on screen is read once, as it appears, exactly
// as displayed under the game's own subtitle settings. Two hotkeys switch the reading
// off and on during play and repeat the last line.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace qa::features
{
    class SubtitlesFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Subtitles";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // Switches the reading of subtitle lines off and on, and says which.
    void ToggleSubtitles();

    // Sets the reading of subtitle lines and keeps the choice in the ini for the next start.
    void SetSubtitles(bool read);

    // Repeats the last subtitle line that appeared, even after it has gone and while the
    // reading is switched off.
    void SpeakLastSubtitle();

    // Forgets the last line (after a map change: the game was left or started).
    void ResetSubtitles();
}
