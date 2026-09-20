#include "speech/Outputs.hpp"

#include "core/Log.hpp"
#include "core/Strings.hpp"
#include "speech/PrismBridge.hpp"
#include "speech/SapiVoices.hpp"

#include <windows.h>

#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace qa::outputs
{
    namespace
    {
        // The screen readers Prism knows on Windows. SAPI and OneCore are left out on purpose:
        // they are voices, not readers, and it is the mod that decides when a voice speaks
        // instead of a reader. The registry is walked in Prism's own priority order, which puts
        // every reader with an API of its own above UI Automation, whose notifications are only
        // heard by whatever assistive software is listening for them.
        constexpr PrismBackendId kReaders[] = {
            PRISM_BACKEND_NVDA,      PRISM_BACKEND_JAWS,         PRISM_BACKEND_ZDSR,          PRISM_BACKEND_PC_TALKER,   PRISM_BACKEND_BOY_PC_READER,
            PRISM_BACKEND_ZOOM_TEXT, PRISM_BACKEND_SENSE_READER, PRISM_BACKEND_SYSTEM_ACCESS, PRISM_BACKEND_WINDOW_EYES, PRISM_BACKEND_UIA,
        };

        bool IsReader(PrismBackendId id)
        {
            return std::find(std::begin(kReaders), std::end(kReaders), id) != std::end(kReaders);
        }

        struct Request
        {
            enum class Kind
            {
                Say,
                Silence,
                Voice,
            };

            Kind kind = Kind::Say;
            std::wstring text;
            bool interrupt = false;
            bool throughSapi = false;
        };

        std::mutex g_queueMutex;
        std::deque<Request> g_queue;
        HANDLE g_wake = nullptr;
        HANDLE g_ready = nullptr;
        std::atomic<bool> g_quit{false};
        std::thread g_thread;
        bool g_started = false;
        std::wstring g_dllPath;

        // What the rest of the mod reads, written by the speech thread.
        std::mutex g_stateMutex;
        std::wstring g_readerName;
        std::wstring g_voiceName;
        std::atomic<bool> g_hasReader{false};
        std::atomic<bool> g_readerBraille{false};
        std::atomic<bool> g_sapiOk{false};

        // The speech thread's own.
        PrismBackend* g_reader = nullptr;
        PrismBackendId g_readerId = PRISM_BACKEND_INVALID;
        PrismBackend* g_sapi = nullptr;
        std::wstring g_language;
        std::unordered_map<std::wstring, std::vector<unsigned>> g_voiceLanguages;

        void SetVoiceName(const std::wstring& name)
        {
            std::lock_guard lock(g_stateMutex);
            g_voiceName = name;
        }

        // Takes over a screen reader, or none, and says in the log what changed.
        void AdoptReader(PrismBackend* backend, PrismBackendId id)
        {
            if (g_reader && g_reader != backend) prism::Free(g_reader);
            g_reader = backend;
            g_readerId = id;

            std::wstring name;
            bool braille = false;
            if (backend)
            {
                name = prism::Name(backend);
                braille = (prism::Features(backend) & PRISM_BACKEND_SUPPORTS_BRAILLE) != 0;
            }
            std::wstring previous;
            {
                std::lock_guard lock(g_stateMutex);
                previous = g_readerName;
                g_readerName = name;
            }
            g_hasReader.store(backend != nullptr);
            g_readerBraille.store(braille);
            if (name == previous) return;
            if (name.empty())
                log::Info(L"speech: the screen reader {} is gone; SAPI speaks", previous);
            else if (previous.empty())
                log::Info(L"speech: screen reader {} (braille: {})", name, braille);
            else
                log::Info(L"speech: screen reader {} in place of {} (braille: {})", name, previous, braille);
        }

        // The screen reader that is running now. The one already in hand is kept while its
        // engine still answers; otherwise the registry is walked from the top, so a reader that
        // ranks higher than the one in use takes over as soon as it starts.
        void RefreshReader()
        {
            if (!prism::IsLoaded()) return;
            const prism::Api& api = prism::Get();
            PrismContext* context = prism::Context();
            const size_t count = api.registry_count(context);
            for (size_t i = 0; i < count; ++i)
            {
                const PrismBackendId id = api.registry_id_at(context, i);
                if (!IsReader(id)) continue;
                if (g_reader && id == g_readerId)
                {
                    if (prism::Running(g_reader)) return;
                    // This one closed. Its backend is dead for good: Prism never reconnects.
                    prism::Free(g_reader);
                    g_readerId = PRISM_BACKEND_INVALID;
                    continue;
                }
                if (PrismBackend* backend = prism::Create(id))
                {
                    AdoptReader(backend, id);
                    return;
                }
            }
            AdoptReader(nullptr, PRISM_BACKEND_INVALID);
        }

        void EnsureReader()
        {
            if (prism::TakeChanged()) RefreshReader();
        }

        // Prism hands out the name and the language of a voice in one buffer of its own, which
        // the next such call overwrites, so both are copied at once.
        std::wstring VoiceNameAt(size_t index)
        {
            const char* text = nullptr;
            if (!g_sapi || prism::Get().backend_get_voice_name(g_sapi, index, &text) != PRISM_OK || !text) return {};
            return str::Utf8ToWide(text);
        }

        std::string VoiceLanguageAt(size_t index)
        {
            const char* text = nullptr;
            if (!g_sapi || prism::Get().backend_get_voice_language(g_sapi, index, &text) != PRISM_OK || !text) return {};
            return std::string(text);
        }

        // The language identifiers Windows keeps for the mod's language codes.
        unsigned LanguageIdOf(std::wstring_view code)
        {
            const auto lower = str::ToLower(code);
            if (lower == L"ru_ru") return 0x419;
            if (lower == L"en_us") return 0x409;
            if (lower == L"en_gb") return 0x809;
            if (lower == L"de_de") return 0x407;
            if (lower == L"fr_fr") return 0x40C;
            if (lower == L"es_es") return 0xC0A;
            if (lower == L"es_mx") return 0x80A;
            if (lower == L"it_it") return 0x410;
            if (lower == L"pt_br") return 0x416;
            if (lower == L"pt_pt") return 0x816;
            if (lower == L"nl_nl") return 0x413;
            if (lower == L"da_dk") return 0x406;
            if (lower == L"sv_se") return 0x41D;
            if (lower == L"nn_no") return 0x814;
            if (lower == L"fi_fi") return 0x40B;
            if (lower == L"pl_pl") return 0x415;
            if (lower == L"tr_tr") return 0x41F;
            if (lower == L"ja_jp") return 0x411;
            if (lower == L"ko_kr") return 0x412;
            if (lower == L"zh_chs") return 0x804;
            if (lower == L"zh_cht") return 0x404;
            return 0;
        }

        // The same identifier behind the locale name Prism reports for a voice, such as "ru-ru".
        unsigned LanguageIdOfTag(const std::string& tag)
        {
            if (tag.empty()) return 0;
            const std::wstring wide = str::Utf8ToWide(tag);
            const LCID lcid = LocaleNameToLCID(wide.c_str(), 0);
            if (lcid == 0 || lcid == LOCALE_CUSTOM_UNSPECIFIED) return 0;
            return static_cast<unsigned>(LANGIDFROMLCID(lcid));
        }

        // 2 for the very language, 1 for another dialect of it, 0 for none.
        int Fit(unsigned language, unsigned wanted)
        {
            if (language == 0 || wanted == 0) return 0;
            if (language == wanted) return 2;
            return (language & 0x3FF) == (wanted & 0x3FF) ? 1 : 0;
        }

        // Every language a voice speaks: what SAPI lists in the voice's own attributes, and, for
        // a voice not found among those, whatever the library says about it.
        std::vector<unsigned> LanguagesOfVoice(size_t index, const std::wstring& name)
        {
            const auto found = g_voiceLanguages.find(name);
            if (found != g_voiceLanguages.end()) return found->second;
            const unsigned language = LanguageIdOfTag(VoiceLanguageAt(index));
            if (language == 0) return {};
            return {language};
        }

        int FitOf(const std::vector<unsigned>& languages, unsigned wanted)
        {
            int best = 0;
            for (const unsigned language : languages)
            {
                const int fit = Fit(language, wanted);
                if (fit > best) best = fit;
                if (best == 2) break;
            }
            return best;
        }

        // The voice Windows is set to when it speaks the language, else the one that fits best.
        void ChooseVoice(const std::wstring& code)
        {
            if (!g_sapi) return;
            const prism::Api& api = prism::Get();
            const unsigned wanted = LanguageIdOf(code);
            size_t count = 0;
            if (api.backend_count_voices(g_sapi, &count) != PRISM_OK) count = 0;
            size_t current = 0;
            if (api.backend_get_voice(g_sapi, &current) != PRISM_OK) current = 0;
            const std::wstring currentName = VoiceNameAt(current);
            const int currentFit = wanted == 0 ? 2 : FitOf(LanguagesOfVoice(current, currentName), wanted);
            if (currentFit == 2)
            {
                SetVoiceName(currentName);
                log::Info(L"speech: the voice {} speaks {}", currentName, code);
                return;
            }

            size_t best = current;
            int bestFit = currentFit;
            std::wstring bestName = currentName;
            for (size_t i = 0; i < count; ++i)
            {
                std::wstring name = VoiceNameAt(i);
                const int fit = FitOf(LanguagesOfVoice(i, name), wanted);
                if (fit > bestFit)
                {
                    best = i;
                    bestFit = fit;
                    bestName = std::move(name);
                }
                if (bestFit == 2) break;
            }
            if (best == current)
            {
                SetVoiceName(currentName);
                log::Info(L"speech: no voice speaks {}; the default voice {} stays", code, currentName);
                return;
            }
            if (api.backend_set_voice(g_sapi, best) == PRISM_OK)
            {
                SetVoiceName(bestName);
                log::Info(L"speech: the voice {} is used for {} in place of the default {}", bestName, code, currentName);
            }
            else
            {
                SetVoiceName(currentName);
                log::Error(L"speech: the voice {} could not be selected for {}", bestName, code);
            }
        }

        // SAPI by name, never the highest-ranking voice Prism can find: the player's Windows
        // speech settings are what a screen-reader user has already set up.
        void StartSapi()
        {
            g_sapi = prism::Create(PRISM_BACKEND_SAPI);
            g_sapiOk.store(g_sapi != nullptr);
            if (!g_sapi)
            {
                log::Error(L"speech: SAPI is not available; without a screen reader nothing will be spoken");
                SetVoiceName({});
                return;
            }
            g_voiceLanguages = sapivoices::Languages();
            size_t current = 0;
            if (prism::Get().backend_get_voice(g_sapi, &current) != PRISM_OK) current = 0;
            const std::wstring name = VoiceNameAt(current);
            SetVoiceName(name);
            log::Info(L"speech: SAPI ready with the voice {} ({} voice(s) with a language of their own)", name, g_voiceLanguages.size());
        }

        void RestartSapi()
        {
            prism::Free(g_sapi);
            g_sapiOk.store(false);
            StartSapi();
            if (g_sapi && !g_language.empty()) ChooseVoice(g_language);
        }

        bool SpeakThroughReader(const std::wstring& text, bool interrupt)
        {
            if (!g_reader) return false;
            const std::string utf8 = str::WideToUtf8(text);
            PrismError error = prism::Get().backend_output(g_reader, utf8.c_str(), interrupt);
            if (prism::Lost(error))
            {
                log::Info(L"speech: the screen reader stopped answering ({}); looking for one again", prism::ErrorText(error));
                prism::Free(g_reader);
                g_readerId = PRISM_BACKEND_INVALID;
                RefreshReader();
                if (!g_reader) return false;
                error = prism::Get().backend_output(g_reader, utf8.c_str(), interrupt);
            }
            if (error != PRISM_OK) log::Error(L"speech: the screen reader refused a line: {}", prism::ErrorText(error));
            return error == PRISM_OK;
        }

        void BrailleThroughReader(const std::wstring& text)
        {
            if (!g_reader || !g_readerBraille.load()) return;
            const std::string utf8 = str::WideToUtf8(text);
            const PrismError error = prism::Get().backend_braille(g_reader, utf8.c_str());
            if (error != PRISM_OK) log::Trace(L"speech: the braille display refused a line: {}", prism::ErrorText(error));
        }

        bool SpeakThroughSapi(const std::wstring& text, bool interrupt)
        {
            if (!g_sapi) return false;
            const std::string utf8 = str::WideToUtf8(text);
            PrismError error = prism::Get().backend_speak(g_sapi, utf8.c_str(), interrupt);
            if (prism::Lost(error))
            {
                log::Info(L"speech: SAPI stopped answering ({}); starting it again", prism::ErrorText(error));
                RestartSapi();
                if (!g_sapi) return false;
                error = prism::Get().backend_speak(g_sapi, utf8.c_str(), interrupt);
            }
            if (error != PRISM_OK) log::Error(L"speech: SAPI refused a line: {}", prism::ErrorText(error));
            return error == PRISM_OK;
        }

        void Deliver(const Request& request)
        {
            // What the mod asked for, unless the voice it asked for is not there.
            const bool throughSapi = (request.throughSapi && g_sapi != nullptr) || !g_reader;
            if (!throughSapi)
            {
                SpeakThroughReader(request.text, request.interrupt);
                return;
            }
            if (!SpeakThroughSapi(request.text, request.interrupt) && g_reader)
            {
                SpeakThroughReader(request.text, request.interrupt);
                return;
            }
            BrailleThroughReader(request.text);
        }

        void StopAll()
        {
            if (g_reader)
            {
                const PrismError error = prism::Get().backend_stop(g_reader);
                if (error != PRISM_OK) log::Trace(L"speech: the screen reader would not stop: {}", prism::ErrorText(error));
            }
            if (g_sapi)
            {
                const PrismError error = prism::Get().backend_stop(g_sapi);
                if (error != PRISM_OK) log::Trace(L"speech: SAPI would not stop: {}", prism::ErrorText(error));
            }
        }

        void ThreadMain()
        {
            // The mod's own apartment: every call into Prism is made on this thread, so the COM
            // proxies behind the Windows backends are only ever used where they were made.
            const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (prism::Load(g_dllPath))
            {
                log::Info(L"prism {} loaded from {}", prism::Version(), g_dllPath);
                RefreshReader();
                StartSapi();
            }
            SetEvent(g_ready);

            while (!g_quit.load())
            {
                WaitForSingleObject(g_wake, INFINITE);
                for (;;)
                {
                    Request request;
                    {
                        std::lock_guard lock(g_queueMutex);
                        if (g_queue.empty()) break;
                        request = std::move(g_queue.front());
                        g_queue.pop_front();
                    }
                    EnsureReader();
                    switch (request.kind)
                    {
                    case Request::Kind::Say: Deliver(request); break;
                    case Request::Kind::Silence: StopAll(); break;
                    case Request::Kind::Voice:
                        g_language = request.text;
                        ChooseVoice(g_language);
                        break;
                    }
                }
            }

            StopAll();
            prism::Free(g_reader);
            prism::Free(g_sapi);
            prism::Unload();
            if (SUCCEEDED(apartment)) CoUninitialize();
        }

        void Post(Request&& request)
        {
            {
                std::lock_guard lock(g_queueMutex);
                // An interrupting line, and a stop, replace the lines that are waiting; the
                // choice of voice is not speech and stays.
                if (request.kind == Request::Kind::Silence || (request.kind == Request::Kind::Say && request.interrupt))
                    std::erase_if(g_queue, [](const Request& waiting) { return waiting.kind == Request::Kind::Say; });
                g_queue.push_back(std::move(request));
            }
            if (g_wake) SetEvent(g_wake);
        }
    }

    bool Start(const std::wstring& prismDllPath)
    {
        if (g_started) return CanSpeak();
        g_dllPath = prismDllPath;
        g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_wake || !g_ready)
        {
            log::Error(L"speech: the speech thread could not be started");
            return false;
        }
        g_started = true;
        g_thread = std::thread(&ThreadMain);
        WaitForSingleObject(g_ready, 5000);
        return CanSpeak();
    }

    void Stop()
    {
        if (!g_started) return;
        {
            std::lock_guard lock(g_queueMutex);
            g_queue.clear();
        }
        g_quit.store(true);
        if (g_wake) SetEvent(g_wake);
        if (g_thread.joinable()) g_thread.join();
        if (g_wake) CloseHandle(g_wake);
        if (g_ready) CloseHandle(g_ready);
        g_wake = g_ready = nullptr;
        g_started = false;
    }

    bool CanSpeak()
    {
        return g_hasReader.load() || g_sapiOk.load();
    }

    std::wstring ReaderName()
    {
        std::lock_guard lock(g_stateMutex);
        return g_readerName;
    }

    bool ReaderHasBraille()
    {
        return g_readerBraille.load();
    }

    bool SapiAvailable()
    {
        return g_sapiOk.load();
    }

    std::wstring SapiVoiceName()
    {
        std::lock_guard lock(g_stateMutex);
        return g_voiceName;
    }

    void SelectVoiceFor(std::wstring_view localeCode)
    {
        if (!g_started) return;
        Request request;
        request.kind = Request::Kind::Voice;
        request.text = std::wstring(localeCode);
        Post(std::move(request));
    }

    void Say(std::wstring_view text, bool interrupt, bool throughSapi)
    {
        if (!g_started || text.empty()) return;
        Request request;
        request.kind = Request::Kind::Say;
        request.text = std::wstring(text);
        request.interrupt = interrupt;
        request.throughSapi = throughSapi;
        Post(std::move(request));
    }

    void Silence()
    {
        if (!g_started) return;
        Request request;
        request.kind = Request::Kind::Silence;
        Post(std::move(request));
    }
}
