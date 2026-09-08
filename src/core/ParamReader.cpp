#include "core/ParamReader.hpp"

#include "core/Log.hpp"

#include <mutex>
#include <unordered_map>

namespace qa::params
{
    namespace
    {
        std::mutex g_mutex;
        std::unordered_map<UFunction*, std::unordered_map<std::wstring, FProperty*>> g_cache;
    }

    FProperty* Find(UFunction* function, std::wstring_view name)
    {
        if (!function) return nullptr;
        {
            std::lock_guard lock(g_mutex);
            const auto f = g_cache.find(function);
            if (f != g_cache.end())
            {
                const auto p = f->second.find(std::wstring(name));
                if (p != f->second.end()) return p->second;
            }
        }
        FProperty* found = nullptr;
        for (auto* prop : function->ForEachProperty())
        {
            if (!prop) continue;
            if (prop->GetName() == name)
            {
                found = prop;
                break;
            }
        }
        std::lock_guard lock(g_mutex);
        g_cache[function][std::wstring(name)] = found;
        return found;
    }

    bool Bool(FFrame& stack, std::wstring_view name, bool& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadBoolAt(stack.Locals(), prop, out);
    }

    bool Int(FFrame& stack, std::wstring_view name, int64_t& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadIntAt(stack.Locals(), prop, out);
    }

    bool Float(FFrame& stack, std::wstring_view name, double& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadFloatAt(stack.Locals(), prop, out);
    }

    bool String(FFrame& stack, std::wstring_view name, std::wstring& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadStringAt(stack.Locals(), prop, out);
    }

    bool Object(FFrame& stack, std::wstring_view name, UObject*& out)
    {
        auto* prop = Find(stack.Node(), name);
        return prop && stack.Locals() && obj::ReadObjectAt(stack.Locals(), prop, out);
    }

    void* StructPtr(FFrame& stack, std::wstring_view name)
    {
        auto* prop = Find(stack.Node(), name);
        if (!prop || !stack.Locals()) return nullptr;
        return prop->ContainerPtrToValuePtr<void>(static_cast<void*>(stack.Locals()));
    }

    bool FirstOfType(FFrame& stack, std::wstring_view typeName, std::wstring& out)
    {
        auto* function = stack.Node();
        if (!function || !stack.Locals()) return false;
        for (auto* prop : function->ForEachProperty())
        {
            if (!prop) continue;
            if (prop->HasAnyPropertyFlags(static_cast<uint64_t>(RC::Unreal::EPropertyFlags::CPF_ReturnParm))) continue;
            if (obj::PropertyTypeName(prop) != typeName) continue;
            return obj::ReadStringAt(stack.Locals(), prop, out);
        }
        return false;
    }

    std::wstring Describe(FFrame& stack)
    {
        auto* function = stack.Node();
        if (!function || !stack.Locals()) return {};
        std::wstring out;
        int count = 0;
        for (auto* prop : function->ForEachProperty())
        {
            if (!prop) continue;
            if (++count > 12) break;
            if (!out.empty()) out += L", ";
            out += prop->GetName();
            out += L"=";
            out += obj::ValueToString(prop, prop->ContainerPtrToValuePtr<void>(static_cast<void*>(stack.Locals())), 0);
        }
        return out;
    }
}
