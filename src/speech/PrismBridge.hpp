#pragma once
// The speech library: prism.dll (https://github.com/ethindp/prism), which speaks through the
// screen reader the player runs and through SAPI. It is loaded from the mod's own dlls folder
// by its full path, so that the mod's copy is its own: another mod's Prism, wherever it lies,
// is a separate module with separate state, and the backends made here come from
// prism_registry_create, which never shares an instance with anyone.
//
// Everything below, and every backend made through it, belongs to one thread: speech/Outputs
// runs it. A backend instance is not thread-safe, and behind the Windows backends are COM
// objects whose proxies answer only in the apartment of the thread that unmarshalled them.

// Prism's feature bits run to bit 63, which MSVC reports as a truncated constant while it
// gives the enumeration its type; the bits the mod reads are all far below that.
#pragma warning(push)
#pragma warning(disable : 4309)
#include <prism.h>
#pragma warning(pop)

#include <cstdint>
#include <string>

namespace qa::prism
{
    // The entry points the mod calls. Their types are taken from prism.h, so a library that
    // changed a signature is a build error rather than a crash.
    struct Api
    {
        decltype(&prism_init) init;
        decltype(&prism_shutdown) shutdown;
        decltype(&prism_registry_count) registry_count;
        decltype(&prism_registry_id_at) registry_id_at;
        decltype(&prism_registry_exists) registry_exists;
        decltype(&prism_registry_create) registry_create;
        decltype(&prism_backend_free) backend_free;
        decltype(&prism_backend_name) backend_name;
        decltype(&prism_backend_get_features) backend_get_features;
        decltype(&prism_backend_initialize) backend_initialize;
        decltype(&prism_backend_speak) backend_speak;
        decltype(&prism_backend_braille) backend_braille;
        decltype(&prism_backend_output) backend_output;
        decltype(&prism_backend_stop) backend_stop;
        decltype(&prism_backend_count_voices) backend_count_voices;
        decltype(&prism_backend_get_voice) backend_get_voice;
        decltype(&prism_backend_set_voice) backend_set_voice;
        decltype(&prism_backend_get_voice_name) backend_get_voice_name;
        decltype(&prism_backend_get_voice_language) backend_get_voice_language;
        decltype(&prism_error_string) error_string;
        decltype(&prism_set_log_handler) set_log_handler;
        decltype(&prism_set_log_level) set_log_level;
        decltype(&prism_log_shutdown) log_shutdown;
        decltype(&prism_version_string) version_string;
    };

    // Loads the library, resolves the entry points and opens the mod's context, which watches
    // for backends coming and going. False when the file is missing or is not this Prism.
    bool Load(const std::wstring& dllPath);
    void Unload();
    bool IsLoaded();

    const Api& Get();
    PrismContext* Context();
    std::wstring Version();

    // A backend of the mod's own, checked to have its engine running and initialized. Null when
    // that engine is not there, which is the answer to "is this screen reader running".
    PrismBackend* Create(PrismBackendId id);
    void Free(PrismBackend*& backend);
    std::uint64_t Features(PrismBackend* backend);
    bool Running(PrismBackend* backend);
    std::wstring Name(PrismBackend* backend);

    // True when a call failed because the engine behind the backend went away. Prism never
    // reconnects a backend, so the only cure is to drop it and make another.
    bool Lost(PrismError error);
    std::wstring ErrorText(PrismError error);

    // Whether any backend has appeared or gone since this was last asked. Prism's own watch
    // answers it, so the mod does not probe the screen readers for every line it speaks.
    bool TakeChanged();
}
