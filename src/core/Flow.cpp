#include "core/Flow.hpp"

#include "core/ObjectUtil.hpp"

#include <unordered_map>

namespace qa::flow
{
    namespace
    {
        // The variables found so far, kept so that a line read again does not walk the whole
        // object array. A pointer is used only while it is still live.
        std::unordered_map<std::wstring, UObject*> g_found;
    }

    bool Value(UObject* variable, bool flag, bool& out)
    {
        UObject* library = obj::FindObject(L"/Script/SMGGameFlow.Default__GFBlackboardBlueprintLibrary");
        auto* fn = library ? obj::FindFunction(library, flag ? L"GetGlobalBlackboardFlag" : L"GetGlobalBlackboardBool") : nullptr;
        UObject* context = obj::LocalPlayerController();
        if (!fn || !context || !obj::IsLive(variable)) return false;
        bool ok = false;
        obj::Call(
            library, fn,
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (!prop) continue;
                    const auto name = prop->GetName();
                    if (name == L"WorldContextObject")
                        *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = context;
                    else if (name == L"VariableRef")
                    {
                        auto* member = obj::StructMember(prop, L"Variable");
                        void* ref = obj::ValuePtrAt(params, prop);
                        if (member && ref) *static_cast<UObject**>(obj::ValuePtrAt(ref, member)) = variable;
                    }
                }
            },
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (prop && prop->GetName() == L"ReturnValue") ok = obj::ReadBoolAt(params, prop, out);
                }
            });
        return ok;
    }

    UObject* Find(std::wstring_view name, bool flag)
    {
        const std::wstring key(name);
        if (auto it = g_found.find(key); it != g_found.end())
        {
            if (obj::IsLive(it->second)) return it->second;
            g_found.erase(it);
        }
        for (UObject* variable : obj::FindAllLive(flag ? L"GFBlackboardVariableFlag" : L"GFBlackboardVariableBool"))
        {
            if (obj::ObjectName(variable) != name) continue;
            g_found[key] = variable;
            return variable;
        }
        return nullptr;
    }

    bool Bool(std::wstring_view name, bool& out)
    {
        UObject* variable = Find(name, false);
        return variable && Value(variable, false, out);
    }
}
