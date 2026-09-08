#pragma once
// A feature installs hooks/pollers for one area of the game (menus, subtitles,
// choices, ...) and contributes to the read-screen (F6) and help (F8) readouts.

#include <memory>
#include <string>
#include <vector>

namespace qa::features
{
    class Feature
    {
    public:
        virtual ~Feature() = default;
        virtual const wchar_t* Name() const = 0;
        virtual void Install() = 0;
        // Append lines describing the current state for the read-screen readout.
        virtual void Describe(std::vector<std::wstring>& /*out*/) {}
        // Append context help lines.
        virtual void Help(std::vector<std::wstring>& /*out*/) {}
    };

    void Register(std::unique_ptr<Feature> feature);
    void InstallAll();
    void DescribeAll(std::vector<std::wstring>& out);
    void HelpAll(std::vector<std::wstring>& out);
    std::vector<std::wstring> Names();
}
