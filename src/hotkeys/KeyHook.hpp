#pragma once
// The mod's keyboard keys are taken by a low-level keyboard hook on a thread of the mod's
// own, ahead of the screen reader and the game. A screen reader, or one of its add-ons, can
// bind a key for itself and take it before the game ever sees it (an NVDA add-on takes F3
// for "find next"); a hook installed after the screen reader's is called before it, so a key
// bound to a mod command is answered every time, and neither the screen reader nor the game
// sees it.

#include <vector>

namespace qa::keyhook
{
    // Where a binding is the mod's: everywhere, or only in a situation of the mod's own, the
    // key being the game's, or a text field's, everywhere else.
    enum class Situation
    {
        Always,
        Context,      // the player walks the character, fights or looks around (the exploration letters)
        Dropdown,     // a dropdown the mod stands in for has the selection, its list open or shut
        DropdownList, // the mod's list of a dropdown is open
    };

    struct Binding
    {
        int vk = 0;
        bool ctrl = false;
        bool alt = false;
        bool shift = false;
        int id = 0;
        Situation situation = Situation::Always;
        // The mod's own switch, taken while the mod is off as well.
        bool switchKey = false;
    };

    // A key the mod watches but never takes: the handler runs on the hook's own thread the
    // moment the key goes down, before the game is given it, and the key goes on to the game
    // all the same. Set before Install; it is read only once the hook runs.
    using Watcher = void (*)(int vk);
    void Watch(std::vector<int> vks, Watcher handler);

    // Starts the hook thread with the keys to take. False when the hook could not be set.
    bool Install(std::vector<Binding> bindings);
    void Uninstall();
    bool Active();
    // Whether the contextual bindings are taken now; set from the game thread as the
    // context comes and goes.
    void SetContext(bool active);
    // Whether a dropdown the mod stands in for has the selection, and whether the mod's list
    // of it is open (features/Menus); set from the game thread as either changes.
    enum class Dropdown
    {
        None,
        Selected,
        Open,
    };
    void SetDropdown(Dropdown state);
    // Whether the mod is on; off, the hook takes nothing but the switch. Set from the game
    // thread as the mod is switched.
    void SetOn(bool on);

    // The ids of the bindings pressed since the last call, in order.
    std::vector<int> TakePresses();
}
