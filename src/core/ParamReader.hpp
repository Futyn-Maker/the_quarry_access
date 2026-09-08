#pragma once
// Reads parameters of a hooked UFunction by name from the FFrame locals block.

#include "core/ObjectUtil.hpp"

#include <Unreal/FFrame.hpp>

namespace qa::params
{
    using RC::Unreal::FFrame;
    using RC::Unreal::FProperty;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;

    FProperty* Find(UFunction* function, std::wstring_view name);
    bool Bool(FFrame& stack, std::wstring_view name, bool& out);
    bool Int(FFrame& stack, std::wstring_view name, int64_t& out);
    bool Float(FFrame& stack, std::wstring_view name, double& out);
    bool String(FFrame& stack, std::wstring_view name, std::wstring& out);
    bool Object(FFrame& stack, std::wstring_view name, UObject*& out);
    void* StructPtr(FFrame& stack, std::wstring_view name);

    // First parameter of the given property type name (e.g. "StrProperty"), excluding the return value.
    bool FirstOfType(FFrame& stack, std::wstring_view typeName, std::wstring& out);

    // "Name=value, Name2=value2" for logging.
    std::wstring Describe(FFrame& stack);
}
