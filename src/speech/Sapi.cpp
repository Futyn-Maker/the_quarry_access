#include "speech/Sapi.hpp"

#include "core/Log.hpp"
#include "core/Strings.hpp"

#include <windows.h>

#include <sapi.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace qa::sapi
{
    namespace
    {
        struct Request
        {
            std::wstring text; // empty: silence
            bool interrupt = false;
        };

        std::mutex g_mutex;
        std::deque<Request> g_queue;
        std::wstring g_wantedLanguage; // the locale code the voice is to speak
        std::wstring g_voiceName;
        std::atomic<bool> g_reselect{false};
        std::atomic<bool> g_quit{false};
        std::atomic<bool> g_available{false};
        bool g_started = false;
        HANDLE g_wake = nullptr;
        HANDLE g_ready = nullptr;
        std::thread g_thread;
        ISpVoice* g_voice = nullptr; // the speech thread's own

        // The language identifiers SAPI writes into a voice's Language attribute, for the
        // mod's language codes.
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

        std::wstring TokenDescription(ISpObjectToken* token)
        {
            LPWSTR text = nullptr;
            std::wstring result;
            if (token && SUCCEEDED(token->GetStringValue(nullptr, &text)) && text)
            {
                result = text;
                CoTaskMemFree(text);
            }
            return result;
        }

        // The languages a voice lists, as SAPI writes them: "419" or "409;809".
        std::vector<unsigned> TokenLanguages(ISpObjectToken* token)
        {
            std::vector<unsigned> languages;
            ISpDataKey* attributes = nullptr;
            if (!token || FAILED(token->OpenKey(L"Attributes", &attributes)) || !attributes) return languages;
            LPWSTR text = nullptr;
            if (SUCCEEDED(attributes->GetStringValue(L"Language", &text)) && text)
            {
                for (const auto& part : str::Split(text, L';'))
                {
                    try
                    {
                        languages.push_back(static_cast<unsigned>(std::stoul(str::Trim(part), nullptr, 16)));
                    }
                    catch (...)
                    {
                    }
                }
                CoTaskMemFree(text);
            }
            attributes->Release();
            return languages;
        }

        // 2 for the very language, 1 for another dialect of it, 0 for none.
        int Fit(const std::vector<unsigned>& languages, unsigned wanted)
        {
            int best = 0;
            for (const unsigned language : languages)
            {
                if (language == wanted) return 2;
                if ((language & 0x3FF) == (wanted & 0x3FF)) best = 1;
            }
            return best;
        }

        void SetVoiceName(const std::wstring& name)
        {
            std::lock_guard lock(g_mutex);
            g_voiceName = name;
        }

        // The default voice when it speaks the language, else the voice that fits it best.
        void ChooseVoice(const std::wstring& code)
        {
            if (!g_voice) return;
            const unsigned wanted = LanguageIdOf(code);
            ISpObjectToken* current = nullptr;
            std::wstring currentName;
            int currentFit = 0;
            if (SUCCEEDED(g_voice->GetVoice(&current)) && current)
            {
                currentName = TokenDescription(current);
                currentFit = wanted == 0 ? 2 : Fit(TokenLanguages(current), wanted);
                current->Release();
            }
            if (currentFit == 2 || wanted == 0)
            {
                SetVoiceName(currentName);
                log::Info(L"sapi: the voice {} speaks {}", currentName, code);
                return;
            }
            ISpObjectTokenCategory* category = nullptr;
            if (FAILED(CoCreateInstance(__uuidof(SpObjectTokenCategory), nullptr, CLSCTX_ALL, __uuidof(ISpObjectTokenCategory),
                                        reinterpret_cast<void**>(&category))) ||
                !category)
            {
                SetVoiceName(currentName);
                return;
            }
            IEnumSpObjectTokens* tokens = nullptr;
            ISpObjectToken* best = nullptr;
            int bestFit = currentFit;
            std::wstring bestName = currentName;
            if (SUCCEEDED(category->SetId(SPCAT_VOICES, FALSE)) && SUCCEEDED(category->EnumTokens(nullptr, nullptr, &tokens)) && tokens)
            {
                ISpObjectToken* token = nullptr;
                while (tokens->Next(1, &token, nullptr) == S_OK && token)
                {
                    const int fit = Fit(TokenLanguages(token), wanted);
                    if (fit > bestFit)
                    {
                        if (best) best->Release();
                        best = token;
                        bestFit = fit;
                        bestName = TokenDescription(token);
                        token = nullptr;
                    }
                    if (token) token->Release();
                    if (bestFit == 2) break;
                }
                tokens->Release();
            }
            category->Release();
            if (best)
            {
                if (SUCCEEDED(g_voice->SetVoice(best)))
                    log::Info(L"sapi: the voice {} is used for {} in place of the default {}", bestName, code, currentName);
                else
                    bestName = currentName;
                best->Release();
            }
            else
            {
                log::Info(L"sapi: no voice speaks {}; the default voice {} stays", code, currentName);
            }
            SetVoiceName(bestName);
        }

        void Pump()
        {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        void ThreadMain()
        {
            const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (SUCCEEDED(CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL, __uuidof(ISpVoice), reinterpret_cast<void**>(&g_voice))) && g_voice)
            {
                ISpObjectToken* current = nullptr;
                if (SUCCEEDED(g_voice->GetVoice(&current)) && current)
                {
                    SetVoiceName(TokenDescription(current));
                    current->Release();
                }
                g_available.store(true);
                log::Info(L"sapi: ready with the voice {}", VoiceName());
            }
            else
            {
                log::Error(L"sapi: no voice could be created");
            }
            SetEvent(g_ready);
            while (!g_quit.load())
            {
                MsgWaitForMultipleObjectsEx(1, &g_wake, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                Pump();
                if (g_quit.load()) break;
                if (g_reselect.exchange(false))
                {
                    std::wstring code;
                    {
                        std::lock_guard lock(g_mutex);
                        code = g_wantedLanguage;
                    }
                    ChooseVoice(code);
                }
                for (;;)
                {
                    Request request;
                    {
                        std::lock_guard lock(g_mutex);
                        if (g_queue.empty()) break;
                        request = std::move(g_queue.front());
                        g_queue.pop_front();
                    }
                    if (!g_voice) continue;
                    if (request.text.empty())
                    {
                        g_voice->Speak(nullptr, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
                        continue;
                    }
                    DWORD flags = SPF_ASYNC | SPF_IS_NOT_XML;
                    if (request.interrupt) flags |= SPF_PURGEBEFORESPEAK;
                    const HRESULT result = g_voice->Speak(request.text.c_str(), flags, nullptr);
                    if (FAILED(result)) log::Error(L"sapi: the voice refused a line ({:#x})", static_cast<unsigned>(result));
                }
            }
            if (g_voice)
            {
                g_voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
                g_voice->Release();
                g_voice = nullptr;
            }
            g_available.store(false);
            if (SUCCEEDED(apartment)) CoUninitialize();
        }
    }

    bool Start()
    {
        if (g_started) return g_available.load();
        g_started = true;
        g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_wake || !g_ready) return false;
        g_thread = std::thread(&ThreadMain);
        WaitForSingleObject(g_ready, 3000);
        return g_available.load();
    }

    void Stop()
    {
        if (!g_started) return;
        g_quit.store(true);
        if (g_wake) SetEvent(g_wake);
        if (g_thread.joinable()) g_thread.join();
        if (g_wake) CloseHandle(g_wake);
        if (g_ready) CloseHandle(g_ready);
        g_wake = g_ready = nullptr;
        g_started = false;
    }

    bool IsAvailable()
    {
        return g_available.load();
    }

    void SelectVoiceFor(std::wstring_view localeCode)
    {
        {
            std::lock_guard lock(g_mutex);
            g_wantedLanguage = std::wstring(localeCode);
        }
        g_reselect.store(true);
        if (g_wake) SetEvent(g_wake);
    }

    std::wstring VoiceName()
    {
        std::lock_guard lock(g_mutex);
        return g_voiceName;
    }

    bool Speak(std::wstring_view text, bool interrupt)
    {
        if (!g_available.load() || text.empty()) return false;
        {
            std::lock_guard lock(g_mutex);
            if (interrupt) g_queue.clear();
            g_queue.push_back(Request{std::wstring(text), interrupt});
        }
        if (g_wake) SetEvent(g_wake);
        return true;
    }

    void Silence()
    {
        if (!g_available.load()) return;
        {
            std::lock_guard lock(g_mutex);
            g_queue.clear();
            g_queue.push_back(Request{});
        }
        if (g_wake) SetEvent(g_wake);
    }
}
