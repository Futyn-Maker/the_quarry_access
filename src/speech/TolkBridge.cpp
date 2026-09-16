#include "speech/TolkBridge.hpp"

#include <windows.h>

namespace qa::tolk
{
    namespace
    {
        using FnVoid = void(__cdecl*)();
        using FnBool = bool(__cdecl*)();
        using FnTrySapi = void(__cdecl*)(bool);
        using FnDetect = const wchar_t*(__cdecl*)();
        using FnOutput = bool(__cdecl*)(const wchar_t*, bool);

        HMODULE g_module = nullptr;
        bool g_loaded = false;
        FnVoid g_load = nullptr;
        FnVoid g_unload = nullptr;
        FnTrySapi g_trySapi = nullptr;
        FnTrySapi g_preferSapi = nullptr;
        FnBool g_isSpeaking = nullptr;
        FnBool g_hasSpeech = nullptr;
        FnBool g_hasBraille = nullptr;
        FnBool g_silence = nullptr;
        FnDetect g_detect = nullptr;
        FnOutput g_output = nullptr;
        using FnBraille = bool(__cdecl*)(const wchar_t*);
        FnBraille g_braille = nullptr;
    }

    bool Load()
    {
        if (g_loaded) return true;
        g_module = LoadLibraryW(L"Tolk.dll");
        if (!g_module) return false;

        g_load = reinterpret_cast<FnVoid>(GetProcAddress(g_module, "Tolk_Load"));
        g_unload = reinterpret_cast<FnVoid>(GetProcAddress(g_module, "Tolk_Unload"));
        g_trySapi = reinterpret_cast<FnTrySapi>(GetProcAddress(g_module, "Tolk_TrySAPI"));
        g_preferSapi = reinterpret_cast<FnTrySapi>(GetProcAddress(g_module, "Tolk_PreferSAPI"));
        g_isSpeaking = reinterpret_cast<FnBool>(GetProcAddress(g_module, "Tolk_IsSpeaking"));
        g_hasSpeech = reinterpret_cast<FnBool>(GetProcAddress(g_module, "Tolk_HasSpeech"));
        g_hasBraille = reinterpret_cast<FnBool>(GetProcAddress(g_module, "Tolk_HasBraille"));
        g_silence = reinterpret_cast<FnBool>(GetProcAddress(g_module, "Tolk_Silence"));
        g_detect = reinterpret_cast<FnDetect>(GetProcAddress(g_module, "Tolk_DetectScreenReader"));
        g_output = reinterpret_cast<FnOutput>(GetProcAddress(g_module, "Tolk_Output"));
        g_braille = reinterpret_cast<FnBraille>(GetProcAddress(g_module, "Tolk_Braille"));

        if (!g_load || !g_output)
        {
            FreeLibrary(g_module);
            g_module = nullptr;
            return false;
        }
        // Screen readers only: SAPI is spoken through by the mod itself, on a thread of its
        // own, which Tolk's driver is not.
        if (g_trySapi) g_trySapi(false);
        if (g_preferSapi) g_preferSapi(false);
        g_load();
        g_loaded = true;
        return true;
    }

    void Unload()
    {
        if (!g_loaded) return;
        if (g_unload) g_unload();
        if (g_module) FreeLibrary(g_module);
        g_module = nullptr;
        g_loaded = false;
    }

    bool IsLoaded()
    {
        return g_loaded;
    }

    bool Output(const wchar_t* text, bool interrupt)
    {
        return (g_loaded && g_output && text) ? g_output(text, interrupt) : false;
    }

    bool Silence()
    {
        return (g_loaded && g_silence) ? g_silence() : false;
    }

    bool IsSpeaking()
    {
        return (g_loaded && g_isSpeaking) ? g_isSpeaking() : false;
    }

    bool HasSpeech()
    {
        return (g_loaded && g_hasSpeech) ? g_hasSpeech() : false;
    }

    bool HasBraille()
    {
        return (g_loaded && g_hasBraille) ? g_hasBraille() : false;
    }

    std::wstring DetectScreenReader()
    {
        if (!g_loaded || !g_detect) return {};
        const wchar_t* name = g_detect();
        return name ? std::wstring(name) : std::wstring();
    }

    bool Braille(const wchar_t* text)
    {
        return (g_loaded && g_braille && text) ? g_braille(text) : false;
    }
}
