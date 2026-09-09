#include "hotkeys/Hotkeys.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "diag/Diagnostics.hpp"
#include "features/Subtitles.hpp"
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

        void PollKeyboard()
        {
            if (g_keys.empty()) return;
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
                    Run(k.command);
                }
                k.wasDown = down;
            }
        }

        // ---- gamepad chord ----
        struct PadBinding
        {
            std::wstring keyName;
            Command command;
            bool wasDown = false;
        };
        std::vector<PadBinding> g_padBindings;
        std::wstring g_padHold;

        bool IsKeyDown(UObject* controller, const std::wstring& keyName)
        {
            static RC::Unreal::UFunction* fn = nullptr;
            if (!fn) fn = obj::FindFunction(controller, L"IsInputKeyDown");
            if (!fn) return false;
            bool result = false;
            obj::Call(
                controller, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Key")
                        {
                            auto* key = prop->ContainerPtrToValuePtr<uint8_t>(params);
                            std::construct_at(reinterpret_cast<RC::Unreal::FName*>(key), keyName.c_str(), RC::Unreal::FNAME_Add);
                        }
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadBoolAt(params, prop, result);
                    }
                });
            return result;
        }

        void PollPad()
        {
            if (g_padBindings.empty() || gamethread::FrameCount() % 3 != 0) return;
            UObject* controller = obj::LocalPlayerController();
            if (!controller) return;
            if (!IsKeyDown(controller, g_padHold))
            {
                for (auto& b : g_padBindings)
                    b.wasDown = false;
                return;
            }
            for (auto& b : g_padBindings)
            {
                const bool down = IsKeyDown(controller, b.keyName);
                if (down && !b.wasDown) Run(b.command);
                b.wasDown = down;
            }
        }
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
        Bind(s.keyDevDumpTree, Command::DevDumpTree);
        Bind(s.keyDevTrace, Command::DevTrace);
        Bind(s.keyDevLogLevel, Command::DevLogLevel);

        g_padHold = str::Trim(s.padChordHold);
        g_padBindings.clear();
        if (!g_padHold.empty())
        {
            if (!s.padRepeat.empty()) g_padBindings.push_back({s.padRepeat, Command::Repeat});
            if (!s.padReadScreen.empty()) g_padBindings.push_back({s.padReadScreen, Command::ReadScreen});
            if (!s.padStop.empty()) g_padBindings.push_back({s.padStop, Command::Stop});
            if (!s.padHelp.empty()) g_padBindings.push_back({s.padHelp, Command::Help});
            if (!s.padSubtitles.empty()) g_padBindings.push_back({s.padSubtitles, Command::Subtitles});
            if (!s.padLastSubtitle.empty()) g_padBindings.push_back({s.padLastSubtitle, Command::LastSubtitle});
        }
        gamethread::AddPoller(L"hotkeys",
                              [](float)
                              {
                                  PollKeyboard();
                                  PollPad();
                              });
    }

    void Run(Command command)
    {
        log::Info(L"hotkey: {}", CommandName(command));
        switch (command)
        {
        case Command::Repeat: speech::Repeat(); break;
        case Command::ReadScreen: speech::Now(diag::ReadScreen()); break;
        case Command::Stop: speech::Stop(); break;
        case Command::Help: speech::Now(diag::Help()); break;
        case Command::Subtitles: features::ToggleSubtitles(); break;
        case Command::LastSubtitle: features::SpeakLastSubtitle(); break;
        case Command::DevDumpTree:
            diag::DumpScreen();
            speech::Announce(locale::Mod(L"diag.dumped"));
            break;
        case Command::DevTrace: diag::ToggleTrace(); break;
        case Command::DevLogLevel: diag::CycleLogLevel(); break;
        }
    }
}
