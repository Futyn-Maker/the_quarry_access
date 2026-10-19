#include "core/Gate.hpp"

#include "core/Log.hpp"

#include <atomic>

namespace qa::gate
{
    namespace
    {
        std::atomic<bool> g_on{true};
    }

    bool IsOn()
    {
        return g_on.load(std::memory_order_relaxed);
    }

    void Set(bool on)
    {
        if (g_on.exchange(on) == on) return;
        log::Info(L"gate: the mod is switched {}", on ? L"on" : L"off");
    }
}
