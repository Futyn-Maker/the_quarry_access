#include "features/Screens.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "speech/Speech.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace qa::features
{
    using RC::Unreal::UObject;

    namespace
    {
        // The text each picture shows, by the lower-case name of the material that shows it.
        std::map<std::wstring, std::wstring> g_texts;

        // A monitor being watched: the mesh component a scene paints its pictures on, and the
        // picture with text it showed at the last look (empty when none).
        struct Screen
        {
            UObject* component = nullptr;
            std::wstring showing;
        };
        std::vector<Screen> g_screens;
        // The pictures already read while the scene is loaded: a scene brings a picture back
        // between its shots, and it is read the first time only.
        std::set<std::wstring> g_read;
        double g_nextSearch = 0.0;
        int g_fruitless = 0; // searches in a row that found no monitor while a scene was loaded

        // The scenes with such monitors. A scene is known to be loaded by a material only it
        // loads; its monitors are the mesh components made of the meshes named. The podcast
        // studio's monitor is a case and a screen, both painted by the podcast's cinematics;
        // Travis's monitor is a prop of its own.
        struct Scene
        {
            const wchar_t* marker;
            std::vector<std::wstring> meshes;
        };
        const Scene kScenes[] = {
            {L"/Game/Environments/FrontEnd/StaticMeshes/SetDressing/Monitor_CRT_PC/Epilogue/M_Monitor_Epilogue.M_Monitor_Epilogue",
             {L"SM_Monitor_CRT_Screen", L"SM_Monitor_CRT_PC"}},
            {L"/Game/Props/Accessories/Travis_Monitor/Models/MI_Travis_Monitor_Email_StandardSM.MI_Travis_Monitor_Email_StandardSM",
             {L"SM_Travis_Monitor", L"AccessoriesTravisMonitor"}},
        };

        void LoadTexts(const std::wstring& path)
        {
            cfg::Ini ini;
            std::wstring error;
            if (!ini.Load(path, &error))
            {
                log::Error(L"screens: {}", error);
                return;
            }
            if (const auto* section = ini.Section(L"screens"))
            {
                for (const auto& [key, value] : *section)
                    g_texts[str::ToLower(key)] = str::UnescapeIni(value);
            }
            log::Info(L"screens: {} picture(s) with text", g_texts.size());
        }

        // The mesh a component draws: a static mesh or a skeletal one.
        std::wstring MeshName(UObject* component)
        {
            UObject* mesh = nullptr;
            if (obj::FindProperty(component, L"StaticMesh"))
                obj::ReadObject(component, L"StaticMesh", mesh);
            else if (obj::FindProperty(component, L"SkeletalMesh"))
                obj::ReadObject(component, L"SkeletalMesh", mesh);
            return obj::IsLive(mesh) ? obj::ObjectName(mesh) : std::wstring();
        }

        // How many material slots a component has, as the engine counts them.
        int NumMaterials(UObject* component)
        {
            auto* fn = obj::FindFunction(component, L"GetNumMaterials");
            if (!fn) return 0;
            int64_t count = 0;
            obj::Call(component, fn, nullptr,
                      [&](void* params)
                      {
                          for (auto* prop : fn->ForEachProperty())
                          {
                              if (prop && prop->GetName() == L"ReturnValue") obj::ReadIntAt(params, prop, count);
                          }
                      });
            return static_cast<int>(std::clamp<int64_t>(count, 0, 8));
        }

        // The material a component draws in one slot: the one a cinematic or a prop has set
        // on it, when one has.
        UObject* MaterialAt(UObject* component, int index)
        {
            auto* fn = obj::FindFunction(component, L"GetMaterial");
            if (!fn) return nullptr;
            UObject* material = nullptr;
            obj::Call(
                component, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ElementIndex") *static_cast<int32_t*>(obj::ValuePtrAt(params, prop)) = index;
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadObjectAt(params, prop, material);
                    }
                });
            return material;
        }

        // The picture with text a monitor shows now, as the lower-case name of its material.
        std::wstring Showing(UObject* component)
        {
            const int count = NumMaterials(component);
            for (int i = 0; i < count; ++i)
            {
                UObject* material = MaterialAt(component, i);
                if (!obj::IsLive(material)) continue;
                const auto name = str::ToLower(obj::ObjectName(material));
                if (g_texts.contains(name)) return name;
            }
            return {};
        }

        // Finds the monitors of whichever scene is loaded. The search walks every mesh component,
        // so it runs only while a scene with monitors is loaded and none has been found yet, and
        // gives up after a few tries until the scene's marker has left memory, which it can
        // outlive.
        void Search(double now)
        {
            if (now < g_nextSearch) return;
            g_nextSearch = now + 3.0;
            std::vector<std::wstring> meshes;
            for (const auto& scene : kScenes)
            {
                if (obj::FindObject(scene.marker)) meshes.insert(meshes.end(), scene.meshes.begin(), scene.meshes.end());
            }
            if (meshes.empty())
            {
                g_fruitless = 0;
                return;
            }
            if (g_fruitless >= 10) return;
            for (UObject* component : obj::FindAllLive(L"MeshComponent"))
            {
                const auto mesh = MeshName(component);
                if (mesh.empty() || std::find(meshes.begin(), meshes.end(), mesh) == meshes.end()) continue;
                g_screens.push_back({component, {}});
                log::Info(L"screens: watching {} ({})", obj::FullName(component), mesh);
            }
            g_fruitless = g_screens.empty() ? g_fruitless + 1 : 0;
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 12 != 5) return;
            std::erase_if(g_screens, [](const Screen& screen) { return !obj::IsLive(screen.component); });
            if (g_screens.empty())
            {
                g_read.clear();
                Search(gamethread::NowSeconds());
                return;
            }
            for (auto& screen : g_screens)
            {
                const auto showing = Showing(screen.component);
                if (showing == screen.showing) continue;
                screen.showing = showing;
                if (showing.empty()) continue;
                log::Info(L"screens: {} shows {}", obj::ObjectName(screen.component), showing);
                if (g_read.insert(showing).second) speech::Announce(g_texts[showing]);
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"screens.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void ScreensFeature::Install()
    {
        LoadTexts(m_dataFile);
        gamethread::AddPoller(L"screens", &Poll);
    }

    void ScreensFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& screen : g_screens)
        {
            if (!obj::IsLive(screen.component) || screen.showing.empty()) continue;
            if (const auto it = g_texts.find(screen.showing); it != g_texts.end()) out.push_back(it->second);
        }
    }
}
