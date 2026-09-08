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

    bool ReadBool(UObject* object, std::wstring_view name, bool& out);
    bool ReadInt(UObject* object, std::wstring_view name, int64_t& out); // byte/int/int64/enum
    bool ReadFloat(UObject* object, std::wstring_view name, double& out);
    bool ReadString(UObject* object, std::wstring_view name, std::wstring& out); // FString/FText/FName
    bool ReadObject(UObject* object, std::wstring_view name, UObject*& out);
    bool ReadObjectArray(UObject* object, std::wstring_view name, std::vector<UObject*>& out);
    bool ReadLocaleKey(UObject* object, std::wstring_view name, std::wstring& outKey); // FLocaleString{Key}
    void* StructPtr(UObject* object, std::wstring_view name);                          // raw pointer to a struct property

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

    // Widgets
    bool IsWidgetVisible(UObject* widget);    // Visibility not Collapsed/Hidden (self only)
    bool IsWidgetShown(UObject* widget);      // self and all outer user widgets visible
    std::wstring TextOf(UObject* textWidget); // "Text" FText/FString of a text widget, else empty
    UObject* WidgetTreeRoot(UObject* userWidget);
    std::vector<UObject*> PanelChildren(UObject* panelWidget);
    // Visible text of all descendant text widgets, in tree order. Recurses into nested user widgets.
    std::vector<std::wstring> DescendantTexts(UObject* userWidget, int maxDepth = 6);
    // Visits every widget in the tree (depth-first). Return false from the visitor to stop.
    void WalkWidgetTree(UObject* userWidget, int maxDepth, const std::function<bool(UObject* widget, int depth)>& visitor);

    // Well-known objects (cached, validated)
    UObject* LocalPlayerController();
    UObject* GameInstance();
    void ResetCaches();
}
