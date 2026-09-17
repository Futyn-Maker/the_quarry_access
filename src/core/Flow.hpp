#pragma once
// The game flow's blackboard: the bools and flags a scene's flow keeps and reads its own
// branches from. The mod reads them where the scene shows the player the same thing by
// other means, so that what is on screen can be said.

#include <Unreal/UObject.hpp>

#include <string>
#include <string_view>

namespace qa::flow
{
    using RC::Unreal::UObject;

    // The value of a bool or a flag, asked of the game's own blackboard library by the
    // variable object. False when the library, the world or the variable is not there.
    bool Value(UObject* variable, bool flag, bool& out);

    // The live variable of that name, or null when no loaded scene holds one.
    UObject* Find(std::wstring_view name, bool flag);

    // The value of the blackboard bool of that name.
    bool Bool(std::wstring_view name, bool& out);
}
