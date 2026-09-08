#include "hooks/HookDispatcher.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace qa::hooks
{
    using RC::Unreal::UClass;
    using RC::Unreal::UFunction;
    namespace Hook = RC::Unreal::Hook;

    namespace
    {
        struct Entry
        {
            std::wstring owner;
            std::wstring function;
            ScriptHandler handler;
        };

        struct ConstructedEntry
        {
            std::wstring className;
            bool suffix = false;
            ConstructedHandler handler;
        };

        std::mutex g_mutex;
        std::vector<std::unique_ptr<Entry>> g_post;
        std::vector<std::unique_ptr<Entry>> g_pre;
        std::unordered_map<UFunction*, std::vector<Entry*>> g_postCache;
        std::unordered_map<UFunction*, std::vector<Entry*>> g_preCache;
        std::vector<ConstructedEntry> g_constructed;
        std::atomic<bool> g_trace{false};
        std::wstring g_lastHook;
        bool g_installed = false;

        // tracer rate limit
        std::chrono::steady_clock::time_point g_traceWindow{};
        int g_traceCount = 0;

        bool IsSkippedFunctionName(const std::wstring& name)
        {
            if (name == L"Tick" || name == L"ReceiveTick" || name == L"OnPaint" || name == L"BlueprintUpdateAnimation") return true;
            if (str::StartsWith(name, L"ExecuteUbergraph")) return true;
            return false;
        }

        std::vector<Entry*> Resolve(std::vector<std::unique_ptr<Entry>>& entries, UFunction* node, const std::wstring& functionName)
        {
            std::vector<Entry*> matches;
            if (entries.empty()) return matches;
            auto* declaring = static_cast<UClass*>(node->GetOuterPrivate());
            if (!declaring) return matches;
            for (auto& entry : entries)
            {
                if (entry->function != functionName) continue;
                if (obj::ClassIsChildOf(declaring, entry->owner)) matches.push_back(entry.get());
            }
            return matches;
        }

        void RunHandlers(const std::vector<Entry*>& handlers, UObject* self, FFrame& stack, const std::wstring& label)
        {
            for (auto* entry : handlers)
            {
                g_lastHook = label;
                try
                {
                    entry->handler(self, stack);
                }
                catch (const std::exception& e)
                {
                    log::Error(L"hook {} failed: {}", label, str::Utf8ToWide(e.what()));
                }
                catch (...)
                {
                    log::Error(L"hook {} failed (unknown exception)", label);
                }
            }
        }

        void Trace(UObject* self, FFrame& stack, const std::wstring& functionName)
        {
            const auto& prefixes = cfg::Get().traceClassPrefixes;
            const std::wstring cls = obj::ClassName(self);
            bool match = prefixes.empty();
            for (const auto& p : prefixes)
            {
                if (cls.find(p) != std::wstring::npos)
                {
                    match = true;
                    break;
                }
            }
            if (!match) return;
            const auto now = std::chrono::steady_clock::now();
            if (now - g_traceWindow > std::chrono::seconds(1))
            {
                g_traceWindow = now;
                g_traceCount = 0;
            }
            if (++g_traceCount > 200) return;
            log::Write(log::Level::Info, std::format(L"TRACE {}:{}({}) self={}", cls, functionName, params::Describe(stack), obj::ObjectName(self)));
        }

        void Dispatch(bool post, UObject* self, FFrame& stack)
        {
            UFunction* node = stack.Node();
            if (!node) return;
            auto& cache = post ? g_postCache : g_preCache;
            auto& entries = post ? g_post : g_pre;

            std::vector<Entry*> handlers;
            bool cached = false;
            {
                std::lock_guard lock(g_mutex);
                const auto it = cache.find(node);
                if (it != cache.end())
                {
                    handlers = it->second;
                    cached = true;
                }
            }
            std::wstring functionName;
            if (!cached)
            {
                functionName = node->GetName();
                if (!IsSkippedFunctionName(functionName)) handlers = Resolve(entries, node, functionName);
                std::lock_guard lock(g_mutex);
                cache[node] = handlers;
            }

            if (post && g_trace.load(std::memory_order_relaxed))
            {
                if (functionName.empty()) functionName = node->GetName();
                if (!IsSkippedFunctionName(functionName)) Trace(self, stack, functionName);
            }

            if (handlers.empty()) return;
            if (functionName.empty()) functionName = node->GetName();
            RunHandlers(handlers, self, stack, obj::ClassName(self) + L":" + functionName + (post ? L"" : L" (pre)"));
        }

        void OnScriptPost(Hook::TCallbackIterationData<void>&, UObject* context, FFrame& stack, void*)
        {
            Dispatch(true, context, stack);
        }

        void OnScriptPreCallback(Hook::TCallbackIterationData<void>&, UObject* context, FFrame& stack, void*)
        {
            Dispatch(false, context, stack);
        }

        void OnConstructedObject(Hook::TCallbackIterationData<UObject*>& data, const RC::Unreal::FStaticConstructObjectParameters& params)
        {
            UObject* object = data.GetOriginalFunctionCallResult();
            if (!object || !params.Class) return;
            std::vector<ConstructedHandler> matched;
            {
                std::lock_guard lock(g_mutex);
                if (g_constructed.empty()) return;
                const std::wstring cls = obj::ClassName(const_cast<UClass*>(params.Class));
                for (const auto& entry : g_constructed)
                {
                    const bool match = entry.suffix ? str::EndsWith(cls, entry.className) : cls == entry.className;
                    if (match) matched.push_back(entry.handler);
                }
            }
            if (matched.empty()) return;
            gamethread::Post(
                [object, matched = std::move(matched)]()
                {
                    if (!obj::IsLive(object)) return;
                    for (const auto& handler : matched)
                    {
                        try
                        {
                            handler(object);
                        }
                        catch (...)
                        {
                            log::Error(L"constructed-object handler failed for {}", obj::ClassName(object));
                        }
                    }
                });
        }
    }

    void Install()
    {
        if (g_installed) return;
        g_installed = true;

        Hook::FCallbackOptions post{};
        post.bReadonly = true;
        post.OwnerModName = STR("QuarryAccess");
        post.HookName = STR("ScriptPost");
        Hook::RegisterProcessLocalScriptFunctionPostCallback(&OnScriptPost, post);

        Hook::FCallbackOptions pre{};
        pre.bReadonly = true;
        pre.OwnerModName = STR("QuarryAccess");
        pre.HookName = STR("ScriptPre");
        Hook::RegisterProcessLocalScriptFunctionPreCallback(&OnScriptPreCallback, pre);

        Hook::FCallbackOptions sco{};
        sco.bReadonly = true;
        sco.OwnerModName = STR("QuarryAccess");
        sco.HookName = STR("Constructed");
        Hook::RegisterStaticConstructObjectPostCallback(&OnConstructedObject, sco);

        log::Info(L"hook dispatcher installed");
    }

    void OnScript(std::wstring ownerClass, std::wstring function, ScriptHandler handler)
    {
        std::lock_guard lock(g_mutex);
        g_post.push_back(std::make_unique<Entry>(Entry{std::move(ownerClass), std::move(function), std::move(handler)}));
        g_postCache.clear();
    }

    void OnScriptPre(std::wstring ownerClass, std::wstring function, ScriptHandler handler)
    {
        std::lock_guard lock(g_mutex);
        g_pre.push_back(std::make_unique<Entry>(Entry{std::move(ownerClass), std::move(function), std::move(handler)}));
        g_preCache.clear();
    }

    bool OnNative(const std::wstring& fullName, ScriptHandler pre, ScriptHandler post)
    {
        try
        {
            auto preCallable = [pre, fullName](RC::Unreal::UnrealScriptFunctionCallableContext& ctx, void*)
            {
                if (!pre) return;
                g_lastHook = fullName + L" (pre)";
                try
                {
                    pre(ctx.Context, ctx.TheStack);
                }
                catch (...)
                {
                    log::Error(L"native pre hook {} failed", fullName);
                }
            };
            auto postCallable = [post, fullName](RC::Unreal::UnrealScriptFunctionCallableContext& ctx, void*)
            {
                if (!post) return;
                g_lastHook = fullName;
                try
                {
                    post(ctx.Context, ctx.TheStack);
                }
                catch (...)
                {
                    log::Error(L"native post hook {} failed", fullName);
                }
            };
            RC::Unreal::UObjectGlobals::RegisterHook(fullName, preCallable, postCallable, nullptr);
            log::Info(L"native hook installed: {}", fullName);
            return true;
        }
        catch (const std::exception& e)
        {
            log::Error(L"native hook failed for {}: {}", fullName, str::Utf8ToWide(e.what()));
            return false;
        }
        catch (...)
        {
            log::Error(L"native hook failed for {}", fullName);
            return false;
        }
    }

    void OnConstructed(std::wstring className, bool suffix, ConstructedHandler handler)
    {
        std::lock_guard lock(g_mutex);
        g_constructed.push_back(ConstructedEntry{std::move(className), suffix, std::move(handler)});
    }

    void SetTrace(bool enabled)
    {
        g_trace = enabled;
    }

    bool TraceEnabled()
    {
        return g_trace.load();
    }

    std::wstring LastHook()
    {
        return g_lastHook;
    }

    size_t ScriptHandlerCount()
    {
        std::lock_guard lock(g_mutex);
        return g_post.size() + g_pre.size();
    }
}
