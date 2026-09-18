#pragma once
// Screens: the text on the monitors of the game's scenes, which the game draws as pictures
// rather than as text. The podcast studio's monitor in the epilogue shows the North Kill
// Gazette page the episode opens on and the one it closes on; Travis's computer in the police
// station shows its login, a hint when the password is wrong, an e-mail and a podcast. Each
// picture is a material the scene puts on the monitor, and screens.ini holds the text each
// one shows under the material's name. A picture is read as it comes on, once per scene, and
// F6 reads the one on the monitor now.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace qa::features
{
    class ScreensFeature : public Feature
    {
    public:
        explicit ScreensFeature(std::wstring dataFile) : m_dataFile(std::move(dataFile)) {}
        const wchar_t* Name() const override
        {
            return L"Screens";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;

    private:
        std::wstring m_dataFile;
    };
}
