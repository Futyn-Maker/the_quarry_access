#include "hotkeys/Hotkeys.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "diag/Diagnostics.hpp"
#include "features/Combat.hpp"
#include "features/Exploration.hpp"
#include "features/Subtitles.hpp"
#include "hotkeys/KeyHook.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"

#include <Unreal/NameTypes.hpp>

#include <windows.h>

#include <map>
#include <memory>
#include <vector>

namespace qa::hotkeys
{
    using RC::Unreal::UObject;

    namespace
    {
        struct KeyBinding
        {
            Command command;
            int vk = 0;
            bool ctrl = false;
            bool alt = false;
            bool shift = false;
            bool wasDown = false;
        };

        const std::map<std::wstring, int> kNamedKeys = {
            {L"space", 0x20},  {L"enter", 0x0D},  {L"return", 0x0D}, {L"escape", 0x1B}, {L"esc", 0x1B},      {L"tab", 0x09},      {L"backspace", 0x08},
            {L"insert", 0x2D}, {L"delete", 0x2E}, {L"home", 0x24},   {L"end", 0x23},    {L"pageup", 0x21},   {L"pagedown", 0x22}, {L"up", 0x26},
            {L"down", 0x28},   {L"left", 0x25},   {L"right", 0x27},  {L"pause", 0x13},  {L"capslock", 0x14}, {L"numlock", 0x90},  {L"scrolllock", 0x91},
        };

        std::vector<KeyBinding> g_keys;

        bool Parse(const std::wstring& spec, Command command, KeyBinding& out)
        {
            out = KeyBinding{};
            out.command = command;
            const auto lower = str::ToLower(str::Trim(spec));
            if (lower.empty() || lower == L"none") return false;
            for (auto& token : str::Split(lower, L'+'))
            {
                if (token == L"ctrl" || token == L"control")
                {
                    out.ctrl = true;
                    continue;
                }
                if (token == L"alt")
                {
                    out.alt = true;
                    continue;
                }
                if (token == L"shift")
                {
                    out.shift = true;
                    continue;
                }
                int vk = 0;
                if (token.size() >= 2 && token[0] == L'f' && iswdigit(token[1]))
                {
                    const int n = std::stoi(token.substr(1));
                    if (n >= 1 && n <= 24) vk = 0x70 + (n - 1);
                }
                else if (token.size() == 1 && token[0] >= L'a' && token[0] <= L'z')
                {
                    vk = 0x41 + (token[0] - L'a');
                }
                else if (token.size() == 1 && iswdigit(token[0]))
                {
                    vk = 0x30 + (token[0] - L'0');
                }
                else if (str::StartsWith(token, L"numpad") && token.size() == 7 && iswdigit(token[6]))
                {
                    vk = 0x60 + (token[6] - L'0');
                }
                else if (const auto it = kNamedKeys.find(token); it != kNamedKeys.end())
                {
                    vk = it->second;
                }
                if (vk != 0) out.vk = vk;
            }
            return out.vk != 0;
        }

        void Bind(const std::wstring& spec, Command command)
        {
            KeyBinding binding;
            if (!Parse(spec, command, binding))
            {
                if (!str::Trim(spec).empty()) log::Error(L"hotkey '{}' for {} not understood", spec, CommandName(command));
                return;
            }
            g_keys.push_back(binding);
            log::Info(L"hotkey {} = {}", CommandName(command), spec);
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
                // there.
                keyhook::SetContext(features::ExplorationActive() || features::CombatActive() || features::LookAroundActive());
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
                const bool down = Down(k.vk);
                if (down && !k.wasDown && ctrl == k.ctrl && alt == k.alt && shift == k.shift)
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
                if (down && !b.wasDown) Run(b.command, Source::Gamepad);
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
            PollBindings(pad, g_exploreBindings,
                         pad.valid && !chord && (features::ExplorationActive() || features::CombatActive() || features::LookAroundActive()));
        }
    }

    void Uninstall()
    {
        keyhook::Uninstall();
    }

    std::wstring CommandName(Command command)
    {
        switch (command)
        {
        case Command::Repeat: return L"Repeat";
        case Command::ReadScreen: return L"ReadScreen";
        case Command::Stop: return L"Stop";
        case Command::Help: return L"Help";
        case Command::Subtitles: return L"Subtitles";
        case Command::LastSubtitle: return L"LastSubtitle";
        case Command::Speech: return L"Speech";
        case Command::Verbosity: return L"Verbosity";
        case Command::NextTarget: return L"NextTarget";
        case Command::PreviousTarget: return L"PreviousTarget";
        case Command::Where: return L"Where";
        case Command::Walk: return L"Walk";
        case Command::Beacon: return L"Beacon";
        case Command::TarotCards: return L"TarotCards";
        case Command::DevDumpTree: return L"DevDumpTree";
        case Command::DevTrace: return L"DevTrace";
        case Command::DevLogLevel: return L"DevLogLevel";
        }
        return L"?";
    }

    void Install()
    {
        const auto& s = cfg::Get();
        g_keys.clear();
        Bind(s.keyRepeat, Command::Repeat);
        Bind(s.keyReadScreen, Command::ReadScreen);
        Bind(s.keyStop, Command::Stop);
        Bind(s.keyHelp, Command::Help);
        Bind(s.keySubtitles, Command::Subtitles);
        Bind(s.keyLastSubtitle, Command::LastSubtitle);
        Bind(s.keySpeech, Command::Speech);
        Bind(s.keyVerbosity, Command::Verbosity);
        Bind(s.keyNextTarget, Command::NextTarget);
        Bind(s.keyPreviousTarget, Command::PreviousTarget);
        Bind(s.keyWhere, Command::Where);
        Bind(s.keyWalk, Command::Walk);
        Bind(s.keyBeacon, Command::Beacon);
        Bind(s.keyTarotCards, Command::TarotCards);
        Bind(s.keyDevDumpTree, Command::DevDumpTree);
        Bind(s.keyDevTrace, Command::DevTrace);
        Bind(s.keyDevLogLevel, Command::DevLogLevel);

        std::vector<keyhook::Binding> bindings;
        for (size_t i = 0; i < g_keys.size(); ++i)
        {
            const Command c = g_keys[i].command;
            const bool contextual = c == Command::NextTarget || c == Command::PreviousTarget || c == Command::Where || c == Command::Walk ||
                                    c == Command::Beacon || c == Command::TarotCards;
            bindings.push_back(keyhook::Binding{g_keys[i].vk, g_keys[i].ctrl, g_keys[i].alt, g_keys[i].shift, static_cast<int>(i), contextual});
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

        g_padHold = str::Trim(s.chordHold);
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
            for (const auto& [key, command] :
                 {std::pair{s.chordRepeat, Command::Repeat}, std::pair{s.chordReadScreen, Command::ReadScreen}, std::pair{s.chordStop, Command::Stop},
                  std::pair{s.chordHelp, Command::Help}, std::pair{s.chordSubtitles, Command::Subtitles}, std::pair{s.chordLastSubtitle, Command::LastSubtitle},
                  std::pair{s.chordSpeech, Command::Speech}, std::pair{s.chordVerbosity, Command::Verbosity},
                  std::pair{s.chordDevDumpTree, Command::DevDumpTree}, std::pair{s.chordDevLogLevel, Command::DevLogLevel}})
            {
                if (str::Trim(key).empty()) continue;
                g_padBindings.push_back({str::Trim(key), command});
                log::Info(L"gamepad {} + {} = {}", g_padHold, str::Trim(key), CommandName(command));
            }
        }
        for (const auto& key : s.obsoleteKeys)
            log::Info(L"config: [Hotkeys] {} is no longer read; the chords are set with ChordHold, ChordRepeat, ChordReadScreen, ChordStop, ChordHelp, "
                      L"ChordSubtitles, ChordLastSubtitle, ChordSpeech, ChordVerbosity, ChordDevDumpTree and ChordDevLogLevel",
                      key);
        g_exploreBindings.clear();
        for (const auto& [key, command] : {std::pair{s.padExploreNext, Command::NextTarget}, std::pair{s.padExplorePrevious, Command::PreviousTarget},
                                           std::pair{s.padExploreWhere, Command::Where}, std::pair{s.padExploreWalk, Command::Walk},
                                           std::pair{s.padExploreBeacon, Command::Beacon}, std::pair{s.padExploreTarotCards, Command::TarotCards}})
        {
            if (key.empty()) continue;
            g_exploreBindings.push_back({key, command});
            log::Info(L"gamepad while walking: {} = {}", key, CommandName(command));
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
