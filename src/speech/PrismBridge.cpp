#include "speech/PrismBridge.hpp"

#include "core/Log.hpp"
#include "core/Strings.hpp"

#include <windows.h>

#include <atomic>
#include <format>
#include <string_view>
#include <type_traits>

namespace qa::prism
{
    namespace
    {
        HMODULE g_module = nullptr;
        bool g_loaded = false;
        Api g_api{};
        PrismContext* g_context = nullptr;
        std::atomic<bool> g_changed{false};

        // Prism's poll thread calls this whenever a backend's engine appears or goes away. It
        // must not block, so it only leaves a mark: the speech thread looks for the screen
        // reader again before the next line it speaks.
        void PRISM_CALL OnAvailability(void* /*userdata*/, PrismBackendId /*backend*/, const char* /*name*/, bool /*available*/)
        {
            g_changed.store(true, std::memory_order_release);
        }

        // Prism's own diagnostics, on Prism's logging thread, in the mod's log.
        void PRISM_CALL OnLog(void* /*userdata*/, PrismLogLevel level, const char* source, const char* message)
        {
            // Prism names some of its sources after itself already ("prism/delayimp").
            std::wstring from = str::Utf8ToWide(source ? source : "?");
            if (!str::StartsWith(from, L"prism")) from = L"prism/" + from;
            const std::wstring text = std::format(L"{}: {}", from, str::Utf8ToWide(message ? message : ""));
            switch (level)
            {
            case PRISM_LOG_LEVEL_ERROR: log::Write(log::Level::Error, text); return;
            case PRISM_LOG_LEVEL_WARN: log::Write(log::Level::Info, text); return;
            case PRISM_LOG_LEVEL_INFO: log::Write(log::Level::Verbose, text); return;
            default: log::Write(log::Level::Trace, text); return;
            }
        }

        PrismLogLevel LevelFor(log::Level level)
        {
            if (level >= log::Level::Trace) return PRISM_LOG_LEVEL_TRACE;
            if (level >= log::Level::Verbose) return PRISM_LOG_LEVEL_DEBUG;
            return PRISM_LOG_LEVEL_WARN;
        }

        bool Bind()
        {
            const char* missing = nullptr;
            const auto bind = [&missing](auto& field, const char* name)
            {
                field = reinterpret_cast<std::remove_reference_t<decltype(field)>>(GetProcAddress(g_module, name));
                if (!field) missing = name;
            };
            bind(g_api.init, "prism_init");
            bind(g_api.shutdown, "prism_shutdown");
            bind(g_api.registry_count, "prism_registry_count");
            bind(g_api.registry_id_at, "prism_registry_id_at");
            bind(g_api.registry_exists, "prism_registry_exists");
            bind(g_api.registry_create, "prism_registry_create");
            bind(g_api.backend_free, "prism_backend_free");
            bind(g_api.backend_name, "prism_backend_name");
            bind(g_api.backend_get_features, "prism_backend_get_features");
            bind(g_api.backend_initialize, "prism_backend_initialize");
            bind(g_api.backend_speak, "prism_backend_speak");
            bind(g_api.backend_braille, "prism_backend_braille");
            bind(g_api.backend_output, "prism_backend_output");
            bind(g_api.backend_stop, "prism_backend_stop");
            bind(g_api.backend_count_voices, "prism_backend_count_voices");
            bind(g_api.backend_get_voice, "prism_backend_get_voice");
            bind(g_api.backend_set_voice, "prism_backend_set_voice");
            bind(g_api.backend_get_voice_name, "prism_backend_get_voice_name");
            bind(g_api.backend_get_voice_language, "prism_backend_get_voice_language");
            bind(g_api.error_string, "prism_error_string");
            bind(g_api.set_log_handler, "prism_set_log_handler");
            bind(g_api.set_log_level, "prism_set_log_level");
            bind(g_api.log_shutdown, "prism_log_shutdown");
            bind(g_api.version_string, "prism_version_string");
            if (missing) log::Error(L"prism: the library has no {}; it is not the one the mod was built against", str::Utf8ToWide(missing));
            return missing == nullptr;
        }
    }

    bool Load(const std::wstring& dllPath)
    {
        if (g_loaded) return true;
        g_module = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!g_module)
        {
            log::Error(L"prism: {} could not be loaded (error {})", dllPath, GetLastError());
            return false;
        }
        if (!Bind())
        {
            FreeLibrary(g_module);
            g_module = nullptr;
            g_api = Api{};
            return false;
        }

        g_api.set_log_handler(PrismLogHandler{OnLog, nullptr});
        g_api.set_log_level(LevelFor(log::GetLevel()));

        const char* version = g_api.version_string();
        if (!version || std::string_view(version) != PRISM_VERSION_STRING)
            log::Info(L"prism: the library is {} and the mod was built against {}", str::Utf8ToWide(version ? version : "unknown"),
                      str::Utf8ToWide(PRISM_VERSION_STRING));

        // The configuration is the mod's own rather than prism_config_init's, and says which
        // version of the structure it is: a library newer than the header the mod was built
        // against then reads only the fields that version has, and writes none of its own past
        // the end of this one.
        //
        // The watch is a thread of Prism's that samples every backend and tells the mod when
        // one appears or goes, which is how a screen reader started or closed during play is
        // noticed without probing for it on every line spoken.
        PrismConfig config{};
        config.version = PRISM_CONFIG_VERSION;
        config.availability_callback = OnAvailability;
        config.availability_backoff_max_ms = 10000;
        config.availability_auto_power_manage = true;
        g_context = g_api.init(&config);
        if (!g_context)
        {
            log::Error(L"prism: the library refused to start");
            g_api.set_log_handler(PrismLogHandler{nullptr, nullptr});
            g_api.log_shutdown();
            FreeLibrary(g_module);
            g_module = nullptr;
            g_api = Api{};
            return false;
        }
        g_loaded = true;
        return true;
    }

    void Unload()
    {
        if (!g_loaded) return;
        // The context owns the poll thread and the logging thread outlives it, so both are
        // ended before the code they run in is unmapped.
        g_api.shutdown(g_context);
        g_context = nullptr;
        g_api.set_log_handler(PrismLogHandler{nullptr, nullptr});
        g_api.log_shutdown();
        if (g_module) FreeLibrary(g_module);
        g_module = nullptr;
        g_api = Api{};
        g_loaded = false;
    }

    bool IsLoaded()
    {
        return g_loaded;
    }

    const Api& Get()
    {
        return g_api;
    }

    PrismContext* Context()
    {
        return g_context;
    }

    std::wstring Version()
    {
        if (!g_loaded) return {};
        const char* text = g_api.version_string();
        return text ? str::Utf8ToWide(text) : std::wstring();
    }

    PrismBackend* Create(PrismBackendId id)
    {
        if (!g_loaded || !g_context || !g_api.registry_exists(g_context, id)) return nullptr;
        PrismBackend* backend = g_api.registry_create(g_context, id);
        if (!backend) return nullptr;
        if (!Running(backend))
        {
            g_api.backend_free(backend);
            return nullptr;
        }
        const PrismError error = g_api.backend_initialize(backend);
        if (error != PRISM_OK && error != PRISM_ERROR_ALREADY_INITIALIZED)
        {
            log::Info(L"prism: {} did not start: {}", Name(backend), ErrorText(error));
            g_api.backend_free(backend);
            return nullptr;
        }
        return backend;
    }

    void Free(PrismBackend*& backend)
    {
        if (!backend) return;
        if (g_loaded) g_api.backend_free(backend);
        backend = nullptr;
    }

    std::uint64_t Features(PrismBackend* backend)
    {
        return (g_loaded && backend) ? g_api.backend_get_features(backend) : 0;
    }

    bool Running(PrismBackend* backend)
    {
        return (Features(backend) & PRISM_BACKEND_IS_SUPPORTED_AT_RUNTIME) != 0;
    }

    std::wstring Name(PrismBackend* backend)
    {
        if (!g_loaded || !backend) return {};
        const char* text = g_api.backend_name(backend);
        return text ? str::Utf8ToWide(text) : std::wstring();
    }

    bool Lost(PrismError error)
    {
        switch (error)
        {
        case PRISM_ERROR_NOT_INITIALIZED:
        case PRISM_ERROR_SPEAK_FAILURE:
        case PRISM_ERROR_INTERNAL:
        case PRISM_ERROR_BACKEND_NOT_AVAILABLE:
        case PRISM_ERROR_UNKNOWN:
        case PRISM_ERROR_BACKEND_ENTERED_UNDEFINED_STATE: return true;
        default: return false;
        }
    }

    std::wstring ErrorText(PrismError error)
    {
        if (!g_loaded) return std::to_wstring(static_cast<int>(error));
        const char* text = g_api.error_string(error);
        return text ? str::Utf8ToWide(text) : std::to_wstring(static_cast<int>(error));
    }

    bool TakeChanged()
    {
        return g_changed.exchange(false, std::memory_order_acq_rel);
    }
}
