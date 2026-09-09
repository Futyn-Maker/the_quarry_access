#include "core/ObjectUtil.hpp"

#include "core/Log.hpp"
#include "core/Strings.hpp"

#include <Unreal/Core/Containers/Array.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UnrealFlags.hpp>

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace qa::obj
{
    using RC::Unreal::FBoolProperty;
    using RC::Unreal::FName;
    using RC::Unreal::FString;
    using RC::Unreal::FText;
    using RC::Unreal::TArray;
    using RC::Unreal::UObjectArray;
    using RC::Unreal::UStruct;
    namespace UObjectGlobals = RC::Unreal::UObjectGlobals;

    namespace
    {
        std::mutex g_mutex;
        std::unordered_map<UClass*, std::wstring> g_classNames;
        std::unordered_map<UClass*, std::unordered_map<std::wstring, FProperty*>> g_propertyCache;
        std::unordered_map<UClass*, std::unordered_map<std::wstring, UFunction*>> g_functionCache;
        UObject* g_playerController = nullptr;
        UObject* g_gameInstance = nullptr;

        std::wstring FStringToWide(const FString* s)
        {
            if (!s || s->Len() <= 0) return {};
            const auto* chars = **s;
            return chars ? std::wstring(chars) : std::wstring();
        }

        std::wstring FTextToWide(const FText* t)
        {
            if (!t || !t->GetTextData()) return {};
            return t->ToString();
        }

        constexpr int kVisCollapsed = 1;
        constexpr int kVisHidden = 2;
    }

    // ---- identity ----------------------------------------------------------

    bool IsLive(UObject* object)
    {
        if (!object) return false;
        __try
        {
            auto* item = object->GetObjectItem();
            if (!item) return false;
            if (!UObjectArray::IsValid(item, false)) return false;
            return object->GetClassPrivate() != nullptr;
        }
        __except (1)
        {
            return false;
        }
    }

    bool IsCDO(UObject* object)
    {
        if (!object) return true;
        return object->HasAnyFlags(static_cast<RC::Unreal::EObjectFlags>(RC::Unreal::RF_ClassDefaultObject | RC::Unreal::RF_ArchetypeObject));
    }

    std::wstring ClassName(UClass* cls)
    {
        if (!cls) return L"<null>";
        {
            std::lock_guard lock(g_mutex);
            const auto it = g_classNames.find(cls);
            if (it != g_classNames.end()) return it->second;
        }
        std::wstring name = cls->GetName();
        std::lock_guard lock(g_mutex);
        g_classNames[cls] = name;
        return name;
    }

    std::wstring ClassName(UObject* object)
    {
        if (!object) return L"<null>";
        return ClassName(object->GetClassPrivate());
    }

    std::wstring ObjectName(UObject* object)
    {
        return object ? object->GetName() : std::wstring(L"<null>");
    }

    std::wstring FullName(UObject* object)
    {
        return object ? object->GetFullName() : std::wstring(L"<null>");
    }

    bool ClassIsChildOf(UClass* cls, std::wstring_view className)
    {
        UStruct* s = cls;
        int guard = 0;
        while (s && guard++ < 64)
        {
            if (ClassName(static_cast<UClass*>(s)) == className) return true;
            s = s->GetSuperStruct();
        }
        return false;
    }

    bool IsA(UObject* object, std::wstring_view className)
    {
        return object && ClassIsChildOf(object->GetClassPrivate(), className);
    }

    UObject* Outer(UObject* object)
    {
        return object ? object->GetOuterPrivate() : nullptr;
    }

    UObject* FindOuterOfClass(UObject* object, std::wstring_view className, int maxDepth)
    {
        UObject* cur = object;
        for (int d = 0; cur && d < maxDepth; ++d)
        {
            if (IsA(cur, className)) return cur;
            cur = cur->GetOuterPrivate();
        }
        return nullptr;
    }

    UObject* FindOuterUserWidget(UObject* widget, int maxDepth)
    {
        UObject* cur = widget ? widget->GetOuterPrivate() : nullptr;
        for (int d = 0; cur && d < maxDepth; ++d)
        {
            if (IsA(cur, L"UserWidget")) return cur;
            cur = cur->GetOuterPrivate();
        }
        return nullptr;
    }

    // ---- properties --------------------------------------------------------

    FProperty* FindProperty(UClass* cls, std::wstring_view name)
    {
        if (!cls) return nullptr;
        const std::wstring key(name);
        {
            std::lock_guard lock(g_mutex);
            const auto c = g_propertyCache.find(cls);
            if (c != g_propertyCache.end())
            {
                const auto p = c->second.find(key);
                if (p != c->second.end()) return p->second;
            }
        }
        FProperty* found = nullptr;
        for (auto* prop : cls->ForEachPropertyInChain())
        {
            if (!prop) continue;
            if (prop->GetName() == name)
            {
                found = prop;
                break;
            }
        }
        std::lock_guard lock(g_mutex);
        g_propertyCache[cls][key] = found;
        return found;
    }

    FProperty* FindProperty(UObject* object, std::wstring_view name)
    {
        return object ? FindProperty(object->GetClassPrivate(), name) : nullptr;
    }

    std::wstring PropertyTypeName(FProperty* property)
    {
        return property ? property->GetClass().GetName() : std::wstring();
    }

    void* ValuePtr(UObject* object, FProperty* property)
    {
        if (!object || !property) return nullptr;
        return property->ContainerPtrToValuePtr<void>(object);
    }

    bool ReadBoolAt(void* container, FProperty* property, bool& out)
    {
        if (!container || !property) return false;
        const auto type = PropertyTypeName(property);
        void* ptr = property->ContainerPtrToValuePtr<void>(container);
        if (type == L"BoolProperty")
        {
            out = static_cast<FBoolProperty*>(property)->GetPropertyValue(ptr);
            return true;
        }
        int64_t i = 0;
        if (ReadIntAt(container, property, i))
        {
            out = i != 0;
            return true;
        }
        return false;
    }

    bool ReadIntAt(void* container, FProperty* property, int64_t& out)
    {
        if (!container || !property) return false;
        const auto type = PropertyTypeName(property);
        void* ptr = property->ContainerPtrToValuePtr<void>(container);
        if (type == L"ByteProperty")
        {
            out = *static_cast<uint8_t*>(ptr);
            return true;
        }
        if (type == L"Int8Property")
        {
            out = *static_cast<int8_t*>(ptr);
            return true;
        }
        if (type == L"Int16Property")
        {
            out = *static_cast<int16_t*>(ptr);
            return true;
        }
        if (type == L"IntProperty")
        {
            out = *static_cast<int32_t*>(ptr);
            return true;
        }
        if (type == L"Int64Property")
        {
            out = *static_cast<int64_t*>(ptr);
            return true;
        }
        if (type == L"UInt16Property")
        {
            out = *static_cast<uint16_t*>(ptr);
            return true;
        }
        if (type == L"UInt32Property")
        {
            out = *static_cast<uint32_t*>(ptr);
            return true;
        }
        if (type == L"UInt64Property")
        {
            out = static_cast<int64_t>(*static_cast<uint64_t*>(ptr));
            return true;
        }
        if (type == L"EnumProperty")
        {
            switch (property->GetElementSize())
            {
            case 1: out = *static_cast<uint8_t*>(ptr); return true;
            case 2: out = *static_cast<int16_t*>(ptr); return true;
            case 4: out = *static_cast<int32_t*>(ptr); return true;
            case 8: out = *static_cast<int64_t*>(ptr); return true;
            default: return false;
            }
        }
        if (type == L"BoolProperty")
        {
            out = static_cast<FBoolProperty*>(property)->GetPropertyValue(ptr) ? 1 : 0;
            return true;
        }
        return false;
    }

    bool ReadFloatAt(void* container, FProperty* property, double& out)
    {
        if (!container || !property) return false;
        const auto type = PropertyTypeName(property);
        void* ptr = property->ContainerPtrToValuePtr<void>(container);
        if (type == L"FloatProperty")
        {
            out = *static_cast<float*>(ptr);
            return true;
        }
        if (type == L"DoubleProperty")
        {
            out = *static_cast<double*>(ptr);
            return true;
        }
        int64_t i = 0;
        if (ReadIntAt(container, property, i))
        {
            out = static_cast<double>(i);
            return true;
        }
        return false;
    }

    bool ReadStringAt(void* container, FProperty* property, std::wstring& out)
    {
        if (!container || !property) return false;
        const auto type = PropertyTypeName(property);
        void* ptr = property->ContainerPtrToValuePtr<void>(container);
        if (type == L"StrProperty")
        {
            out = FStringToWide(static_cast<FString*>(ptr));
            return true;
        }
        if (type == L"TextProperty")
        {
            out = FTextToWide(static_cast<FText*>(ptr));
            return true;
        }
        if (type == L"NameProperty")
        {
            out = static_cast<FName*>(ptr)->ToString();
            if (out == L"None") out.clear();
            return true;
        }
        return false;
    }

    bool ReadObjectAt(void* container, FProperty* property, UObject*& out)
    {
        if (!container || !property) return false;
        const auto type = PropertyTypeName(property);
        if (type == L"WeakObjectProperty")
        {
            out = property->ContainerPtrToValuePtr<RC::Unreal::FWeakObjectPtr>(container)->Get();
            return true;
        }
        if (type != L"ObjectProperty" && type != L"ClassProperty") return false;
        out = *property->ContainerPtrToValuePtr<UObject*>(container);
        return true;
    }

    bool ReadBool(UObject* object, std::wstring_view name, bool& out)
    {
        return ReadBoolAt(object, FindProperty(object, name), out);
    }

    bool ReadInt(UObject* object, std::wstring_view name, int64_t& out)
    {
        return ReadIntAt(object, FindProperty(object, name), out);
    }

    bool ReadFloat(UObject* object, std::wstring_view name, double& out)
    {
        return ReadFloatAt(object, FindProperty(object, name), out);
    }

    bool ReadString(UObject* object, std::wstring_view name, std::wstring& out)
    {
        return ReadStringAt(object, FindProperty(object, name), out);
    }

    bool ReadObject(UObject* object, std::wstring_view name, UObject*& out)
    {
        return ReadObjectAt(object, FindProperty(object, name), out);
    }

    bool ReadObjectArray(UObject* object, std::wstring_view name, std::vector<UObject*>& out)
    {
        auto* prop = FindProperty(object, name);
        if (!prop || PropertyTypeName(prop) != L"ArrayProperty") return false;
        auto* arr = prop->ContainerPtrToValuePtr<TArray<UObject*>>(object);
        out.clear();
        if (!arr || arr->Num() <= 0 || !arr->GetData()) return true;
        const int32_t n = arr->Num();
        if (n > 100000) return false;
        out.reserve(static_cast<size_t>(n));
        for (int32_t i = 0; i < n; ++i)
            out.push_back(arr->GetData()[i]);
        return true;
    }

    bool ReadLocaleKey(UObject* object, std::wstring_view name, std::wstring& outKey)
    {
        auto* prop = FindProperty(object, name);
        if (!prop || PropertyTypeName(prop) != L"StructProperty") return false;
        auto* key = prop->ContainerPtrToValuePtr<FString>(object);
        outKey = FStringToWide(key);
        return true;
    }

    void* StructPtr(UObject* object, std::wstring_view name)
    {
        auto* prop = FindProperty(object, name);
        if (!prop || PropertyTypeName(prop) != L"StructProperty") return nullptr;
        return prop->ContainerPtrToValuePtr<void>(object);
    }

    std::wstring ValueToString(FProperty* property, void* valuePtr, int depth)
    {
        if (!property || !valuePtr) return L"<null>";
        const auto type = PropertyTypeName(property);
        if (type == L"BoolProperty") return static_cast<FBoolProperty*>(property)->GetPropertyValue(valuePtr) ? L"true" : L"false";
        if (type == L"StrProperty") return L"\"" + FStringToWide(static_cast<FString*>(valuePtr)) + L"\"";
        if (type == L"TextProperty") return L"\"" + FTextToWide(static_cast<FText*>(valuePtr)) + L"\"";
        if (type == L"NameProperty") return static_cast<FName*>(valuePtr)->ToString();
        if (type == L"FloatProperty") return std::format(L"{:.3f}", *static_cast<float*>(valuePtr));
        if (type == L"DoubleProperty") return std::format(L"{:.3f}", *static_cast<double*>(valuePtr));
        if (type == L"ObjectProperty" || type == L"ClassProperty")
        {
            auto* o = *static_cast<UObject**>(valuePtr);
            if (!o) return L"null";
            return ClassName(o) + L" " + ObjectName(o);
        }
        if (type == L"ArrayProperty")
        {
            auto* arr = static_cast<TArray<uint8_t>*>(valuePtr);
            return std::format(L"[{} items]", arr ? arr->Num() : 0);
        }
        if (type == L"StructProperty")
        {
            (void)depth;
            return std::format(L"<struct {} bytes>", property->GetSize());
        }
        // numeric / enum
        uint8_t tmp[8]{};
        (void)tmp;
        switch (property->GetElementSize())
        {
        case 1: return std::to_wstring(*static_cast<uint8_t*>(valuePtr));
        case 2: return std::to_wstring(*static_cast<int16_t*>(valuePtr));
        case 4: return std::to_wstring(*static_cast<int32_t*>(valuePtr));
        case 8: return std::to_wstring(*static_cast<int64_t*>(valuePtr));
        default: return L"<" + type + L">";
        }
    }

    // ---- functions ---------------------------------------------------------

    UFunction* FindFunction(UObject* object, std::wstring_view name)
    {
        if (!object) return nullptr;
        UClass* cls = object->GetClassPrivate();
        if (!cls) return nullptr;
        const std::wstring key(name);
        {
            std::lock_guard lock(g_mutex);
            const auto c = g_functionCache.find(cls);
            if (c != g_functionCache.end())
            {
                const auto f = c->second.find(key);
                if (f != c->second.end()) return f->second;
            }
        }
        UFunction* found = nullptr;
        for (auto* fn : cls->ForEachFunctionInChain())
        {
            if (!fn) continue;
            if (fn->GetName() == name)
            {
                found = fn;
                break;
            }
        }
        std::lock_guard lock(g_mutex);
        g_functionCache[cls][key] = found;
        return found;
    }

    UFunction* FindFunctionByFullName(std::wstring_view fullName)
    {
        return UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, RC::StringType(fullName));
    }

    UObject* FindObject(std::wstring_view fullPath)
    {
        return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, RC::StringType(fullPath));
    }

    std::vector<UObject*> FindAllLive(std::wstring_view className)
    {
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(RC::StringType(className), all);
        std::vector<UObject*> out;
        for (auto* o : all)
        {
            if (o && !IsCDO(o) && IsLive(o)) out.push_back(o);
        }
        return out;
    }

    UObject* FindFirstLive(std::wstring_view className)
    {
        const auto all = FindAllLive(className);
        return all.empty() ? nullptr : all.front();
    }

    bool Call(UObject* target, UFunction* function, const std::function<void(void* params)>& fill, const std::function<void(void* params)>& read)
    {
        if (!target || !function) return false;
        const int32_t size = function->GetPropertiesSize();
        if (size < 0 || size > 4096) return false;
        std::vector<uint8_t> buffer(static_cast<size_t>(size) + 32, 0);
        if (fill) fill(buffer.data());
        target->ProcessEvent(function, buffer.data());
        if (read) read(buffer.data());
        // Release engine-allocated string/text/object-array parameters.
        for (auto* prop : function->ForEachProperty())
        {
            if (!prop) continue;
            const auto type = PropertyTypeName(prop);
            void* ptr = prop->ContainerPtrToValuePtr<void>(static_cast<void*>(buffer.data()));
            if (type == L"StrProperty")
                std::destroy_at(static_cast<FString*>(ptr));
            else if (type == L"TextProperty")
                std::destroy_at(static_cast<FText*>(ptr));
        }
        return true;
    }

    bool CallNoArgs(UObject* target, std::wstring_view functionName)
    {
        auto* fn = FindFunction(target, functionName);
        if (!fn) return false;
        return Call(target, fn, nullptr, nullptr);
    }

    std::wstring CallForText(UObject* target, std::wstring_view functionName)
    {
        auto* fn = FindFunction(target, functionName);
        if (!fn) return {};
        std::wstring result;
        Call(target, fn, nullptr,
             [&](void* params)
             {
                 for (auto* prop : fn->ForEachProperty())
                 {
                     if (prop && prop->GetName() == L"ReturnValue") ReadStringAt(params, prop, result);
                 }
             });
        return result;
    }

    bool CallForBool(UObject* target, std::wstring_view functionName)
    {
        auto* fn = FindFunction(target, functionName);
        if (!fn) return false;
        bool result = false;
        Call(target, fn, nullptr,
             [&](void* params)
             {
                 for (auto* prop : fn->ForEachProperty())
                 {
                     if (prop && prop->GetName() == L"ReturnValue") ReadBoolAt(params, prop, result);
                 }
             });
        return result;
    }

    // ---- widgets -----------------------------------------------------------

    bool IsWidgetVisible(UObject* widget)
    {
        if (!widget) return false;
        int64_t vis = 0;
        if (!ReadInt(widget, L"Visibility", vis)) return true;
        return vis != kVisCollapsed && vis != kVisHidden;
    }

    bool IsWidgetShown(UObject* widget)
    {
        UObject* cur = widget;
        for (int depth = 0; cur && depth < 48; ++depth)
        {
            if (!IsLive(cur)) return false;
            if (!IsWidgetVisible(cur)) return false;
            UObject* slot = nullptr;
            UObject* parent = nullptr;
            if (ReadObject(cur, L"Slot", slot) && slot && IsLive(slot) && ReadObject(slot, L"Parent", parent) && parent)
            {
                cur = parent;
                continue;
            }
            // Root of a widget tree: continue with the owning user widget.
            UObject* outer = cur->GetOuterPrivate();
            if (outer && IsA(outer, L"WidgetTree"))
            {
                UObject* owner = outer->GetOuterPrivate();
                if (owner && IsA(owner, L"UserWidget"))
                {
                    cur = owner;
                    continue;
                }
            }
            break;
        }
        return true;
    }

    std::wstring TextOf(UObject* textWidget)
    {
        if (!textWidget) return {};
        std::wstring text;
        if (ReadString(textWidget, L"Text", text)) return text;
        return {};
    }

    UObject* WidgetTreeRoot(UObject* userWidget)
    {
        UObject* tree = nullptr;
        if (!ReadObject(userWidget, L"WidgetTree", tree) || !tree) return nullptr;
        UObject* root = nullptr;
        if (!ReadObject(tree, L"RootWidget", root)) return nullptr;
        return root;
    }

    std::vector<UObject*> PanelChildren(UObject* panelWidget)
    {
        std::vector<UObject*> children;
        std::vector<UObject*> slots;
        if (!ReadObjectArray(panelWidget, L"Slots", slots)) return children;
        for (auto* slot : slots)
        {
            UObject* content = nullptr;
            if (slot && IsLive(slot) && ReadObject(slot, L"Content", content) && content && IsLive(content)) children.push_back(content);
        }
        return children;
    }

    UObject* ParentWidget(UObject* widget)
    {
        if (!widget || !IsLive(widget)) return nullptr;
        UObject* slot = nullptr;
        UObject* parent = nullptr;
        if (ReadObject(widget, L"Slot", slot) && slot && IsLive(slot) && ReadObject(slot, L"Parent", parent) && parent && IsLive(parent))
        {
            return parent;
        }
        UObject* outer = widget->GetOuterPrivate();
        if (outer && IsLive(outer) && IsA(outer, L"WidgetTree"))
        {
            UObject* owner = outer->GetOuterPrivate();
            if (owner && IsLive(owner) && IsA(owner, L"UserWidget")) return owner;
        }
        return nullptr;
    }

    UObject* NearestAncestorOfClass(UObject* widget, std::wstring_view className, int maxDepth)
    {
        UObject* cur = widget;
        for (int depth = 0; cur && depth < maxDepth; ++depth)
        {
            if (IsA(cur, className)) return cur;
            cur = ParentWidget(cur);
        }
        return nullptr;
    }

    UObject* RootScreen(UObject* widget)
    {
        UObject* outermost = nullptr;
        UObject* cur = widget;
        for (int depth = 0; cur && depth < 64; ++depth)
        {
            if (IsA(cur, L"SMGUIWidget")) outermost = cur;
            cur = ParentWidget(cur);
        }
        return outermost;
    }

    bool IsDescendantOf(UObject* widget, UObject* ancestor)
    {
        if (!widget || !ancestor) return false;
        UObject* cur = widget;
        for (int depth = 0; cur && depth < 64; ++depth)
        {
            if (cur == ancestor) return true;
            cur = ParentWidget(cur);
        }
        return false;
    }

    bool SafeInvoke(void (*fn)(void*), void* context) noexcept
    {
        __try
        {
            fn(context);
            return true;
        }
        __except (1)
        {
            return false;
        }
    }

    namespace
    {
        // Every child widget of one widget: panel slot contents, and a single "Content"
        // widget for NamedSlot / Border / content widgets that a panel walk would miss
        // (the menu prompt bar lives in a NamedSlot, for instance).
        std::vector<UObject*> ChildWidgets(UObject* widget)
        {
            std::vector<UObject*> children;
            if (IsA(widget, L"PanelWidget"))
            {
                children = PanelChildren(widget);
            }
            else
            {
                UObject* content = nullptr;
                if (FindProperty(widget, L"Content") && ReadObject(widget, L"Content", content) && content && IsLive(content) && IsA(content, L"Widget"))
                {
                    children.push_back(content);
                }
            }
            return children;
        }
    }

    namespace
    {
        bool WalkImpl(UObject* widget, int depth, int maxDepth, const std::function<bool(UObject*, int)>& visitor)
        {
            if (!widget || !IsLive(widget)) return true;
            if (!visitor(widget, depth)) return false;
            if (depth >= maxDepth) return true;
            if (IsA(widget, L"UserWidget"))
            {
                if (!WalkImpl(WidgetTreeRoot(widget), depth + 1, maxDepth, visitor)) return false;
            }
            for (auto* child : ChildWidgets(widget))
            {
                if (!WalkImpl(child, depth + 1, maxDepth, visitor)) return false;
            }
            return true;
        }

        TextLeafReader g_textLeaf = nullptr;

        void CollectTexts(UObject* widget, int depth, int maxDepth, std::vector<std::wstring>& out)
        {
            if (!widget || !IsLive(widget) || depth > maxDepth) return;
            if (!IsWidgetVisible(widget)) return;
            if (g_textLeaf)
            {
                std::wstring text;
                if (g_textLeaf(widget, text))
                {
                    if (!text.empty()) out.push_back(text);
                    return;
                }
            }
            if (IsA(widget, L"TextBlock") || IsA(widget, L"RichTextBlock") || IsA(widget, L"EditableTextBox") || IsA(widget, L"EditableText") ||
                IsA(widget, L"MultiLineEditableText"))
            {
                const auto text = str::StripMarkup(TextOf(widget));
                if (!text.empty()) out.push_back(text);
            }
            if (IsA(widget, L"UserWidget"))
            {
                CollectTexts(WidgetTreeRoot(widget), depth + 1, maxDepth, out);
            }
            for (auto* child : ChildWidgets(widget))
                CollectTexts(child, depth + 1, maxDepth, out);
        }
    }

    void WalkWidgetTree(UObject* userWidget, int maxDepth, const std::function<bool(UObject* widget, int depth)>& visitor)
    {
        WalkImpl(userWidget, 0, maxDepth, visitor);
    }

    std::vector<std::wstring> DescendantTexts(UObject* userWidget, int maxDepth)
    {
        std::vector<std::wstring> out;
        CollectTexts(userWidget, 0, maxDepth, out);
        return out;
    }

    void SetTextLeafReader(TextLeafReader reader)
    {
        g_textLeaf = reader;
    }

    // ---- well-known objects ------------------------------------------------

    UObject* LocalPlayerController()
    {
        if (g_playerController && IsLive(g_playerController)) return g_playerController;
        g_playerController = nullptr;
        for (auto* pc : FindAllLive(L"PlayerController"))
        {
            UObject* player = nullptr;
            if (ReadObject(pc, L"Player", player) && player)
            {
                g_playerController = pc;
                break;
            }
        }
        if (g_playerController) log::Verbose(L"player controller: {}", FullName(g_playerController));
        return g_playerController;
    }

    UObject* GameInstance()
    {
        if (g_gameInstance && IsLive(g_gameInstance)) return g_gameInstance;
        g_gameInstance = FindFirstLive(L"GameInstance");
        return g_gameInstance;
    }

    void ResetCaches()
    {
        std::lock_guard lock(g_mutex);
        g_playerController = nullptr;
        g_gameInstance = nullptr;
        g_propertyCache.clear();
        g_functionCache.clear();
    }
}
