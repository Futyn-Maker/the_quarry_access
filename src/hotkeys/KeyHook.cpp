#include "hotkeys/KeyHook.hpp"

#include "core/Log.hpp"
#include "input/InputNames.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

namespace qa::keyhook
{
    namespace
    {
        std::vector<Binding> g_bindings; // set before the hook thread starts, then read only
        std::vector<int> g_watched;      // likewise
        Watcher g_watcher = nullptr;
        std::mutex g_mutex;
        std::vector<int> g_presses;
        std::array<bool, 256> g_down{};        // the hook thread's own
        std::array<bool, 256> g_watchedDown{}; // likewise, for the keys that are only watched
        std::atomic<bool> g_active{false};
        std::atomic<bool> g_context{false};
        std::atomic<DWORD> g_threadId{0};
        HHOOK g_hook = nullptr;
        HANDLE g_ready = nullptr;
        std::thread g_thread;

        bool GameInForeground()
        {
            HWND foreground = GetForegroundWindow();
            if (!foreground) return false;
            DWORD pid = 0;
            GetWindowThreadProcessId(foreground, &pid);
            return pid == GetCurrentProcessId();
        }

        bool ModifierDown(int vk)
        {
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        }

        LRESULT CALLBACK Proc(int code, WPARAM wParam, LPARAM lParam)
        {
            if (code == HC_ACTION && lParam != 0)
            {
                const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
                const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
                const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
                const int vk = static_cast<int>(key->vkCode);
                // Keys the mod itself sends (the walk) and keys pressed for another window
                // are not the mod's to take.
                if ((down || up) && vk > 0 && vk < 256 && (key->flags & LLKHF_INJECTED) == 0 && GameInForeground())
                {
                    // A watched key is answered and then left alone: the mod acts on it before
                    // the game is given it, and the game still gets it.
                    if (g_watcher && std::find(g_watched.begin(), g_watched.end(), vk) != g_watched.end())
                    {
                        if (down)
                        {
                            // The key repeats while held; only the press counts.
                            if (!g_watchedDown[static_cast<size_t>(vk)]) g_watcher(vk);
                            g_watchedDown[static_cast<size_t>(vk)] = true;
                        }
                        else
                        {
                            g_watchedDown[static_cast<size_t>(vk)] = false;
                        }
                        input::NoteKeyboardActivity();
                    }

                    const bool ctrl = ModifierDown(VK_CONTROL);
                    const bool alt = ModifierDown(VK_MENU);
                    const bool shift = ModifierDown(VK_SHIFT);
                    for (const auto& binding : g_bindings)
                    {
                        if (binding.vk != vk) continue;
                        if (down)
                        {
                            if (ctrl != binding.ctrl || alt != binding.alt || shift != binding.shift) continue;
                            if (binding.contextual && !g_context.load(std::memory_order_relaxed)) continue;
                            // The key repeats while held; only the press counts.
                            if (!g_down[static_cast<size_t>(vk)])
                            {
                                std::lock_guard lock(g_mutex);
                                g_presses.push_back(binding.id);
                            }
                            g_down[static_cast<size_t>(vk)] = true;
                            input::NoteKeyboardActivity();
                            return 1;
                        }
                        if (g_down[static_cast<size_t>(vk)])
                        {
                            g_down[static_cast<size_t>(vk)] = false;
                            return 1;
                        }
                    }
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        void ThreadMain()
        {
            g_threadId.store(GetCurrentThreadId());
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Proc),
                               &module);
            g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &Proc, module, 0);
            g_active.store(g_hook != nullptr);
            SetEvent(g_ready);
            if (!g_hook) return;
            // The hook is called on this thread, through its message loop, so nothing the
            // game does can hold a key up.
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            UnhookWindowsHookEx(g_hook);
            g_hook = nullptr;
            g_active.store(false);
        }
    }

    void Watch(std::vector<int> vks, Watcher handler)
    {
        g_watched = std::move(vks);
        g_watcher = handler;
    }

    bool Install(std::vector<Binding> bindings)
    {
        if (g_thread.joinable()) return g_active.load();
        g_bindings = std::move(bindings);
        if (g_bindings.empty()) return false;
        g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_ready) return false;
        g_thread = std::thread(&ThreadMain);
        WaitForSingleObject(g_ready, 2000);
        return g_active.load();
    }

    void Uninstall()
    {
        if (!g_thread.joinable()) return;
        for (int i = 0; i < 200 && g_threadId.load() == 0; ++i)
            Sleep(10);
        PostThreadMessageW(g_threadId.load(), WM_QUIT, 0, 0);
        g_thread.join();
        if (g_ready) CloseHandle(g_ready);
        g_ready = nullptr;
        g_threadId.store(0);
    }

    bool Active()
    {
        return g_active.load();
    }

    void SetContext(bool active)
    {
        g_context.store(active, std::memory_order_relaxed);
    }

    std::vector<int> TakePresses()
    {
        std::lock_guard lock(g_mutex);
        std::vector<int> presses = std::move(g_presses);
        g_presses.clear();
        return presses;
    }
}
