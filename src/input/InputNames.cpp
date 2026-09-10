#include "input/InputNames.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "locale/Locale.hpp"
#include "watch/Watchers.hpp"

#include <Unreal/Core/Containers/Array.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>

namespace qa::input
{
    using RC::Unreal::FName;
    using RC::Unreal::FString;
    using RC::Unreal::FText;
    using RC::Unreal::TArray;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;

    namespace
    {
        std::mutex g_mutex;
        std::map<std::pair<int, std::wstring>, std::wstring> g_actionCache;
        std::map<std::wstring, std::wstring> g_keyNameCache;

        // Size of one FKey, taken from the engine's own reflection rather than assumed,
        // so the returned key array is walked with the right stride.
        size_t FKeyStride()
        {
            static size_t cached = 0;
            if (cached) return cached;
            cached = 0x18;
            if (auto* lib = obj::FindObject(L"/Script/Engine.Default__KismetInputLibrary"))
            {
                if (auto* fn = obj::FindFunction(lib, L"Key_GetDisplayName"))
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Key" && prop->GetSize() > 0)
                        {
                            cached = static_cast<size_t>(prop->GetSize());
                            break;
                        }
                    }
                }
            }
            return cached;
        }

        // The screen on display, which the game asks for the keys of an action in menus.
        // Nothing else is used: a widget kept from an earlier screen is torn down with
        // that screen, and calling into it takes the game down.
        UObject* ScreenWidget()
        {
            UObject* screen = watch::CurrentScreen();
            if (screen && obj::IsLive(screen) && obj::FindFunction(screen, L"GetKeysFromActionMapping")) return screen;
            for (auto* candidate : watch::CurrentScreens())
            {
                if (obj::IsLive(candidate) && obj::FindFunction(candidate, L"GetKeysFromActionMapping")) return candidate;
            }
            return nullptr;
        }

        // The keys of a mapping in the engine's input settings, by reflection alone.
        std::vector<std::wstring> MappingKeys(const wchar_t* table, const wchar_t* nameField, const std::wstring& mappingName)
        {
            std::vector<std::wstring> keys;
            UObject* settings = obj::FindObject(L"/Script/Engine.Default__InputSettings");
            if (!settings) return keys;
            obj::ForEachArrayElement(settings, obj::FindProperty(settings, table),
                                     [&](void* element, RC::Unreal::FProperty* inner)
                                     {
                                         std::wstring name;
                                         if (!obj::ReadStringAt(element, obj::StructMember(inner, nameField), name) || name != mappingName) return;
                                         auto* keyProp = obj::StructMember(inner, L"Key");
                                         std::wstring key;
                                         if (keyProp && obj::ReadStringAt(obj::ValuePtrAt(element, keyProp), obj::StructMember(keyProp, L"KeyName"), key) &&
                                             !key.empty())
                                             keys.push_back(key);
                                     });
            return keys;
        }

        // The keys of an action as the game's keyboard remapping shows them: an action listed
        // in one of the game's remap rows is bound to that row's key (the row's own editable
        // mapping), plus the row's fixed key when it has one. Actions outside the rows keep
        // their own mapping.
        std::vector<std::wstring> KeysFromBindingRows(const std::wstring& action)
        {
            std::vector<std::wstring> keys;
            UObject* ui = obj::FindObject(L"/Script/SMG026Runtime.Default__UISettingsSMG026");
            UObject* data = nullptr;
            if (!ui || !obj::ReadObject(ui, L"KeyBindingSettingsData", data) || !obj::IsLive(data)) return keys;
            bool found = false;
            std::wstring edit;
            std::wstring fixedKey;
            int64_t type = 0;
            obj::ForEachArrayElement(data, obj::FindProperty(data, L"KeyBindingSettings"),
                                     [&](void* row, RC::Unreal::FProperty* rowType)
                                     {
                                         if (found) return;
                                         void* rowPtr = obj::ValuePtrAt(row, rowType);
                                         bool listed = false;
                                         obj::ForEachArrayElement(
                                             rowPtr, obj::StructMember(rowType, L"MappingReferences"),
                                             [&](void* ref, RC::Unreal::FProperty* refType)
                                             {
                                                 std::wstring value;
                                                 if (obj::ReadStringAt(obj::ValuePtrAt(ref, refType), obj::StructMember(refType, L"Value"), value) &&
                                                     value == action)
                                                     listed = true;
                                             });
                                         if (!listed) return;
                                         found = true;
                                         if (auto* editRef = obj::StructMember(rowType, L"UIEditMappingReference"))
                                             obj::ReadStringAt(obj::ValuePtrAt(rowPtr, editRef), obj::StructMember(editRef, L"Value"), edit);
                                         obj::ReadIntAt(rowPtr, obj::StructMember(rowType, L"InputMappingType"), type);
                                         if (auto* fixed = obj::StructMember(rowType, L"FixedKey"))
                                             obj::ReadStringAt(obj::ValuePtrAt(rowPtr, fixed), obj::StructMember(fixed, L"KeyName"), fixedKey);
                                     });
            if (!found || edit.empty()) return keys;
            keys = type == 0 ? MappingKeys(L"ActionMappings", L"ActionName", edit) : MappingKeys(L"AxisMappings", L"AxisName", edit);
            if (!fixedKey.empty() && fixedKey != L"None") keys.push_back(fixedKey);
            return keys;
        }

        std::wstring Humanize(std::wstring_view keyName)
        {
            std::wstring s(keyName);
            if (str::StartsWith(s, L"Gamepad_")) s.erase(0, 8);
            // Split CamelCase and underscores into words.
            std::wstring out;
            for (size_t i = 0; i < s.size(); ++i)
            {
                const wchar_t c = s[i];
                if (c == L'_')
                {
                    out += L' ';
                    continue;
                }
                if (i > 0 && iswupper(c) && iswlower(s[i - 1])) out += L' ';
                out += c;
            }
            return str::CollapseWhitespace(out);
        }

        std::wstring EngineDisplayName(std::wstring_view keyName)
        {
            auto* lib = obj::FindObject(L"/Script/Engine.Default__KismetInputLibrary");
            if (!lib) return {};
            auto* fn = obj::FindFunction(lib, L"Key_GetDisplayName");
            if (!fn) return {};
            std::wstring result;
            const std::wstring name(keyName);
            obj::Call(
                lib, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Key")
                        {
                            auto* key = prop->ContainerPtrToValuePtr<uint8_t>(params);
                            std::construct_at(reinterpret_cast<FName*>(key), name.c_str(), RC::Unreal::FNAME_Add);
                        }
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadStringAt(params, prop, result);
                    }
                });
            return result;
        }
    }

    Scheme CurrentScheme()
    {
        auto* pc = obj::LocalPlayerController();
        if (!pc) return Scheme::Unknown;
        int64_t scheme = 0;
        if (!obj::ReadInt(pc, L"CurrentControlScheme", scheme)) return Scheme::Unknown;
        switch (scheme)
        {
        case 1: return Scheme::Gamepad;
        case 2: return Scheme::MouseKeyboard;
        case 3: return Scheme::Touch;
        default: return Scheme::Unknown;
        }
    }

    std::wstring SchemeName(Scheme scheme)
    {
        switch (scheme)
        {
        case Scheme::Gamepad: return L"Gamepad";
        case Scheme::MouseKeyboard: return L"MouseKeyboard";
        case Scheme::Touch: return L"Touch";
        default: return L"Unknown";
        }
    }

    std::wstring KeyDisplayName(std::wstring_view keyName)
    {
        if (keyName.empty() || keyName == L"None") return {};
        const std::wstring name(keyName);
        {
            std::lock_guard lock(g_mutex);
            const auto it = g_keyNameCache.find(name);
            if (it != g_keyNameCache.end()) return it->second;
        }
        // 1. Mod table (localized, curated names).
        const std::wstring localeKey = L"key." + str::ToLower(name);
        std::wstring result;
        if (locale::Has(localeKey)) result = locale::Mod(localeKey);
        // 2. Engine display name.
        if (result.empty()) result = EngineDisplayName(name);
        // 3. Humanized engine key name.
        if (result.empty()) result = Humanize(name);
        std::lock_guard lock(g_mutex);
        g_keyNameCache[name] = result;
        return result;
    }

    std::wstring KeyForAction(std::wstring_view actionName)
    {
        const Scheme scheme = CurrentScheme();
        const std::wstring action(actionName);
        const auto cacheKey = std::make_pair(static_cast<int>(scheme), action);
        {
            std::lock_guard lock(g_mutex);
            const auto it = g_actionCache.find(cacheKey);
            if (it != g_actionCache.end()) return it->second;
        }

        std::vector<std::wstring> keys;
        const wchar_t* source = L"key binding row";
        if (scheme != Scheme::Gamepad) keys = KeysFromBindingRows(action);
        if (keys.empty()) source = L"screen";
        if (UObject* widget = keys.empty() ? ScreenWidget() : nullptr)
        {
            auto* fn = obj::FindFunction(widget, L"GetKeysFromActionMapping");
            log::Verbose(L"input: resolving {} via {} {} (params {} bytes)", action, obj::ClassName(widget), obj::ObjectName(widget), fn->GetPropertiesSize());
            obj::Call(
                widget, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        if (obj::PropertyTypeName(prop) == L"StructProperty" &&
                            !prop->HasAnyPropertyFlags(static_cast<uint64_t>(RC::Unreal::EPropertyFlags::CPF_ReturnParm)))
                        {
                            // FActionMappingReference { FString Value; }
                            auto* value = prop->ContainerPtrToValuePtr<FString>(params);
                            std::construct_at(value, action.c_str());
                        }
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop || prop->GetName() != L"ReturnValue") continue;
                        auto* arr = prop->ContainerPtrToValuePtr<TArray<uint8_t>>(params);
                        if (!arr || arr->Num() <= 0 || !arr->GetData()) continue;
                        const int32_t n = std::min<int32_t>(arr->Num(), 16);
                        for (int32_t i = 0; i < n; ++i)
                        {
                            auto* key = reinterpret_cast<FName*>(arr->GetData() + static_cast<size_t>(i) * FKeyStride());
                            keys.push_back(key->ToString());
                        }
                    }
                });
        }
        if (keys.empty())
        {
            keys = MappingKeys(L"ActionMappings", L"ActionName", action);
            source = L"input settings";
        }

        std::wstring chosen;
        const bool wantPad = scheme == Scheme::Gamepad;
        for (const auto& k : keys)
        {
            const bool isPad = str::StartsWith(k, L"Gamepad_");
            const bool isTouch = str::StartsWith(k, L"Touch");
            if (isTouch) continue;
            if (isPad == wantPad)
            {
                chosen = k;
                break;
            }
        }
        if (chosen.empty() && !keys.empty()) chosen = keys.front();
        const std::wstring display = KeyDisplayName(chosen);
        log::Verbose(L"input: action {} -> keys [{}] -> \"{}\" (scheme {}, from {})", action, str::Join(keys, L", "), display, SchemeName(scheme), source);
        // Nothing found is not remembered: the screen or the settings may not be ready yet.
        if (!display.empty())
        {
            std::lock_guard lock(g_mutex);
            g_actionCache[cacheKey] = display;
        }
        return display;
    }

    // Both sources of an action's keys, for the log.
    std::wstring DescribeAction(std::wstring_view actionName)
    {
        const std::wstring action(actionName);
        std::wstring screen = L"<no screen>";
        if (UObject* widget = ScreenWidget()) screen = obj::ClassName(widget);
        return L"action " + action + L": key binding row [" + str::Join(KeysFromBindingRows(action), L", ") + L"], screen " + screen + L", input settings [" +
               str::Join(MappingKeys(L"ActionMappings", L"ActionName", action), L", ") + L"], spoken \"" + KeyForAction(action) + L"\"";
    }

    std::wstring PointerName()
    {
        return locale::Mod(CurrentScheme() == Scheme::Gamepad ? L"input.rightstick" : L"input.mouse");
    }

    void InvalidateCache()
    {
        std::lock_guard lock(g_mutex);
        g_actionCache.clear();
        g_keyNameCache.clear();
    }

    // ---- input activity ----------------------------------------------------

    namespace
    {
        std::atomic<long long> g_lastInputAt{0}; // steady clock milliseconds; 0 = nothing yet
        POINT g_lastCursor{};
        bool g_haveCursor = false;

        long long NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        bool GameWindowInForeground()
        {
            HWND foreground = GetForegroundWindow();
            if (!foreground) return false;
            DWORD pid = 0;
            GetWindowThreadProcessId(foreground, &pid);
            return pid == GetCurrentProcessId();
        }

        // Everything the game can be driven with, including rebindable keys. A key that is
        // held down counts too, so holding a direction to scroll a menu stays "activity",
        // and so does a tap released between two polls (the low bit of the key state) when
        // the mask asks for it.
        bool AnyKeyDown(int mask)
        {
            static constexpr std::pair<int, int> kRanges[] = {
                {VK_LBUTTON, VK_XBUTTON2},
                {VK_BACK, VK_TAB},
                {VK_RETURN, VK_RETURN},
                {VK_SHIFT, VK_MENU},
                {VK_ESCAPE, VK_ESCAPE},
                {VK_SPACE, VK_HELP},
                {'0', '9'},
                {'A', 'Z'},
                {VK_NUMPAD0, VK_DIVIDE},
                {VK_F1, VK_F24},
                {VK_OEM_1, VK_OEM_102},
            };
            for (const auto& [first, last] : kRanges)
            {
                for (int vk = first; vk <= last; ++vk)
                {
                    if (GetAsyncKeyState(vk) & mask) return true;
                }
            }
            return false;
        }

        struct XInputGamepad
        {
            uint16_t buttons;
            uint8_t leftTrigger;
            uint8_t rightTrigger;
            int16_t thumbLX;
            int16_t thumbLY;
            int16_t thumbRX;
            int16_t thumbRY;
        };
        struct XInputState
        {
            uint32_t packetNumber;
            XInputGamepad gamepad;
        };
        using XInputGetStateFn = uint32_t(__stdcall*)(uint32_t, XInputState*);

        XInputGetStateFn XInput()
        {
            static XInputGetStateFn function = nullptr;
            static bool resolved = false;
            if (resolved) return function;
            resolved = true;
            for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"})
            {
                HMODULE module = GetModuleHandleW(name);
                if (!module) module = LoadLibraryW(name);
                if (!module) continue;
                function = reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"));
                if (function) break;
            }
            log::Info(L"input: gamepad activity {}", function ? L"tracked" : L"unavailable");
            return function;
        }

        // Asking an empty controller slot for its state is slow, so slots are only
        // re-checked once a second until one answers.
        bool g_padConnected[4] = {true, true, true, true};
        long long g_padScanAt = 0;
        constexpr int16_t kStickDeadZone = 12000;

        bool AnyPadInput()
        {
            auto* getState = XInput();
            if (!getState) return false;
            bool* connected = g_padConnected;
            long long& nextScanAt = g_padScanAt;
            const long long now = NowMs();
            const bool scanAll = now >= nextScanAt;
            if (scanAll) nextScanAt = now + 1000;
            constexpr int16_t kDeadZone = kStickDeadZone;
            bool active = false;
            for (uint32_t pad = 0; pad < 4; ++pad)
            {
                if (!connected[pad] && !scanAll) continue;
                XInputState state{};
                connected[pad] = getState(pad, &state) == ERROR_SUCCESS;
                if (!connected[pad]) continue;
                const auto& g = state.gamepad;
                if (g.buttons != 0 || g.leftTrigger > 40 || g.rightTrigger > 40) active = true;
                if (g.thumbLX > kDeadZone || g.thumbLX < -kDeadZone || g.thumbLY > kDeadZone || g.thumbLY < -kDeadZone) active = true;
                if (g.thumbRX > kDeadZone || g.thumbRX < -kDeadZone || g.thumbRY > kDeadZone || g.thumbRY < -kDeadZone) active = true;
            }
            return active;
        }

        // The left stick, which is what the character walks with.
        bool LeftStickPushed()
        {
            auto* getState = XInput();
            if (!getState) return false;
            for (uint32_t pad = 0; pad < 4; ++pad)
            {
                if (!g_padConnected[pad]) continue;
                XInputState state{};
                if (getState(pad, &state) != ERROR_SUCCESS) continue;
                const auto& g = state.gamepad;
                if (g.thumbLX > kStickDeadZone || g.thumbLX < -kStickDeadZone || g.thumbLY > kStickDeadZone || g.thumbLY < -kStickDeadZone) return true;
            }
            return false;
        }

        // The Windows key code of an engine key name, for the few names a character walks with.
        int VirtualKeyOf(const std::wstring& engineName)
        {
            if (engineName.size() == 1)
            {
                const wchar_t c = towupper(engineName[0]);
                if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return static_cast<int>(c);
            }
            static const std::map<std::wstring, int> named = {
                {L"Up", VK_UP},          {L"Down", VK_DOWN},    {L"Left", VK_LEFT},        {L"Right", VK_RIGHT},
                {L"SpaceBar", VK_SPACE}, {L"Enter", VK_RETURN}, {L"LeftShift", VK_LSHIFT}, {L"RightShift", VK_RSHIFT}};
            const auto it = named.find(engineName);
            return it == named.end() ? 0 : it->second;
        }

        // One key of the game's for each way the character walks.
        struct WalkKeys
        {
            int forward = 0, back = 0, left = 0, right = 0;
        };

        bool TypesACharacter(int vk)
        {
            return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9');
        }

        // The keys the game itself walks the character with, taken from its axis settings so
        // a remapped set is followed. Where a way has both a letter and an arrow, the arrow
        // wins: a screen reader stops speaking whenever a character key goes down.
        const WalkKeys& MovementKeys()
        {
            static WalkKeys keys;
            static bool resolved = false;
            if (resolved) return keys;
            UObject* settings = obj::FindObject(L"/Script/Engine.Default__InputSettings");
            if (!settings) return keys;
            resolved = true;
            obj::ForEachArrayElement(settings, obj::FindProperty(settings, L"AxisMappings"),
                                     [&](void* element, RC::Unreal::FProperty* inner)
                                     {
                                         std::wstring axis;
                                         if (!obj::ReadStringAt(element, obj::StructMember(inner, L"AxisName"), axis)) return;
                                         const bool sideways = axis == L"MovementX";
                                         if (!sideways && axis != L"MovementY") return;
                                         double scale = 0.0;
                                         obj::ReadFloatAt(element, obj::StructMember(inner, L"Scale"), scale);
                                         auto* keyProp = obj::StructMember(inner, L"Key");
                                         std::wstring name;
                                         if (!keyProp || !obj::ReadStringAt(obj::ValuePtrAt(element, keyProp), obj::StructMember(keyProp, L"KeyName"), name))
                                             return;
                                         const int vk = VirtualKeyOf(name);
                                         if (vk == 0 || scale == 0.0) return;
                                         int& slot = sideways ? (scale > 0 ? keys.right : keys.left) : (scale > 0 ? keys.forward : keys.back);
                                         if (slot == 0 || (TypesACharacter(slot) && !TypesACharacter(vk))) slot = vk;
                                     });
            log::Info(L"input: the character walks with keys {} {} {} {}", keys.forward, keys.back, keys.left, keys.right);
            return keys;
        }

        int g_walkHeld[4] = {0, 0, 0, 0}; // what the mod is holding down, one for each way

        void SendKey(int vk, bool down)
        {
            INPUT event{};
            event.type = INPUT_KEYBOARD;
            event.ki.wVk = static_cast<WORD>(vk);
            event.ki.wScan = static_cast<WORD>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
            const bool extended = vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT;
            event.ki.dwFlags = (extended ? KEYEVENTF_EXTENDEDKEY : 0u) | (down ? 0u : KEYEVENTF_KEYUP);
            SendInput(1, &event, sizeof(event));
        }

        bool HoldingWalkKey(int vk)
        {
            return vk != 0 && (g_walkHeld[0] == vk || g_walkHeld[1] == vk || g_walkHeld[2] == vk || g_walkHeld[3] == vk);
        }

        bool CursorMoved()
        {
            POINT cursor{};
            if (!GetCursorPos(&cursor)) return false;
            const bool moved = g_haveCursor && (cursor.x != g_lastCursor.x || cursor.y != g_lastCursor.y);
            g_lastCursor = cursor;
            g_haveCursor = true;
            return moved;
        }

        void PollActivity(float)
        {
            if (!GameWindowInForeground()) return;
            if (AnyKeyDown(0x8001) || CursorMoved() || AnyPadInput()) g_lastInputAt.store(NowMs(), std::memory_order_relaxed);
        }
    }

    void InstallActivityTracker()
    {
        gamethread::AddPoller(L"input.activity", &PollActivity);
    }

    long long MsSinceInput()
    {
        const long long last = g_lastInputAt.load(std::memory_order_relaxed);
        if (last == 0) return 1'000'000;
        return NowMs() - last;
    }

    bool InputHeld()
    {
        return GameWindowInForeground() && (AnyKeyDown(0x8000) || AnyPadInput());
    }

    bool MovementHeld()
    {
        // The keys the game itself moves the character with, so a remapped set counts too.
        static std::vector<int> keys;
        if (keys.empty())
        {
            for (const wchar_t* axis : {L"MovementX", L"MovementY"})
            {
                for (const auto& key : MappingKeys(L"AxisMappings", L"AxisName", axis))
                {
                    const int vk = VirtualKeyOf(key);
                    if (vk != 0 && std::find(keys.begin(), keys.end(), vk) == keys.end()) keys.push_back(vk);
                }
            }
        }
        if (!GameWindowInForeground()) return false;
        for (const int vk : keys)
        {
            if (!HoldingWalkKey(vk) && (GetAsyncKeyState(vk) & 0x8000)) return true;
        }
        return LeftStickPushed();
    }

    bool HoldWalkKeys(double forward, double right)
    {
        const WalkKeys& keys = MovementKeys();
        if (!GameWindowInForeground() || (keys.forward == 0 && keys.right == 0))
        {
            ReleaseWalkKeys();
            return false;
        }
        // A key goes down for each way the heading leans far enough toward, which gives the
        // eight headings a player has with the same keys.
        constexpr double kLean = 0.38;
        const int wanted[4] = {forward > kLean ? keys.forward : 0, forward < -kLean ? keys.back : 0, right < -kLean ? keys.left : 0,
                               right > kLean ? keys.right : 0};
        for (int i = 0; i < 4; ++i)
        {
            if (g_walkHeld[i] == wanted[i]) continue;
            if (g_walkHeld[i] != 0) SendKey(g_walkHeld[i], false);
            if (wanted[i] != 0) SendKey(wanted[i], true);
            g_walkHeld[i] = wanted[i];
        }
        return true;
    }

    std::wstring DescribeWalkKeys()
    {
        const WalkKeys& keys = MovementKeys();
        const auto name = [](int vk)
        {
            wchar_t buffer[64] = {};
            const bool extended = vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT;
            const LONG code = static_cast<LONG>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC)) << 16 | (extended ? (1L << 24) : 0);
            if (vk != 0 && GetKeyNameTextW(code, buffer, 64) > 0) return std::wstring(buffer);
            return vk == 0 ? std::wstring(L"<none>") : std::to_wstring(vk);
        };
        return L"the character walks with forward " + name(keys.forward) + L", back " + name(keys.back) + L", left " + name(keys.left) + L", right " +
               name(keys.right);
    }

    void ReleaseWalkKeys()
    {
        for (int& vk : g_walkHeld)
        {
            if (vk != 0) SendKey(vk, false);
            vk = 0;
        }
    }
}
