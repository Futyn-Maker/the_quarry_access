#include "hotkeys/Hotkeys.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Gate.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "diag/Diagnostics.hpp"
#include "features/Combat.hpp"
#include "features/Exploration.hpp"
#include "features/Subtitles.hpp"
#include "hotkeys/KeyHook.hpp"
#include "hotkeys/KeySpec.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"

#include <windows.h>

#include <vector>

namespace qa::hotkeys
{
    namespace
    {
        struct KeyBinding
        {
            Command command;
            KeySpec key;
            bool wasDown = false;
        };

        std::vector<KeyBinding> g_keys;

        // The situations in which the mod's own letters are the mod's: the player walking the
        // character, fighting or looking around. Elsewhere they are the game's, or a text
        // field's.
        bool InContext()
        {
            return features::ExplorationActive() || features::CombatActive() || features::LookAroundActive();
        }

        // Whether a command answers now: every one while the mod is on, its switch alone
        // while it is off.
        bool Answers(Command command)
        {
            return gate::IsOn() || command == Command::ModOnOff;
        }

        void Bind(const Hotkey& hotkey)
        {
            const std::wstring spec = KeyOf(hotkey.command);
            KeyBinding binding{hotkey.command};
            if (!ParseKeySpec(spec, binding.key))
            {
                if (!spec.empty()) log::Error(L"hotkey '{}' for {} not understood", spec, hotkey.name);
                return;
            }
            g_keys.push_back(binding);
            log::Info(L"hotkey {} = {}", hotkey.name, spec);
        }

        bool GameWindowInForeground()
        {
            HWND fg = GetForegroundWindow();
            if (!fg) return false;
            DWORD pid = 0;
            GetWindowThreadProcessId(fg, &pid);
            return pid == GetCurrentProcessId();
        }

        bool Down(int vk)
        {
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        }

        // The name of a key the hook watches, for the log.
        std::wstring WatchedKeyName(int vk)
        {
            switch (vk)
            {
            case VK_ESCAPE: return L"Escape";
            case VK_BACK: return L"Backspace";
            case VK_UP: return L"Up";
            case VK_DOWN: return L"Down";
            case VK_LEFT: return L"Left";
            case VK_RIGHT: return L"Right";
            }
            return std::to_wstring(vk);
        }

        void PollKeyboard()
        {
            if (g_keys.empty()) return;
            // The hook hands over the presses it took; the poll below is for when it could
            // not be set.
            if (keyhook::Active())
            {
                // The exploration letters are the game's, or a text field's, outside
                // exploration, the fights and the look-arounds; the hook leaves them alone
                // there. While the mod is off the hook takes nothing but the switch.
                keyhook::SetOn(gate::IsOn());
                keyhook::SetContext(gate::IsOn() && InContext());
                for (const int id : keyhook::TakePresses())
                {
                    if (id >= 0 && static_cast<size_t>(id) < g_keys.size()) Run(g_keys[static_cast<size_t>(id)].command, Source::Keyboard);
                }
                return;
            }
            if (!GameWindowInForeground())
            {
                for (auto& k : g_keys)
                    k.wasDown = false;
                return;
            }
            const bool ctrl = Down(VK_CONTROL);
            const bool alt = Down(VK_MENU);
            const bool shift = Down(VK_SHIFT);
            for (auto& k : g_keys)
            {
                const bool down = Down(k.key.vk);
                if (down && !k.wasDown && ctrl == k.key.ctrl && alt == k.key.alt && shift == k.key.shift && Answers(k.command))
                {
                    Run(k.command, Source::Keyboard);
                }
                k.wasDown = down;
            }
        }

        // ---- gamepad chords ----
        struct PadBinding
        {
            std::wstring keyName;
            Command command;
            bool wasDown = false;
        };
        // Chords: the hold button and one of these.
        std::vector<PadBinding> g_padBindings;
        // Buttons that need no chord because the game leaves them unused while the player
        // walks the character freely or fights; they answer only there.
        std::vector<PadBinding> g_exploreBindings;
        std::wstring g_padHold;

        void PollBindings(const input::PadReading& pad, std::vector<PadBinding>& bindings, bool live)
        {
            for (auto& b : bindings)
            {
                if (!live)
                {
                    b.wasDown = false;
                    continue;
                }
                const bool down = input::PadKeyDown(pad, b.keyName);
                if (down && !b.wasDown && Answers(b.command)) Run(b.command, Source::Gamepad);
                b.wasDown = down;
            }
        }

        // The pad is read the way the game reads it, so the chords answer in a menu, where
        // the player controller no longer reports keys, as well as in play.
        void PollPad()
        {
            if ((g_padBindings.empty() && g_exploreBindings.empty()) || gamethread::FrameCount() % 3 != 0) return;
            const input::PadReading pad = GameWindowInForeground() ? input::ReadPad() : input::PadReading{};
            const bool chord = pad.valid && !g_padHold.empty() && input::PadKeyDown(pad, g_padHold);
            PollBindings(pad, g_padBindings, pad.valid && chord);
            PollBindings(pad, g_exploreBindings, pad.valid && !chord && gate::IsOn() && InContext());
        }

        // The mod switched off and on. Off is said before the switch turns, so that it is
        // heard; on is followed by the screen as F6 reads it, since whatever came up while
        // the mod was off went unsaid.
        void ToggleMod()
        {
            if (gate::IsOn())
            {
                speech::Now(locale::Mod(L"onoff.off"));
                gate::Set(false);
                return;
            }
            gate::Set(true);
            speech::Now(locale::Mod(L"onoff.on"));
            const auto readout = diag::Readout();
            if (!readout.empty()) speech::Announce(readout);
        }
    }

    void Uninstall()
    {
        keyhook::Uninstall();
    }

    std::wstring KeyOf(Command command)
    {
        const Hotkey& hotkey = Of(command);
        return str::Trim(cfg::Get().Hotkey(hotkey.name, hotkey.key));
    }

    std::wstring ChordOf(Command command)
    {
        const Hotkey& hotkey = Of(command);
        if (std::wstring_view(hotkey.chordName).empty()) return {};
        return str::Trim(cfg::Get().Hotkey(hotkey.chordName, hotkey.chord));
    }

    std::wstring PadOf(Command command)
    {
        const Hotkey& hotkey = Of(command);
        if (std::wstring_view(hotkey.padName).empty()) return {};
        return str::Trim(cfg::Get().Hotkey(hotkey.padName, hotkey.pad));
    }

    std::wstring ChordHold()
    {
        return str::Trim(cfg::Get().Hotkey(kChordHoldSetting, kChordHoldDefault));
    }

    bool ChordsInHand()
    {
        return input::CurrentScheme() == input::Scheme::Gamepad && !ChordHold().empty();
    }

    std::wstring SpokenKey(Command command)
    {
        if (input::CurrentScheme() == input::Scheme::Gamepad)
        {
            const auto pad = PadOf(command);
            if (!pad.empty()) return input::KeyDisplayName(pad);
        }
        return KeyOf(command);
    }

    void Install()
    {
        g_keys.clear();
        for (const Hotkey& hotkey : Table())
            Bind(hotkey);

        std::vector<keyhook::Binding> bindings;
        for (size_t i = 0; i < g_keys.size(); ++i)
        {
            const Scope scope = Of(g_keys[i].command).scope;
            bindings.push_back(keyhook::Binding{g_keys[i].key.vk, g_keys[i].key.ctrl, g_keys[i].key.alt, g_keys[i].key.shift, static_cast<int>(i),
                                                scope == Scope::Context, scope == Scope::Switch});
        }
        // The keys that move the selection or leave a screen stay the game's; the mod only
        // answers each, by stopping what is being said the moment it goes down, whether or
        // not the game answers it with anything new: a list moves on at once, and a screen
        // the player is leaving is not still being read over the one they arrive at. Only
        // the hook can do this: it runs before the game is given the key, so the stop always
        // lands before whatever the new screen says, while a polled key could arrive after
        // the new screen had been announced and silence that instead. Every other key only
        // lets the next answer interrupt (speech::Focus).
        keyhook::Watch({VK_ESCAPE, VK_BACK, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT}, [](int vk) { speech::StopForKey(WatchedKeyName(vk)); });
        if (keyhook::Install(bindings))
            log::Info(L"keyboard hook installed: the mod's keys are taken ahead of the screen reader and the game");
        else
            log::Error(L"the keyboard hook could not be set; the mod's keys are polled, and a key the screen reader takes for itself is lost");

        g_padHold = ChordHold();
        input::SetChordHold(g_padHold);
        // The pad's own keys for the same: the D-pad, B and Start, answered where the game
        // reads its pad, which is before it acts on them. The sticks are never among them: a
        // walk holds one for seconds, and a stick the game answers with a new selection is
        // answered by the reading of that selection.
        input::WatchPadButtons(
            {L"Gamepad_DPad_Up", L"Gamepad_DPad_Down", L"Gamepad_DPad_Left", L"Gamepad_DPad_Right", L"Gamepad_FaceButton_Right", L"Gamepad_Special_Right"},
            [](std::wstring_view key) { speech::StopForKey(key); });
        g_padBindings.clear();
        if (!g_padHold.empty())
        {
            for (const Hotkey& hotkey : Table())
            {
                const auto button = ChordOf(hotkey.command);
                if (button.empty()) continue;
                g_padBindings.push_back({button, hotkey.command});
                log::Info(L"gamepad {} + {} = {}", g_padHold, button, hotkey.name);
            }
        }
        for (const auto& key : cfg::Get().obsoleteKeys)
            log::Info(L"config: [Hotkeys] {} is no longer read; the chords are set with ChordHold, ChordRepeat, ChordReadScreen, ChordStop, ChordHelp, "
                      L"ChordSubtitles, ChordLastSubtitle, ChordSpeech, ChordVerbosity, ChordModOnOff, ChordDevDumpTree and ChordDevLogLevel",
                      key);
        g_exploreBindings.clear();
        for (const Hotkey& hotkey : Table())
        {
            const auto button = PadOf(hotkey.command);
            if (button.empty()) continue;
            g_exploreBindings.push_back({button, hotkey.command});
            log::Info(L"gamepad while walking: {} = {}", button, hotkey.name);
        }
        gamethread::AddPoller(L"hotkeys",
                              [](float)
                              {
                                  PollKeyboard();
                                  PollPad();
                              });
    }

    namespace
    {
        void RunImpl(Command command);

        void RunGuarded(void* context)
        {
            RunImpl(*static_cast<Command*>(context));
        }
    }

    void Run(Command command, Source source)
    {
        log::Info(L"hotkey: {}{}", CommandName(command), source == Source::Keyboard ? L" (keyboard)" : (source == Source::Gamepad ? L" (gamepad)" : L""));
        // The answer is worded for the device the command came from.
        if (source == Source::Keyboard) input::NoteHotkeyDevice(input::Scheme::MouseKeyboard);
        if (source == Source::Gamepad) input::NoteHotkeyDevice(input::Scheme::Gamepad);
        // A hotkey must never take the game down: a fault abandons the command and is logged.
        if (!obj::SafeInvoke(&RunGuarded, &command)) log::Error(L"hotkey {} ran into a memory fault and was abandoned", CommandName(command));
    }

    namespace
    {
        void RunImpl(Command command)
        {
            switch (command)
            {
            case Command::Repeat: speech::Repeat(); break;
            case Command::ReadScreen: speech::Now(diag::ReadScreen()); break;
            case Command::Stop: speech::Stop(); break;
            case Command::Help: speech::Now(diag::Help()); break;
            case Command::Subtitles: features::ToggleSubtitles(); break;
            case Command::LastSubtitle: features::SpeakLastSubtitle(); break;
            case Command::Speech: speech::ToggleOutput(); break;
            case Command::Verbosity: speech::Now(locale::Mod(cfg::ToggleVerbosity() ? L"verbosity.full" : L"verbosity.brief")); break;
            case Command::ModOnOff: ToggleMod(); break;
            // The target keys serve the fight while one is on, exploration while the player
            // walks the character, and nothing anywhere else.
            case Command::NextTarget:
                if (features::CombatActive())
                    features::NextCombatTarget();
                else if (features::ExplorationActive())
                    features::NextTarget();
                break;
            case Command::PreviousTarget:
                if (features::CombatActive())
                    features::PreviousCombatTarget();
                else if (features::ExplorationActive())
                    features::PreviousTarget();
                break;
            case Command::Where:
                if (features::CombatActive())
                    features::WhereIsCombatTarget();
                else if (features::ExplorationActive())
                    features::WhereIsTarget();
                break;
            case Command::Walk:
                if (features::ExplorationActive()) features::WalkToTarget();
                break;
            case Command::Beacon:
                if (features::CombatActive() || features::LookAroundActive())
                    features::ToggleAimSound();
                else if (features::ExplorationActive())
                    features::ToggleBeacon();
                break;
            case Command::TarotCards:
                if (features::ExplorationActive()) features::ToggleTarotCards();
                break;
            case Command::DevDumpTree:
                diag::DumpScreen();
                speech::Announce(locale::Mod(L"diag.dumped"));
                break;
            case Command::DevTrace: diag::ToggleTrace(); break;
            case Command::DevLogLevel: diag::CycleLogLevel(); break;
            }
        }
    }
}
