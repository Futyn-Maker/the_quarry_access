#pragma once
// Reflection helpers over UE4SS's UObject API. Everything is name-based so the
// mod survives Blueprint/native layout changes between game patches.
// All functions must be called on the game thread.

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace qa::obj
{
    using RC::Unreal::FProperty;
    using RC::Unreal::UClass;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;

    // Liveness / identity
    bool IsLive(UObject* object); // non-null, valid in GUObjectArray, not unreachable
    bool IsCDO(UObject* object);
    std::wstring ClassName(UObject* object); // short class name, e.g. "MenuButton_C"
    std::wstring ClassName(UClass* cls);
    std::wstring ObjectName(UObject* object);               // instance name, e.g. "NewGame"
    std::wstring FullName(UObject* object);                 // "Class /Path.Object"
    bool IsA(UObject* object, std::wstring_view className); // walks the super chain by short name
    bool ClassIsChildOf(UClass* cls, std::wstring_view className);
    UObject* Outer(UObject* object);
    UObject* FindOuterOfClass(UObject* object, std::wstring_view className, int maxDepth = 12);
    UObject* FindOuterUserWidget(UObject* widget, int maxDepth = 12); // nearest outer that is a UserWidget

    // Properties (by name, in the inheritance chain)
    FProperty* FindProperty(UObject* object, std::wstring_view name);
    FProperty* FindProperty(UClass* cls, std::wstring_view name);
    std::wstring PropertyTypeName(FProperty* property); // "ObjectProperty", "TextProperty", ...
    void* ValuePtr(UObject* object, FProperty* property);
    // The same inside raw memory: a struct, a parameter block.
    void* ValuePtrAt(void* container, FProperty* property);

    bool ReadBool(UObject* object, std::wstring_view name, bool& out);
    bool ReadInt(UObject* object, std::wstring_view name, int64_t& out); // byte/int/int64/enum
    bool ReadFloat(UObject* object, std::wstring_view name, double& out);
    bool ReadString(UObject* object, std::wstring_view name, std::wstring& out); // FString/FText/FName
    bool ReadObject(UObject* object, std::wstring_view name, UObject*& out);
    bool ReadObjectArray(UObject* object, std::wstring_view name, std::vector<UObject*>& out);
    bool ReadLocaleKey(UObject* object, std::wstring_view name, std::wstring& outKey); // FLocaleString{Key}
    void* StructPtr(UObject* object, std::wstring_view name);                          // raw pointer to a struct property
    // A member of a struct-typed property, for reading nested values with the *At readers
    // (container = ValuePtr of the struct property). Null when there is no such member.
    FProperty* StructMember(FProperty* structProperty, std::wstring_view name);
    // Visits every element of an array property held in raw memory (an object or a struct):
    // the element's address, to be read with the inner property as the container.
    void ForEachArrayElement(void* container, FProperty* arrayProperty, const std::function<void(void* element, FProperty* inner)>& visit);

    // Same readers on raw struct memory (container = struct address).
    bool ReadBoolAt(void* container, FProperty* property, bool& out);
    bool ReadIntAt(void* container, FProperty* property, int64_t& out);
    bool ReadFloatAt(void* container, FProperty* property, double& out);
    bool ReadStringAt(void* container, FProperty* property, std::wstring& out);
    bool ReadObjectAt(void* container, FProperty* property, UObject*& out);
    // Diagnostic formatting of any property value ("true", "12", "text", "Class Name", "<struct>").
    std::wstring ValueToString(FProperty* property, void* valuePtr, int depth = 0);

    // Functions
    UFunction* FindFunction(UObject* object, std::wstring_view name);
    UFunction* FindFunctionByFullName(std::wstring_view fullName); // "/Script/Pkg.Class:Func"
    UObject* FindObject(std::wstring_view fullPath);               // StaticFindObject by path
    UObject* FindFirstLive(std::wstring_view className);           // first non-CDO instance
    std::vector<UObject*> FindAllLive(std::wstring_view className);
    // Allocates the parameter block, lets `fill` write inputs, calls ProcessEvent, lets `read` read outputs.
    bool Call(UObject* target, UFunction* function, const std::function<void(void* params)>& fill, const std::function<void(void* params)>& read);
    bool CallNoArgs(UObject* target, std::wstring_view functionName);
    // Result of a no-argument function returning FText/FString/FName. Empty when there is
    // no such function. Reaches text the game computes on demand instead of storing it.
    std::wstring CallForText(UObject* target, std::wstring_view functionName);
    // Result of a no-argument function returning bool; false when there is no such function.
    bool CallForBool(UObject* target, std::wstring_view functionName);
    // Calls a no-argument function and hands `read` the parameter block and the property of
    // the return value (null when the function returns nothing), then releases what the
    // engine allocated in a returned struct. False when there is no such function.
    bool CallReturn(UObject* target, std::wstring_view functionName, const std::function<void(void* params, FProperty* returnValue)>& read);

    // Widgets
    bool IsWidgetVisible(UObject* widget); // Visibility not Collapsed/Hidden (self only)
    // Self and all outer user widgets visible and on the active page of any switcher above
    // them; with `opaque`, none of them faded out either.
    bool IsWidgetShown(UObject* widget, bool opaque = false);
    // The opacity a widget is drawn with (its render opacity times its colour alpha).
    double WidgetOpacity(UObject* widget);
    // What UMG's property binding computes for a bound property. A binding leaves the
    // property itself at its design-time value and feeds the widget from a companion
    // delegate generated beside it ("Text" -> "TextDelegate"), which is what is drawn.
    std::wstring BoundText(UObject* widget, std::wstring_view property);
    std::wstring TextOf(UObject* textWidget); // "Text" FText/FString of a text widget, else empty
    UObject* WidgetTreeRoot(UObject* userWidget);
    std::vector<UObject*> PanelChildren(UObject* panelWidget);
    // Visible text of all descendant text widgets, in tree order. Recurses into nested user widgets.
    std::vector<std::wstring> DescendantTexts(UObject* userWidget, int maxDepth = 6);
    // Lets the game-specific layer declare widgets whose text is held in one property
    // rather than in their child text blocks (which may repeat it for visual effects).
    // The reader returns true to claim a widget: its text (possibly empty) is taken as
    // is and its subtree is not walked.
    using TextLeafReader = bool (*)(UObject* widget, std::wstring& outText);
    void SetTextLeafReader(TextLeafReader reader);
    // Visits every widget in the tree (depth-first). Return false from the visitor to stop.
    void WalkWidgetTree(UObject* userWidget, int maxDepth, const std::function<bool(UObject* widget, int depth)>& visitor);

    // The widget one step up in the live hierarchy: the slot parent, or the owning user
    // widget when `widget` is the root of a widget tree. Null at the top.
    UObject* ParentWidget(UObject* widget);

    // Nearest ancestor of the given class, following ParentWidget. Includes `widget` itself.
    UObject* NearestAncestorOfClass(UObject* widget, std::wstring_view className, int maxDepth = 64);

    // Outermost screen widget above `widget`: the whole screen, so that a control inside a
    // section still reports the screen that owns the title and the prompt bar.
    UObject* RootScreen(UObject* widget);

    // True when `widget` is `ancestor` or sits below it in the live widget hierarchy
    // (slot parents, hopping from a widget-tree root to its owning user widget).
    bool IsDescendantOf(UObject* widget, UObject* ancestor);

    // Runs `fn(context)` guarded against memory faults from stale game pointers, so a
    // widget that dies mid-walk costs one skipped frame instead of the process.
    // `fn` must be a captureless function; keep C++ objects inside it, not around it.
    bool SafeInvoke(void (*fn)(void*), void* context) noexcept;
    // The same, with a fault written to the log (at most once in five seconds per name).
    bool SafeInvokeLogged(const wchar_t* name, void (*fn)(void*), void* context);

    // Well-known objects (cached, validated)
    UObject* LocalPlayerController();
    UObject* GameInstance();
    void ResetCaches();
}
