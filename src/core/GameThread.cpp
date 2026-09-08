#include "core/GameThread.hpp"

#include "core/Log.hpp"

#include <Unreal/Hooks.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace qa::gamethread
{
    namespace
    {
        std::mutex g_queueMutex;
        std::deque<Task> g_queue;
        std::vector<std::pair<std::wstring, Poller>> g_pollers;
        std::atomic<std::thread::id> g_gameThreadId{};
        std::atomic<unsigned long long> g_frame{0};
        std::chrono::steady_clock::time_point g_firstTick{};
        bool g_installed = false;

        void OnEngineTick(RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::UEngine*, float deltaSeconds, bool)
        {
            g_gameThreadId = std::this_thread::get_id();
            if (g_firstTick.time_since_epoch().count() == 0) g_firstTick = std::chrono::steady_clock::now();
            ++g_frame;

            std::deque<Task> tasks;
            {
                std::lock_guard lock(g_queueMutex);
                tasks.swap(g_queue);
            }
            for (auto& task : tasks)
            {
                try
                {
                    task();
                }
                catch (const std::exception& e)
                {
                    log::Error(L"game thread task failed: {}", std::wstring(e.what(), e.what() + strlen(e.what())));
                }
                catch (...)
                {
                    log::Error(L"game thread task failed (unknown exception)");
                }
            }

            for (auto& [name, poller] : g_pollers)
            {
                try
                {
                    poller(deltaSeconds);
                }
                catch (const std::exception& e)
                {
                    log::Error(L"poller '{}' failed: {}", name, std::wstring(e.what(), e.what() + strlen(e.what())));
                }
                catch (...)
                {
                    log::Error(L"poller '{}' failed (unknown exception)", name);
                }
            }
        }
    }

    void Install()
    {
        if (g_installed) return;
        g_installed = true;
        RC::Unreal::Hook::FCallbackOptions options{};
        options.bReadonly = true;
        options.OwnerModName = STR("QuarryAccess");
        options.HookName = STR("GameThreadPump");
        RC::Unreal::Hook::RegisterEngineTickPostCallback(&OnEngineTick, options);
        log::Info(L"game thread pump installed (engine tick post callback)");
    }

    void Post(Task task)
    {
        std::lock_guard lock(g_queueMutex);
        g_queue.push_back(std::move(task));
    }

    void AddPoller(std::wstring name, Poller poller)
    {
        g_pollers.emplace_back(std::move(name), std::move(poller));
    }

    bool IsGameThread()
    {
        return std::this_thread::get_id() == g_gameThreadId.load();
    }

    unsigned long long FrameCount()
    {
        return g_frame.load();
    }

    double NowSeconds()
    {
        if (g_firstTick.time_since_epoch().count() == 0) return 0.0;
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_firstTick).count();
    }
}
