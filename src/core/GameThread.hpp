#pragma once
// Game-thread pump. UE4SS calls CppUserModBase::on_update() and key events on
// its own event-loop thread, so every touch of a UObject is funneled through
// the engine-tick post callback registered here. Post() enqueues work from any
// thread; Tick() (called by the engine-tick callback) drains it, then runs the
// registered per-frame pollers.

#include <functional>
#include <string>

namespace qa::gamethread
{
    using Task = std::function<void()>;
    using Poller = std::function<void(float deltaSeconds)>;

    // Installs the engine-tick hook. Call from on_unreal_init().
    void Install();

    // Enqueue work for the next engine tick (thread-safe).
    void Post(Task task);

    // Register a per-frame poller (call during initialization).
    void AddPoller(std::wstring name, Poller poller);

    bool IsGameThread();
    unsigned long long FrameCount();
    double NowSeconds(); // steady clock seconds since first tick
}
