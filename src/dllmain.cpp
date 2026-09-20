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
#include "features/Combat.hpp"
#include "features/Credits.hpp"
#include "features/DontBreathe.hpp"
#include "features/Exploration.hpp"
#include "features/Feature.hpp"
#include "features/Hud.hpp"
#include "features/Menus.hpp"
#include "features/Pause.hpp"
#include "features/Prompts.hpp"
#include "features/Qte.hpp"
#include "features/Screens.hpp"
#include "features/Subtitles.hpp"
#include "features/Tarot.hpp"
#include "hooks/HookDispatcher.hpp"
#include "hotkeys/Hotkeys.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Outputs.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks.hpp>

#include <windows.h>

#include <filesystem>
#include <memory>

// QA_VERSION_STRING comes from the VERSION file at the root of the repository, through CMake.

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
        ModDescription = STR("Screen-reader accessibility for The Quarry (NVDA, JAWS, Narrator, SAPI).");
        ModAuthors = STR("Futyn-Maker");
        ModIntendedSDKVersion = STR("3.0.1");
    }

    ~QuarryAccessMod() override
    {
        qa::hotkeys::Uninstall();
        qa::input::ForgetWalkKeys();
        qa::outputs::Stop();
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

        qa::speech::SetPreferSapi(settings.preferSapi);
        // The speech library sits beside main.dll and is loaded from there, so that the mod's
        // copy of it is the mod's own. It finds the screen reader and creates the SAPI voice,
        // and says in the log which of them answered.
        if (!qa::outputs::Start(dllDir + L"\\prism.dll"))
        {
            qa::log::Error(L"Nothing can be spoken: neither a screen reader nor SAPI answered. prism.dll belongs beside main.dll in Mods\\QuarryAccess\\dlls.");
            MessageBeep(MB_ICONERROR);
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
        qa::features::Register(std::make_unique<qa::features::CreditsFeature>());
        qa::features::Register(std::make_unique<qa::features::CombatFeature>());
        qa::features::Register(std::make_unique<qa::features::TarotFeature>());
        qa::features::Register(std::make_unique<qa::features::ScreensFeature>(m_modDir + L"\\screens.ini"));
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

        // Startup: wait for the game's locale, load the matching table and greet, then
        // follow the locale for the rest of the session.
        qa::gamethread::AddPoller(L"startup", [this, forcedLanguage](float) { Startup(forcedLanguage); });
        qa::log::Info(L"initialization done; waiting for the game locale");
    }

private:
    void Startup(const std::wstring& forcedLanguage)
    {
        const double now = qa::gamethread::NowSeconds();
        if (now - m_lastLocaleProbe < 0.5) return;
        m_lastLocaleProbe = now;

        std::wstring gameLocale = qa::gametext::CurrentLocale();
        if (m_started)
        {
            FollowLocale(forcedLanguage, gameLocale);
            return;
        }
        // The game starts on the system's locale and applies its own language (Steam's)
        // a few seconds later, so an answer is taken only once it has held for two seconds.
        if (gameLocale != m_candidateLocale)
        {
            m_candidateLocale = gameLocale;
            m_candidateSince = now;
        }
        if (gameLocale.empty() ? now < 8.0 : now - m_candidateSince < 2.0) return;
        m_started = true;
        m_gameLocale = gameLocale;

        const std::wstring language = !forcedLanguage.empty() ? forcedLanguage : (gameLocale.empty() ? L"en_US" : gameLocale);
        qa::locale::LoadTables(m_modDir + L"\\lang", language);
        qa::log::Info(L"game locale: {}; mod language: {}", gameLocale.empty() ? L"<unknown>" : gameLocale, qa::locale::CurrentCode());
        // The SAPI voice follows the language of what is said.
        qa::outputs::SelectVoiceFor(qa::locale::CurrentCode());

        // Self-check: game string resolution, control scheme, key naming.
        const auto sample = qa::gametext::Resolve(L"SMG_HUD_MENU_BUTTON_NEWGAME_000001");
        qa::log::Info(L"self-check: SMG_HUD_MENU_BUTTON_NEWGAME_000001 = \"{}\"", sample);
        qa::log::Info(L"self-check: control scheme {}", qa::input::SchemeName(qa::input::CurrentScheme()));
        qa::input::ShareGamepadReading();
        qa::log::Info(L"self-check: UINavigationCancel = \"{}\"", qa::input::KeyForAction(L"UINavigationCancel"));
        qa::log::Info(L"self-check: script handlers {}", qa::hooks::ScriptHandlerCount());

        if (qa::cfg::Get().speakOnLoad)
        {
            const std::wstring output = qa::speech::OutputName();
            const std::wstring reader = output.empty() ? qa::locale::Mod(L"greeting.noreader") : output;
            // What the mod runs with belongs in the log either way; briefly the player is
            // told only that it is there.
            qa::speech::Announce(qa::cfg::Detailed() ? qa::locale::Mod(L"greeting", std::vector<std::wstring>{ModVersion, reader, qa::locale::CurrentCode()})
                                                     : qa::locale::Mod(L"greeting.brief"));
        }
        qa::log::Info(L"ready");
    }

    // The language can still change after the start, so the mod follows it for the whole
    // session: the game's strings are resolved anew, and the mod's tables and voice change.
    void FollowLocale(const std::wstring& forcedLanguage, const std::wstring& gameLocale)
    {
        if (gameLocale.empty() || gameLocale == m_gameLocale) return;
        qa::log::Info(L"game locale changed: {} -> {}", m_gameLocale.empty() ? L"<unknown>" : m_gameLocale, gameLocale);
        m_gameLocale = gameLocale;
        qa::gametext::ClearCache();
        qa::input::InvalidateCache();
        if (!forcedLanguage.empty()) return;
        qa::locale::LoadTables(m_modDir + L"\\lang", gameLocale);
        qa::outputs::SelectVoiceFor(qa::locale::CurrentCode());
        qa::log::Info(L"mod language: {}", qa::locale::CurrentCode());
    }

    std::wstring m_modDir;
    std::wstring m_gameLocale;
    std::wstring m_candidateLocale;
    double m_candidateSince = 0.0;
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
