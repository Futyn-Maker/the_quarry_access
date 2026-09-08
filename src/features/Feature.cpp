#include "features/Feature.hpp"

#include "core/Config.hpp"
#include "core/Log.hpp"

namespace qa::features
{
    namespace
    {
        std::vector<std::unique_ptr<Feature>>& Registry()
        {
            static std::vector<std::unique_ptr<Feature>> registry;
            return registry;
        }
    }

    void Register(std::unique_ptr<Feature> feature)
    {
        Registry().push_back(std::move(feature));
    }

    void InstallAll()
    {
        for (auto& feature : Registry())
        {
            if (!cfg::Get().FeatureEnabled(feature->Name()))
            {
                log::Info(L"feature {} disabled in QuarryAccess.ini", feature->Name());
                continue;
            }
            try
            {
                feature->Install();
                log::Info(L"feature {} installed", feature->Name());
            }
            catch (...)
            {
                log::Error(L"feature {} failed to install", feature->Name());
            }
        }
    }

    void DescribeAll(std::vector<std::wstring>& out)
    {
        for (auto& feature : Registry())
        {
            if (!cfg::Get().FeatureEnabled(feature->Name())) continue;
            feature->Describe(out);
        }
    }

    void HelpAll(std::vector<std::wstring>& out)
    {
        for (auto& feature : Registry())
        {
            if (!cfg::Get().FeatureEnabled(feature->Name())) continue;
            feature->Help(out);
        }
    }

    std::vector<std::wstring> Names()
    {
        std::vector<std::wstring> names;
        for (auto& feature : Registry())
            names.emplace_back(feature->Name());
        return names;
    }
}
