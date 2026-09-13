// QuarryAccess — screen-reader accessibility mod for The Quarry (UE4SS C++ mod).
// Entry point: UE4SS loads Mods\QuarryAccess\dlls\main.dll and calls start_mod().

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "diag/Diagnostics.hpp"
#include "features/ButtonMash.hpp"
#include "features/Choices.hpp"
#include "features/DontBreathe.hpp"
#include "features/Exploration.hpp"
#include "features/Feature.hpp"
#include "features/Hud.hpp"
#include "features/Menus.hpp"
#include "features/Pause.hpp"
#include "features/Prompts.hpp"
#include "features/Qte.hpp"
#include "features/Subtitles.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "speech/TolkBridge.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks.hpp>

#include <windows.h>

#include <filesystem>
#include <memory>

#ifndef QA_VERSION_STRING
#define QA_VERSION_STRING "0.0.0"
#endif

using namespace RC;

namespace
{
    std::wstring ModuleDirectory()
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&ModuleDirectory),
                           &module);
        wchar_t buffer[MAX_PATH * 2]{};
        GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
        return std::filesystem::path(buffer).parent_path().wstring();
    }
}

class QuarryAccessMod : public CppUserModBase
{
public:
    QuarryAccessMod()
    {
        ModName = STR("QuarryAccess");
        ModVersion = qa::str::Utf8ToWide(QA_VERSION_STRING);
        ModDescription = STR("Screen-reader accessibility for The Quarry (NVDA, JAWS, SAPI via Tolk).");
        ModAuthors = STR("Futyn-Maker");
        ModIntendedSDKVersion = STR("3.0.1");
    }

    ~QuarryAccessMod() override
    {
        qa::input::ForgetWalkKeys();
        qa::tolk::Unload();
        qa::log::Shutdown();
    }

    auto on_unreal_init() -> void override
    {
        // Mods\QuarryAccess\dlls\main.dll -> Mods\QuarryAccess
        const std::wstring dllDir = ModuleDirectory();
        m_modDir = std::filesystem::path(dllDir).parent_path().wstring();

        std::wstring configError;
        auto settings = qa::cfg::LoadSettings(m_modDir + L"\\QuarryAccess.ini", &configError);
        qa::cfg::Set(settings);
        qa::log::Level level = qa::log::Level::Info;
        qa::log::ParseLevel(settings.logLevel, level);
        qa::log::Init(m_modDir + L"\\QuarryAccess.log", settings.logFile, level);
        qa::diag::SetModDir(m_modDir);

        qa::log::Info(L"QuarryAccess {} starting; mod dir {}", ModVersion, m_modDir);
        if (!configError.empty()) qa::log::Error(L"config: {} (defaults in use)", configError);

        if (!qa::tolk::Load())
        {
            qa::log::Error(L"Tolk.dll could not be loaded. Put Tolk.dll, nvdaControllerClient64.dll and SAAPI64.dll next to TheQuarry-Win64-Shipping.exe.");
            MessageBeep(MB_ICONERROR);
        }
        else
        {
            m_reader = qa::tolk::DetectScreenReader();
            qa::log::Info(L"Tolk loaded; screen reader: {}; speech={} braille={}", m_reader.empty() ? L"<none, SAPI fallback>" : m_reader,
                          qa::tolk::HasSpeech(), qa::tolk::HasBraille());
        }
        qa::speech::Init();
        qa::sounds::Init(settings.soundVolume);

        // English strings first; the game's language is applied once the frontend reports it.
        const std::wstring forcedLanguage = qa::str::EqualsNoCase(settings.language, L"auto") ? L"" : settings.language;
        qa::locale::LoadTables(m_modDir + L"\\lang", forcedLanguage.empty() ? L"en_US" : forcedLanguage);

        qa::gamethread::Install();
        // What the player presses is watched so that only their own actions cut an
        // utterance short.
        qa::input::InstallActivityTracker();
        qa::hooks::Install();
        qa::watch::Install();
        qa::hotkeys::Install();
        qa::diag::InstallCommandFile();
        qa::ui::Install();
        qa::features::Register(std::make_unique<qa::features::PauseFeature>());
        qa::features::Register(std::make_unique<qa::features::MenusFeature>());
        qa::features::Register(std::make_unique<qa::features::HudFeature>());
        qa::features::Register(std::make_unique<qa::features::SubtitlesFeature>());
        qa::features::Register(std::make_unique<qa::features::PromptsFeature>());
        qa::features::Register(std::make_unique<qa::features::ChoicesFeature>());
        qa::features::Register(std::make_unique<qa::features::QteFeature>());
        qa::features::Register(std::make_unique<qa::features::ButtonMashFeature>());
        qa::features::Register(std::make_unique<qa::features::DontBreatheFeature>());
        qa::features::Register(std::make_unique<qa::features::ExplorationFeature>());
        qa::features::InstallAll();

        Unreal::Hook::FCallbackOptions options{};
        options.bReadonly = true;
        options.OwnerModName = STR("QuarryAccess");
        options.HookName = STR("LoadMap");
        Unreal::Hook::RegisterLoadMapPostCallback(
            [](Unreal::Hook::TCallbackIterationData<bool>&, Unreal::UEngine*, Unreal::FWorldContext&, Unreal::FURL url, Unreal::UPendingNetGame*,
               Unreal::FString&)
            {
                (void)url;
                qa::gamethread::Post(
                    []()
                    {
                        qa::log::Info(L"map loaded; resetting caches");
                        qa::obj::ResetCaches();
                        qa::watch::Reset();
                        qa::features::ResetMenus();
                        qa::features::ResetSubtitles();
                        qa::input::InvalidateCache();
                    });
            },
            options);

        // Startup: wait for the game's locale, then load the matching table and greet.
        qa::gamethread::AddPoller(L"startup", [this, forcedLanguage](float) { Startup(forcedLanguage); });
        qa::log::Info(L"initialization done; waiting for the game locale");
    }

private:
    void Startup(const std::wstring& forcedLanguage)
    {
        if (m_started) return;
        const double now = qa::gamethread::NowSeconds();
        if (now - m_lastLocaleProbe < 0.5) return;
        m_lastLocaleProbe = now;

        std::wstring gameLocale = qa::gametext::CurrentLocale();
        if (gameLocale.empty() && now < 8.0) return;
        m_started = true;

        const std::wstring language = !forcedLanguage.empty() ? forcedLanguage : (gameLocale.empty() ? L"en_US" : gameLocale);
        qa::locale::LoadTables(m_modDir + L"\\lang", language);
        qa::log::Info(L"game locale: {}; mod language: {}", gameLocale.empty() ? L"<unknown>" : gameLocale, qa::locale::CurrentCode());

        // Self-check: game string resolution, control scheme, key naming.
        const auto sample = qa::gametext::Resolve(L"SMG_HUD_MENU_BUTTON_NEWGAME_000001");
        qa::log::Info(L"self-check: SMG_HUD_MENU_BUTTON_NEWGAME_000001 = \"{}\"", sample);
        qa::log::Info(L"self-check: control scheme {}", qa::input::SchemeName(qa::input::CurrentScheme()));
        qa::input::ShareGamepadReading();
        qa::log::Info(L"self-check: UINavigationCancel = \"{}\"", qa::input::KeyForAction(L"UINavigationCancel"));
        qa::log::Info(L"self-check: script handlers {}", qa::hooks::ScriptHandlerCount());

        if (qa::cfg::Get().speakOnLoad)
        {
            const std::wstring reader = m_reader.empty() ? qa::locale::Mod(L"greeting.noreader") : m_reader;
            qa::speech::Announce(qa::locale::Mod(L"greeting", std::vector<std::wstring>{ModVersion, reader, qa::locale::CurrentCode()}));
        }
        qa::log::Info(L"ready");
    }

    std::wstring m_modDir;
    std::wstring m_reader;
    bool m_started = false;
    double m_lastLocaleProbe = -1.0;
};

#define QUARRYACCESS_API __declspec(dllexport)
extern "C"
{
    QUARRYACCESS_API CppUserModBase* start_mod()
    {
        return new QuarryAccessMod();
    }

    QUARRYACCESS_API void uninstall_mod(CppUserModBase* mod)
    {
        delete mod;
    }
}
