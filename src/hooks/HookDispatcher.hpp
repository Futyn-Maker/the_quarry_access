#pragma once
// Central hook installation and routing.
//  - One ProcessLocalScriptFunction post (and pre) callback routes Blueprint
//    function executions by (owner class name, function name) to handlers.
//  - Native UFunctions are hooked individually with UObjectGlobals::RegisterHook.
//  - StaticConstructObject post callbacks record new objects; class-filtered
//    listeners are notified on the game thread on the next tick.

#include <Unreal/FFrame.hpp>
#include <Unreal/UObject.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace qa::hooks
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    using ScriptHandler = std::function<void(UObject* self, FFrame& stack)>;
    using ConstructedHandler = std::function<void(UObject* object)>;

    void Install();

    // Blueprint function routing. `ownerClass` is the short class name that declares
    // the function (e.g. "MenuButton_C"); subclasses inherit the match automatically.
    void OnScript(std::wstring ownerClass, std::wstring function, ScriptHandler handler);
    void OnScriptPre(std::wstring ownerClass, std::wstring function, ScriptHandler handler);

    // Native function hook by full name ("/Script/SMGRuntime.GFSubtitleLineWidget:SetCurrentText").
    // Returns false (and logs) when the function does not exist in this build.
    bool OnNative(const std::wstring& fullName, ScriptHandler pre, ScriptHandler post);

    // Object construction notifications, filtered by short class name (exact) or
    // by suffix when `suffix` is true. Delivered on the game thread.
    void OnConstructed(std::wstring className, bool suffix, ConstructedHandler handler);

    // Function tracer (dev): logs every Blueprint call whose owner class matches the configured prefixes.
    void SetTrace(bool enabled);
    bool TraceEnabled();

    // Last hook that ran (crash breadcrumb).
    std::wstring LastHook();

    // Statistics for the log header.
    size_t ScriptHandlerCount();
}
