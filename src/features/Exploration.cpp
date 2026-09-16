#include "features/Exploration.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <map>
#include <numbers>
#include <string>
#include <vector>

namespace qa::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::FProperty;
    using RC::Unreal::UObject;

    namespace
    {
        struct Vec
        {
            double x = 0.0, y = 0.0, z = 0.0;
        };

        // Something the character can walk to: a use location (the interactable itself, with
        // the verb the game shows for it), or a place of the scene. A use location and the
        // place next to it are one target, named "place: verb".
        struct Target
        {
            UObject* actor = nullptr; // the actor to reach: the use location, else the place
            UObject* place = nullptr; // the place merged into a use location, if any
            bool destination = false; // true when the target is a place alone
            bool way = false;         // true when it is a way on that the scene is watching for
            std::wstring label;
            Vec position;
            double distance = 0.0;  // straight line from the character, centimetres
            double bearing = 0.0;   // degrees to the right of the camera's forward
            bool inRange = false;   // the character stands in the use location
            double navRadius = 0.0; // the radius a use location counts as reached in, when known
            bool reached = false;
            // A way on is a volume: the box its collision fills, whether the character stands
            // inside it, and whether it lies wholly above or below the character.
            bool hasBox = false;
            Vec boxLow;
            Vec boxHigh;
            bool inside = false;
            int storey = 0;
            std::wstring verb; // what the game says can be done here, when it says anything
            // The walkable way there, from the navigation mesh: how far it is on foot and
            // the corner to head for now. Everything said about a target uses these when
            // they are known, so the list, the beacon and the walking agree.
            bool hasRoute = false;
            double routeAt = -1000.0; // when the mesh was last asked
            double routeLength = 0.0; // centimetres along the way
            Vec routeNext;            // the corner to walk to now
            Vec routeFrom;            // where the character was when it was asked
            // The mesh's way runs through a place the scene is waiting on that the target is
            // not beyond, so it is not taken and the target is walked to by sight.
            bool routeBarred = false;
            UObject* barredBy = nullptr;
            // The use location a place names, when it names one.
            std::wstring namedUse;
        };

        // What the navigation mesh answers for one question.
        struct Route
        {
            bool valid = false;   // the mesh gave a way through
            bool partial = false; // it stops short of the target
            double length = 0.0;
            Vec next;
            Vec goal; // the spot on the ground it was asked to reach
            Vec end;  // where it actually stops
        };

        // What the scene last said about one of its use locations. The game can flick one on
        // and off many times a second while the character stands in it, so the moment it was
        // last on is kept as well.
        struct UseLocationState
        {
            int64_t state = 0;
            double availableAt = -1000.0;
            double loggedAt = -1000.0;
        };

        std::vector<Target> g_targets; // in a stable order: the earlier a target came, the earlier it stays
        UObject* g_selected = nullptr;
        std::map<UObject*, UseLocationState> g_useLocationState;
        UObject* g_pawn = nullptr;
        double g_leftAt = -1000.0; // when the player last lost control
        Vec g_leftAtPosition;
        bool g_exploring = false;
        bool g_inLoco = false;
        double g_hintDueAt = -1.0;     // when the keys are to be said, after the game's own word about the stick
        double g_hintSaidAt = -1000.0; // when they were last said
        bool g_beacon = true;
        double g_beaconAt = -10.0;
        double g_destinationsScannedAt = -10.0;
        std::vector<UObject*> g_destinations;
        std::vector<UObject*> g_ways;
        double g_waysScannedAt = -10.0;
        std::vector<UObject*> g_waysHidden; // ways that lie along the road to something named
        double g_waysFilteredAt = -10.0;
        std::vector<std::wstring> g_waysSpent; // volumes the scene has moved past, as last logged
        std::vector<UObject*> g_waysAnnounced; // ways already said while the scene keeps watching them
        double g_stepHeight = -1.0;            // the step the navigation mesh lets a character climb
        double g_stepHeightAt = -10.0;
        std::vector<UObject*> g_glints;
        double g_glintsScannedAt = -10.0;
        UObject* g_static = nullptr;
        UObject* g_readingPane = nullptr;
        std::wstring g_pageText;
        std::wstring g_pageRead;
        int g_pageStable = 0;
        bool g_destinationPrompt = false; // the game offered its own place navigation
        // Widgets of the game's own mechanics: while one of them is on screen, exploration
        // stands aside and leaves the keys to it.
        std::vector<UObject*> g_mechanicWidgets;
        // Walking to the target by itself.
        bool g_walking = false;
        double g_walkStartedAt = 0.0;
        double g_walkMovingAt = 0.0;  // when the character was last seen actually moving
        double g_walkLean = 0.0;      // degrees the heading leans while feeling for a way past
        double g_walkBestWay = 1e9;   // the shortest the way there has been on this walk
        double g_walkBestWayAt = 0.0; // when it last grew shorter
        double g_walkLeanedAt = 0.0;
        double g_walkLoggedAt = 0.0;
        Vec g_walkLastPosition;
        double g_cameraYaw = 0.0;        // to notice the game cutting to another camera
        UObject* g_currentUse = nullptr; // the use location the game is offering right now

        // ---- geometry ---------------------------------------------------------------

        bool ReadVec(void* structPtr, FProperty* structProp, Vec& out)
        {
            if (!structPtr || !structProp) return false;
            return obj::ReadFloatAt(structPtr, obj::StructMember(structProp, L"X"), out.x) &&
                   obj::ReadFloatAt(structPtr, obj::StructMember(structProp, L"Y"), out.y) &&
                   obj::ReadFloatAt(structPtr, obj::StructMember(structProp, L"Z"), out.z);
        }

        void WriteVec(void* structPtr, FProperty* structProp, const Vec& v)
        {
            if (!structPtr || !structProp) return;
            const struct
            {
                const wchar_t* name;
                double value;
            } members[] = {{L"X", v.x}, {L"Y", v.y}, {L"Z", v.z}};
            for (const auto& m : members)
            {
                auto* prop = obj::StructMember(structProp, m.name);
                if (!prop) continue;
                const auto type = obj::PropertyTypeName(prop);
                if (type == L"FloatProperty")
                    *static_cast<float*>(obj::ValuePtrAt(structPtr, prop)) = static_cast<float>(m.value);
                else if (type == L"DoubleProperty")
                    *static_cast<double*>(obj::ValuePtrAt(structPtr, prop)) = m.value;
            }
        }

        bool ActorLocation(UObject* actor, Vec& out)
        {
            bool ok = false;
            obj::CallReturn(actor, L"K2_GetActorLocation",
                            [&](void* params, FProperty* returnValue) { ok = ReadVec(obj::ValuePtrAt(params, returnValue), returnValue, out); });
            return ok;
        }

        bool YawOf(UObject* target, const wchar_t* function, double& out)
        {
            bool ok = false;
            obj::CallReturn(target, function,
                            [&](void* params, FProperty* returnValue)
                            {
                                void* value = obj::ValuePtrAt(params, returnValue);
                                if (value) ok = obj::ReadFloatAt(value, obj::StructMember(returnValue, L"Yaw"), out);
                            });
            return ok;
        }

        double NormalizeDegrees(double a)
        {
            a = std::fmod(a + 180.0, 360.0);
            if (a < 0) a += 360.0;
            return a - 180.0;
        }

        // Degrees to the right of a heading, from one point to another (the engine's yaw
        // turns clockwise seen from above).
        double BearingDegrees(const Vec& from, const Vec& to, double headingYaw)
        {
            const double toYaw = std::atan2(to.y - from.y, to.x - from.x) * 180.0 / std::numbers::pi;
            return NormalizeDegrees(toYaw - headingYaw);
        }

        double Distance(const Vec& a, const Vec& b)
        {
            return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
        }

        double FlatDistance(const Vec& a, const Vec& b)
        {
            return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
        }

        const wchar_t* SectorKey(double bearing)
        {
            const int sector = (static_cast<int>(std::lround(bearing / 45.0)) % 8 + 8) % 8;
            static const wchar_t* const keys[] = {L"dir8.ahead",  L"dir8.aheadright", L"dir8.right", L"dir8.behindright",
                                                  L"dir8.behind", L"dir8.behindleft", L"dir8.left",  L"dir8.aheadleft"};
            return keys[sector];
        }

        std::wstring Metres(double centimetres)
        {
            return std::to_wstring(std::max<long>(1, std::lround(centimetres / 100.0)));
        }

        // ---- the scene ------------------------------------------------------------------

        UObject* Pawn()
        {
            UObject* controller = obj::LocalPlayerController();
            UObject* pawn = nullptr;
            if (controller) obj::ReadObject(controller, L"Pawn", pawn);
            return obj::IsLive(pawn) ? pawn : nullptr;
        }

        bool CameraYaw(double& yaw)
        {
            UObject* controller = obj::LocalPlayerController();
            UObject* camera = nullptr;
            if (controller && obj::ReadObject(controller, L"PlayerCameraManager", camera) && obj::IsLive(camera))
                return YawOf(camera, L"GetCameraRotation", yaw);
            return false;
        }

        // The name an actor carries in the level, read as words: the prefix of its kind and
        // the copy number are dropped, and the run-together words are parted where the
        // capitals allow it ("UL_CampMap" -> "Camp Map"). It is all there is to say about a
        // thing the game gives no text for.
        std::wstring TechnicalName(UObject* actor)
        {
            std::wstring name = obj::ObjectName(actor);
            for (const wchar_t* prefix : {L"UL_", L"UI_", L"EDI_", L"EDN_", L"ED_", L"EDG_", L"BP_"})
            {
                if (str::StartsWith(name, prefix)) name.erase(0, wcslen(prefix));
            }
            while (!name.empty() && (iswdigit(name.back()) || name.back() == L'_'))
                name.pop_back();
            std::wstring out;
            for (size_t i = 0; i < name.size(); ++i)
            {
                const wchar_t c = name[i];
                if (c == L'_')
                {
                    out += L' ';
                    continue;
                }
                if (i > 0 && iswupper(c))
                {
                    const wchar_t previous = name[i - 1];
                    const bool opensWord = !iswupper(previous);
                    const bool closesRun = iswupper(previous) && i + 1 < name.size() && iswlower(name[i + 1]);
                    if (opensWord || closesRun) out += L' ';
                }
                out += c;
            }
            return str::CollapseWhitespace(out);
        }

        // The words the game shows over a use location: one of the few verbs it has, never
        // the name of the thing.
        std::wstring UseLocationVerb(UObject* actor)
        {
            UObject* data = nullptr;
            if (obj::ReadObject(actor, L"UseLocationData", data) && obj::IsLive(data))
                return str::CollapseWhitespace(str::StripMarkup(gametext::ReadLocalized(data, L"PromptLabel")));
            return {};
        }

        // What the thing is called: the text the scene gave this use location, else the name
        // it carries in the level.
        std::wstring UseLocationName(UObject* actor)
        {
            const auto own = str::CollapseWhitespace(str::StripMarkup(gametext::ReadLocalized(actor, L"LocaleString")));
            return own.empty() ? TechnicalName(actor) : own;
        }

        // "Name: verb", or whichever half there is.
        std::wstring ComposeLabel(const std::wstring& name, const std::wstring& verb)
        {
            if (name.empty()) return verb;
            if (verb.empty() || name == verb) return name;
            return locale::Mod(L"explore.merged", name, verb);
        }

        std::wstring UseLocationLabel(UObject* actor)
        {
            return ComposeLabel(UseLocationName(actor), UseLocationVerb(actor));
        }

        // The flow names actors by text: "Ul_Stream", "trigger_backtopath".
        bool ReadActorReference(UObject* object, const wchar_t* field, std::wstring& out)
        {
            auto* prop = obj::FindProperty(object, field);
            void* ptr = prop ? obj::ValuePtr(object, prop) : nullptr;
            return ptr && obj::ReadStringAt(ptr, obj::StructMember(prop, L"ActorName"), out);
        }

        // Whether an actor is the one a flow calls by this name. A flow names actors by the
        // name in their register, the name they were given in the editor, and a cooked level
        // can number the actor itself apart from it: the chest of the prologue woods is the
        // actor UL_FindOldTrunk_2 registered as UL_FindOldTrunk, and the game turns it on by
        // the register. So the register is what a reference is matched against, and the
        // actor's own name serves where the two are one.
        bool IsNamed(UObject* actor, const std::wstring& named)
        {
            if (!obj::IsLive(actor)) return false;
            if (obj::ObjectName(actor) == named) return true;
            std::wstring registered;
            return ReadActorReference(actor, L"ActorRegister", registered) && registered == named;
        }

        enum class Verdict
        {
            Holds,
            Fails,
            Unknown
        };

        // Asks the game's blackboard library about one of the scene's variables, handing it the
        // reference exactly as the condition holds it. False when the library, the function or
        // a matching parameter is missing, so nothing is called with a reference it cannot use.
        bool AskBlackboard(UObject* pawn, const wchar_t* function, FProperty* reference, void* source, bool& out)
        {
            UObject* library = obj::FindObject(L"/Script/SMGGameFlow.Default__GFBlackboardBlueprintLibrary");
            auto* fn = library ? obj::FindFunction(library, function) : nullptr;
            if (!fn) return false;
            bool fits = false;
            for (auto* prop : fn->ForEachProperty())
            {
                if (prop && prop->GetName() == L"VariableRef" && prop->GetSize() == reference->GetSize()) fits = true;
            }
            if (!fits) return false;
            bool answered = false;
            obj::Call(
                library, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"WorldContextObject")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = pawn;
                        else if (name == L"VariableRef")
                            std::memcpy(obj::ValuePtrAt(params, prop), source, static_cast<size_t>(reference->GetSize()));
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") answered = obj::ReadBoolAt(params, prop, out);
                    }
                });
            return answered;
        }

        // What one of a scene's own conditions says right now, for the kinds the game lets
        // anyone read: a value that is true or false, and a flag that is raised or lowered, both
        // kept on the scene's blackboard. Every other kind is left unjudged. `said` names the
        // variable and what it holds.
        Verdict BlackboardCondition(UObject* pawn, UObject* condition, std::wstring& said)
        {
            const wchar_t* exists = nullptr;
            const wchar_t* read = nullptr;
            if (obj::IsA(condition, L"GFConditionBlackboardBoolCheck"))
            {
                exists = L"DoesBlackboardBoolExist";
                read = L"GetGlobalBlackboardBool";
            }
            else if (obj::IsA(condition, L"GFConditionBlackboardFlagCheck"))
            {
                exists = L"DoesBlackboardFlagExist";
                read = L"GetGlobalBlackboardFlag";
            }
            if (!read) return Verdict::Unknown;
            FProperty* reference = obj::FindProperty(condition, L"Variable");
            void* source = reference ? obj::ValuePtr(condition, reference) : nullptr;
            if (!source) return Verdict::Unknown;
            UObject* variable = nullptr;
            obj::ReadObjectAt(source, obj::StructMember(reference, L"Variable"), variable);
            if (!obj::IsLive(variable)) return Verdict::Unknown;
            bool present = false;
            if (!AskBlackboard(pawn, exists, reference, source, present) || !present) return Verdict::Unknown;
            bool value = false;
            if (!AskBlackboard(pawn, read, reference, source, value)) return Verdict::Unknown;
            bool inverted = false;
            obj::ReadBool(condition, L"bNot", inverted);
            said = obj::ObjectName(variable) + (value ? L" is set" : L" is not set");
            return value != inverted ? Verdict::Holds : Verdict::Fails;
        }

        // The ways on that the scene is watching for right now. A scene often moves only when
        // the character walks into a place its own logic waits on, and those places carry no
        // marker, no glint and no name on screen for anyone. What a player does see is the
        // ground leading to them, so they are offered by where they are and never by what they
        // are called. They are found by asking the flow: the action that turned on a use
        // location the character can see belongs to a state, and that state's transitions say
        // which volumes it is waiting on. A transition also carries the scene's own memory of
        // what has already happened: one whose flag says its moment has passed can no longer
        // fire, so walking into its volume sets nothing off and the volume is left out.
        std::vector<UObject*> WayVolumes(UObject* pawn, const std::vector<UObject*>& availableUses)
        {
            std::vector<std::wstring> wanted;
            std::vector<std::wstring> spent;
            std::vector<UObject*> statesSeen;
            for (auto* action : obj::FindAllLive(L"GFActionMakeUseLocationAvailable"))
            {
                std::wstring named;
                if (!ReadActorReference(action, L"UseLocationName", named)) continue;
                if (std::none_of(availableUses.begin(), availableUses.end(), [&](UObject* use) { return IsNamed(use, named); })) continue;
                UObject* schema = obj::Outer(action);
                UObject* state = schema ? obj::Outer(schema) : nullptr;
                if (!obj::IsLive(state)) continue;
                if (std::find(statesSeen.begin(), statesSeen.end(), state) != statesSeen.end()) continue;
                statesSeen.push_back(state);
                std::vector<UObject*> transitions;
                obj::ReadObjectArray(state, L"Transitions", transitions);
                for (auto* transition : transitions)
                {
                    if (!obj::IsLive(transition)) continue;
                    std::vector<UObject*> conditions;
                    obj::ReadObjectArray(transition, L"Conditions", conditions);
                    std::vector<std::wstring> volumes;
                    std::wstring closedBy;
                    for (auto* condition : conditions)
                    {
                        if (!obj::IsLive(condition)) continue;
                        std::wstring volume;
                        if (obj::IsA(condition, L"GFConditionInVolume"))
                        {
                            if (ReadActorReference(condition, L"TriggerVolumeName", volume)) volumes.push_back(volume);
                            continue;
                        }
                        std::wstring said;
                        if (closedBy.empty() && BlackboardCondition(pawn, condition, said) == Verdict::Fails) closedBy = said;
                    }
                    for (const auto& volume : volumes)
                    {
                        if (closedBy.empty())
                            wanted.push_back(volume);
                        else
                            spent.push_back(volume + L" (" + closedBy + L")");
                    }
                }
            }
            std::sort(spent.begin(), spent.end());
            spent.erase(std::unique(spent.begin(), spent.end()), spent.end());
            if (spent != g_waysSpent)
            {
                g_waysSpent = spent;
                if (!spent.empty()) log::Info(L"explore: the scene has moved past {}", str::Join(spent, L", "));
            }
            std::vector<UObject*> out;
            if (wanted.empty()) return out;
            for (auto* volume : obj::FindAllLive(L"TriggerVolumeSMG"))
            {
                if (std::any_of(wanted.begin(), wanted.end(), [&](const std::wstring& name) { return IsNamed(volume, name); })) out.push_back(volume);
            }
            if (out.size() != g_ways.size())
            {
                std::vector<std::wstring> names;
                for (auto* volume : out)
                    names.push_back(obj::ObjectName(volume));
                log::Info(L"explore: the scene is watching for {} of {} named places: {}", out.size(), wanted.size(), str::Join(names, L", "));
            }
            return out;
        }

        std::wstring DestinationLabel(UObject* actor)
        {
            const auto label = str::CollapseWhitespace(str::StripMarkup(gametext::ReadLocalized(actor, L"PromptLabel")));
            return label.empty() ? TechnicalName(actor) : label;
        }

        // A place counts once the game would show it: available ones always, discoverable
        // ones once discovered; a used one is done with.
        bool DestinationShown(UObject* actor)
        {
            bool used = false;
            obj::ReadBool(actor, L"bIsUsed", used);
            if (used) return false;
            int64_t type = 0;
            obj::ReadInt(actor, L"DestinationType", type);
            bool discovered = false;
            obj::ReadBool(actor, L"bIsDiscovered", discovered);
            return type == 1 || discovered;
        }

        Route FindRoute(UObject* pawn, const Vec& here, const Target& target, std::vector<Vec>* road);
        bool TriggerBox(UObject* actor, Vec& low, Vec& high);
        double DistanceToBox(const std::vector<Vec>& road, const Vec& low, const Vec& high);
        double WalkingRadius(UObject* pawn);
        bool CapsuleSpan(UObject* pawn, Vec& middle, double& halfHeight);
        bool PointInVolume(UObject* volume, const Vec& point, const Vec& low, const Vec& high);
        bool InsideVolume(UObject* pawn, UObject* volume, const Vec& low, const Vec& high, int& storey);
        UObject* WayBarring(UObject* pawn, const Vec& here, const std::vector<Vec>& road, const Target& target);

        std::vector<Target> Gather(UObject* pawn, const Vec& here, double yaw, double now)
        {
            std::vector<Target> uses;
            std::vector<Target> places;
            const double range = cfg::Get().exploreRange * 100.0;
            std::vector<UObject*> available;
            std::vector<UObject*> overlapping;
            obj::ReadObjectArray(pawn, L"AvailableUseLocations", available);
            obj::ReadObjectArray(pawn, L"OverlappingUseLocations", overlapping);
            UObject* offered = nullptr;
            obj::ReadObject(pawn, L"CurrentUseLocation", offered);
            // What the scene has turned on is what it told the client, and only that is
            // listed, since that is what the glints show. The character's own list holds
            // whatever stands within reach, turned on or not: the ticket stub in the lodge,
            // which its scene never turned on, came into it whenever the character stood on
            // it, and the way out of a room comes into it seconds before the scene turns it
            // on. Which of several things standing together the game is offering counts as
            // on as well.
            const auto on = [&](UObject* actor)
            {
                if (actor == offered) return true;
                const auto it = g_useLocationState.find(actor);
                if (it == g_useLocationState.end()) return false;
                return it->second.state == 1 || now - it->second.availableAt < 2.0;
            };
            for (const auto& [actor, state] : g_useLocationState)
            {
                if (on(actor) && obj::IsLive(actor) && std::find(available.begin(), available.end(), actor) == available.end()) available.push_back(actor);
            }
            for (auto* actor : available)
            {
                if (!obj::IsLive(actor) || !on(actor)) continue;
                Target t;
                t.actor = actor;
                if (!ActorLocation(actor, t.position)) continue;
                if (Distance(here, t.position) > range) continue;
                t.verb = UseLocationVerb(actor);
                t.label = ComposeLabel(UseLocationName(actor), t.verb);
                obj::ReadFloat(actor, L"NavigationRadius", t.navRadius);
                t.inRange = std::find(overlapping.begin(), overlapping.end(), actor) != overlapping.end();
                uses.push_back(std::move(t));
            }
            // Place actors do not move; the scan of all objects is repeated only now and then.
            if (now - g_destinationsScannedAt > 2.0)
            {
                g_destinations = obj::FindAllLive(L"ExplorationDestinationSMG026");
                g_destinationsScannedAt = now;
            }
            for (auto* actor : g_destinations)
            {
                if (!obj::IsLive(actor) || !DestinationShown(actor)) continue;
                Target t;
                t.actor = actor;
                t.destination = true;
                if (!ActorLocation(actor, t.position)) continue;
                if (Distance(here, t.position) > range) continue;
                t.label = DestinationLabel(actor);
                ReadActorReference(actor, L"UseLocation", t.namedUse);
                places.push_back(std::move(t));
            }

            // The ways on the scene is watching for, which are what a player walks toward
            // when there is nothing left to use.
            if (now - g_waysScannedAt > 3.0)
            {
                g_ways = WayVolumes(pawn, available);
                g_waysScannedAt = now;
                std::erase_if(g_waysAnnounced, [](UObject* way) { return std::find(g_ways.begin(), g_ways.end(), way) == g_ways.end(); });
            }
            std::vector<Target> ways;
            for (auto* volume : g_ways)
            {
                if (!obj::IsLive(volume)) continue;
                Target t;
                t.actor = volume;
                t.destination = true;
                t.way = true;
                // A volume's pivot can stand metres away from the space it fills, so a way is
                // where its collision is: the middle of that box.
                if (TriggerBox(volume, t.boxLow, t.boxHigh))
                {
                    t.hasBox = true;
                    t.position = Vec{(t.boxLow.x + t.boxHigh.x) / 2.0, (t.boxLow.y + t.boxHigh.y) / 2.0, (t.boxLow.z + t.boxHigh.z) / 2.0};
                }
                else if (!ActorLocation(volume, t.position))
                {
                    continue;
                }
                if (Distance(here, t.position) > range) continue;
                t.label = locale::Mod(L"explore.wayon");
                if (t.hasBox) t.inside = InsideVolume(pawn, volume, t.boxLow, t.boxHigh, t.storey);
                ways.push_back(std::move(t));
            }

            // A use location takes the place standing next to it as its name, and twins of
            // the same thing (two places or two use locations with one label at one spot)
            // are listed once.
            std::vector<Target> out;
            std::vector<bool> placeTaken(places.size(), false);
            // Closest pairs first, so several things standing on one desk each take the place
            // that is really theirs rather than whichever the character happened to meet first.
            struct Pairing
            {
                size_t use = 0;
                size_t place = 0;
                double distance = 0.0;
            };
            std::vector<Pairing> pairings;
            for (size_t u = 0; u < uses.size(); ++u)
            {
                for (size_t i = 0; i < places.size(); ++i)
                {
                    const double d = FlatDistance(places[i].position, uses[u].position);
                    if (d < 250.0) pairings.push_back({u, i, d});
                }
            }
            std::sort(pairings.begin(), pairings.end(), [](const Pairing& a, const Pairing& b) { return a.distance < b.distance; });
            std::vector<size_t> placeOfUse(uses.size(), places.size());
            // A place that names its use location is that use location's name and nothing
            // else: it is listed with it, and not at all while the use location is off, since
            // the game shows no glint there then. The crash site's trunk has two such places,
            // one for each time the trunk is opened up, and one of them stood in the list as
            // a second "Trunk" beside "Trunk: Examine".
            for (size_t i = 0; i < places.size(); ++i)
            {
                if (places[i].namedUse.empty()) continue;
                placeTaken[i] = true;
                for (size_t u = 0; u < uses.size(); ++u)
                {
                    if (placeOfUse[u] < places.size() || !IsNamed(uses[u].actor, places[i].namedUse)) continue;
                    placeOfUse[u] = i;
                    break;
                }
            }
            for (const auto& pairing : pairings)
            {
                if (placeTaken[pairing.place] || placeOfUse[pairing.use] < places.size()) continue;
                placeTaken[pairing.place] = true;
                placeOfUse[pairing.use] = pairing.place;
            }
            for (size_t u = 0; u < uses.size(); ++u)
            {
                if (std::any_of(out.begin(), out.end(), [&](const Target& o)
                                { return !o.destination && o.label == uses[u].label && FlatDistance(o.position, uses[u].position) < 150.0; }))
                    continue;
                if (placeOfUse[u] < places.size())
                {
                    const Target& place = places[placeOfUse[u]];
                    uses[u].place = place.actor;
                    // The place names the thing, the use location says what can be done to it.
                    uses[u].label = ComposeLabel(place.label, uses[u].verb);
                }
                out.push_back(std::move(uses[u]));
            }
            for (size_t i = 0; i < places.size(); ++i)
            {
                if (placeTaken[i]) continue;
                const auto& p = places[i];
                if (std::any_of(out.begin(), out.end(), [&](const Target& o) { return o.label == p.label && FlatDistance(o.position, p.position) < 150.0; }))
                    continue;
                out.push_back(p);
            }
            // A way the character walks through on its road to something the scene already
            // names is not offered: that journey sets it off anyway, and saying both makes two
            // entries out of one. The test is the game's own: the road from the navigation
            // mesh against the box the trigger actually occupies, widened by the character's
            // own width, so a way is dropped only where walking to something else really does
            // enter it. A way no road enters, whether it stands to one side of them all or
            // beyond where every one of them ends, is a journey of its own and is offered.
            if (!ways.empty() && now - g_waysFilteredAt > 3.0)
            {
                g_waysFilteredAt = now;
                g_waysHidden.clear();
                std::vector<std::vector<Vec>> roads;
                size_t asked = 0;
                for (const auto& t : out)
                {
                    if (++asked > 10) break;
                    std::vector<Vec> road;
                    FindRoute(pawn, here, t, &road);
                    // A road the walk will not take, because it runs through a way the target
                    // is not beyond, sets nothing off and hides nothing.
                    if (road.size() >= 2 && !WayBarring(pawn, here, road, t)) roads.push_back(std::move(road));
                }
                const double radius = WalkingRadius(pawn);
                for (const auto& way : ways)
                {
                    Vec low, high;
                    if (!TriggerBox(way.actor, low, high))
                    {
                        // Without the trigger's own box there is nothing to say a road goes
                        // through it, so it stays on offer rather than being dropped on a guess.
                        log::Info(L"explore: a way {:.0f} cm off keeps no box of its own and is offered", Distance(here, way.position));
                        continue;
                    }
                    low = Vec{low.x - radius, low.y - radius, low.z};
                    high = Vec{high.x + radius, high.y + radius, high.z};
                    double nearest = 1e9;
                    for (const auto& road : roads)
                        nearest = std::min(nearest, DistanceToBox(road, low, high));
                    // The way the player has chosen stays in the list: taking it out as the
                    // character moves would end the walk to it halfway.
                    const bool chosen = way.actor == g_selected;
                    const bool onRoad = nearest <= 0.0 && !chosen;
                    if (onRoad) g_waysHidden.push_back(way.actor);
                    log::Info(L"explore: a way {:.0f} cm off, {:.0f} by {:.0f} cm across, is missed by the nearest road by {:.0f} cm and is {}",
                              Distance(here, way.position), high.x - low.x, high.y - low.y, nearest,
                              onRoad ? L"not offered" : (nearest <= 0.0 ? L"kept as the chosen target" : L"offered"));
                }
            }
            for (auto& way : ways)
            {
                if (std::find(g_waysHidden.begin(), g_waysHidden.end(), way.actor) != g_waysHidden.end()) continue;
                out.push_back(std::move(way));
            }
            for (auto& t : out)
            {
                t.distance = Distance(here, t.position);
                t.bearing = BearingDegrees(here, t.position, yaw);
            }
            return out;
        }

        // The known targets keep their order and take the fresh readings; the new ones join
        // at the end, nearest first. Returns the new ones.
        std::vector<Target> Reconcile(std::vector<Target>& fresh)
        {
            std::vector<Target> kept;
            for (auto& old : g_targets)
            {
                auto it = std::find_if(fresh.begin(), fresh.end(), [&](const Target& f) { return f.actor == old.actor; });
                if (it == fresh.end()) continue;
                it->reached = old.reached;
                it->hasRoute = old.hasRoute;
                it->routeAt = old.routeAt;
                it->routeLength = old.routeLength;
                it->routeNext = old.routeNext;
                it->routeFrom = old.routeFrom;
                it->routeBarred = old.routeBarred;
                it->barredBy = old.barredBy;
                kept.push_back(*it);
                it->actor = nullptr; // consumed
            }
            std::vector<Target> added;
            for (auto& f : fresh)
            {
                if (f.actor) added.push_back(f);
            }
            std::sort(added.begin(), added.end(), [](const Target& a, const Target& b) { return a.distance < b.distance; });
            g_targets = kept;
            g_targets.insert(g_targets.end(), added.begin(), added.end());
            return added;
        }

        // Being there. For a use location this is the game's own answer and nothing else:
        // it is reached once the game offers it, or once the character stands within its
        // trigger while the game offers nothing there. Two of them can share a patch of
        // floor (in Laura's cell the loose brick that hides the syringe and the cot beside
        // it have overlapping triggers), and the game offers the nearer of them, so
        // standing in the trigger of one while the other is offered is not being there yet:
        // a press would use the other. A way on is reached once the character stands inside
        // its volume, by the engine's own test against that volume's collision. A place of
        // the scene is only a point on the ground with nothing to press, so being near it is
        // arriving.
        bool AtTarget(const Target& t, UObject* offered)
        {
            if (t.way && t.hasBox) return t.inside;
            if (t.destination) return t.distance < 150.0;
            if (t.actor == offered) return true;
            return t.inRange && offered == nullptr;
        }

        Target* Selected()
        {
            for (auto& t : g_targets)
            {
                if (t.actor == g_selected) return &t;
            }
            return nullptr;
        }

        // ---- routes over the navigation mesh -----------------------------------------------

        UObject* NavigationSystem()
        {
            return obj::FindObject(L"/Script/NavigationSystem.Default__NavigationSystemV1");
        }

        // The step the scene's navigation mesh was built to let a character climb, which is also
        // the most one stretch of floor can rise or fall without becoming another floor.
        double StepHeight(double now)
        {
            if (now - g_stepHeightAt < 5.0) return g_stepHeight;
            g_stepHeightAt = now;
            g_stepHeight = -1.0;
            for (auto* mesh : obj::FindAllLive(L"RecastNavMesh"))
            {
                double height = 0.0;
                if (obj::ReadFloat(mesh, L"AgentMaxStepHeight", height) && height > 0.0)
                {
                    g_stepHeight = height;
                    break;
                }
            }
            return g_stepHeight;
        }

        // The point on the navigation mesh nearest to a point, when there is one close by.
        bool ProjectToMesh(UObject* pawn, const Vec& point, Vec& out, const Vec& extent)
        {
            UObject* nav = NavigationSystem();
            auto* fn = nav ? obj::FindFunction(nav, L"K2_ProjectPointToNavigation") : nullptr;
            if (!fn) return false;
            bool ok = false;
            obj::Call(
                nav, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"WorldContextObject")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = pawn;
                        else if (name == L"Point")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, point);
                        else if (name == L"QueryExtent")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, extent);
                    }
                },
                [&](void* params)
                {
                    bool result = false;
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"ReturnValue")
                            obj::ReadBoolAt(params, prop, result);
                        else if (name == L"ProjectedLocation")
                            ReadVec(obj::ValuePtrAt(params, prop), prop, out);
                    }
                    ok = result;
                });
            return ok;
        }

        Route FindRoute(UObject* pawn, const Vec& here, const Target& target, std::vector<Vec>* road)
        {
            Route route;
            UObject* nav = NavigationSystem();
            auto* fn = nav ? obj::FindFunction(nav, L"FindPathToLocationSynchronously") : nullptr;
            if (!fn) return route;
            Vec end = target.position;
            Vec projected;
            // The engine settles on the ground nearest across, whatever its height within the
            // search, so a search that takes in another floor can hand back that floor: a thing
            // on a wall above lower ground sends the character to the ground behind the wall.
            // Things stand on the floor they are used from, so that floor is looked for first,
            // within the step the mesh lets a character climb, and only then further up and down.
            const double step = StepHeight(gamethread::NowSeconds());
            // A way is walked into rather than stood beside, so its ground is looked for inside
            // the box its collision fills before anywhere else.
            const Vec half{(target.boxHigh.x - target.boxLow.x) / 2.0, (target.boxHigh.y - target.boxLow.y) / 2.0, (target.boxHigh.z - target.boxLow.z) / 2.0};
            if ((target.way && target.hasBox && ProjectToMesh(pawn, end, projected, half)) ||
                (step > 0.0 && ProjectToMesh(pawn, end, projected, Vec{250.0, 250.0, step})) || ProjectToMesh(pawn, end, projected, Vec{250.0, 250.0, 120.0}) ||
                ProjectToMesh(pawn, end, projected, Vec{250.0, 250.0, 400.0}))
                end = projected;
            UObject* path = nullptr;
            obj::Call(
                nav, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop) continue;
                        const auto name = prop->GetName();
                        if (name == L"WorldContextObject" || name == L"PathfindingContext")
                            *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = pawn;
                        else if (name == L"PathStart")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, here);
                        else if (name == L"PathEnd")
                            WriteVec(obj::ValuePtrAt(params, prop), prop, end);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadObjectAt(params, prop, path);
                    }
                });
            if (!obj::IsLive(path)) return route;
            std::vector<Vec> points;
            obj::ForEachArrayElement(path, obj::FindProperty(path, L"PathPoints"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         Vec p;
                                         if (ReadVec(obj::ValuePtrAt(element, inner), inner, p)) points.push_back(p);
                                     });
            if (points.size() < 2) return route;
            if (road) *road = points;
            route.valid = true;
            route.goal = end;
            route.end = points.back();
            route.partial = obj::CallForBool(path, L"IsPartial");
            for (size_t i = 1; i < points.size(); ++i)
                route.length += Distance(points[i - 1], points[i]);
            // The next point to walk to: the first one that is not already underfoot.
            route.next = points.back();
            for (size_t i = 1; i < points.size(); ++i)
            {
                if (FlatDistance(here, points[i]) > 100.0)
                {
                    route.next = points[i];
                    break;
                }
            }
            log::Verbose(L"explore: route to \"{}\": {} points, {:.0f} cm{}", target.label, points.size(), route.length, route.partial ? L", partial" : L"");
            return route;
        }

        double DistanceToSegment(const Vec& p, const Vec& a, const Vec& b)
        {
            const double abx = b.x - a.x;
            const double aby = b.y - a.y;
            const double length = abx * abx + aby * aby;
            double along = 0.0;
            if (length > 1.0) along = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / length, 0.0, 1.0);
            return FlatDistance(p, Vec{a.x + abx * along, a.y + aby * along, 0.0});
        }

        // The box the game keeps for a trigger, in the world's own coordinates. Volumes are
        // brushes, so their size lives in their collision rather than in any property, and the
        // engine hands it over whole.
        bool TriggerBox(UObject* actor, Vec& low, Vec& high)
        {
            auto* fn = obj::IsLive(actor) ? obj::FindFunction(actor, L"GetActorBounds") : nullptr;
            if (!fn) return false;
            Vec origin{}, extent{};
            bool read = false;
            for (const bool collidingOnly : {true, false})
            {
                obj::Call(
                    actor, fn,
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"bOnlyCollidingComponents") *static_cast<bool*>(obj::ValuePtrAt(params, prop)) = collidingOnly;
                        }
                    },
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (!prop) continue;
                            const auto name = prop->GetName();
                            if (name == L"Origin")
                                read = ReadVec(obj::ValuePtrAt(params, prop), prop, origin);
                            else if (name == L"BoxExtent")
                                ReadVec(obj::ValuePtrAt(params, prop), prop, extent);
                        }
                    });
                if (read && (extent.x > 1.0 || extent.y > 1.0)) break;
            }
            if (!read || (extent.x <= 1.0 && extent.y <= 1.0)) return false;
            low = Vec{origin.x - extent.x, origin.y - extent.y, origin.z - extent.z};
            high = Vec{origin.x + extent.x, origin.y + extent.y, origin.z + extent.z};
            return true;
        }

        // How wide the character is, which is how near a road has to come for the walk to set
        // a trigger off.
        double WalkingRadius(UObject* pawn)
        {
            UObject* capsule = nullptr;
            double radius = 0.0;
            if (obj::ReadObject(pawn, L"CapsuleComponent", capsule) && capsule) obj::ReadFloat(capsule, L"CapsuleRadius", radius);
            return radius;
        }

        // Where the character's capsule stands: its middle and how far it reaches up and down.
        bool CapsuleSpan(UObject* pawn, Vec& middle, double& halfHeight)
        {
            UObject* capsule = nullptr;
            if (!obj::ReadObject(pawn, L"CapsuleComponent", capsule) || !obj::IsLive(capsule)) return false;
            bool placed = false;
            bool sized = false;
            obj::CallReturn(capsule, L"K2_GetComponentLocation",
                            [&](void* params, FProperty* returnValue) { placed = ReadVec(obj::ValuePtrAt(params, returnValue), returnValue, middle); });
            obj::CallReturn(capsule, L"GetScaledCapsuleHalfHeight",
                            [&](void* params, FProperty* returnValue) { sized = obj::ReadFloatAt(params, returnValue, halfHeight); });
            return placed && sized;
        }

        // Whether a point lies inside a volume, by the engine's measure of the distance from a
        // point to the volume's collision, which is zero inside. Where the engine cannot measure
        // it, the collision's box stands in.
        bool PointInVolume(UObject* volume, const Vec& point, const Vec& low, const Vec& high)
        {
            UObject* body = nullptr;
            obj::ReadObject(volume, L"BrushComponent", body);
            auto* fn = obj::IsLive(body) ? obj::FindFunction(body, L"GetClosestPointOnCollision") : nullptr;
            double distance = -1.0;
            if (fn)
            {
                obj::Call(
                    body, fn,
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"Point") WriteVec(obj::ValuePtrAt(params, prop), prop, point);
                        }
                    },
                    [&](void* params)
                    {
                        for (auto* prop : fn->ForEachProperty())
                        {
                            if (prop && prop->GetName() == L"ReturnValue") obj::ReadFloatAt(params, prop, distance);
                        }
                    });
            }
            if (distance >= 0.0) return distance == 0.0;
            return point.x >= low.x && point.x <= high.x && point.y >= low.y && point.y <= high.y && point.z >= low.z && point.z <= high.z;
        }

        // Whether the character stands inside a way's volume. The point measured is on the
        // character's own upright line, at the height of the volume's middle kept within the
        // capsule, so a volume counts as entered wherever the capsule reaches into it. `storey`
        // says whether the box lies wholly above or below the capsule instead.
        bool InsideVolume(UObject* pawn, UObject* volume, const Vec& low, const Vec& high, int& storey)
        {
            storey = 0;
            Vec middle;
            double halfHeight = 0.0;
            if (!CapsuleSpan(pawn, middle, halfHeight)) return false;
            const double bottom = middle.z - halfHeight;
            const double top = middle.z + halfHeight;
            if (low.z > top)
                storey = 1;
            else if (high.z < bottom)
                storey = -1;
            if (storey != 0) return false;
            return PointInVolume(volume, Vec{middle.x, middle.y, std::clamp((low.z + high.z) / 2.0, bottom, top)}, low, high);
        }

        // Whether a stretch of road crosses a box laid out along the world's axes, by the
        // fraction of it that stays inside every pair of sides.
        bool SegmentCrossesBox(const Vec& a, const Vec& b, const Vec& low, const Vec& high)
        {
            double from = 0.0, to = 1.0;
            const double step[2] = {b.x - a.x, b.y - a.y};
            const double start[2] = {a.x, a.y};
            const double least[2] = {low.x, low.y};
            const double most[2] = {high.x, high.y};
            for (int axis = 0; axis < 2; ++axis)
            {
                if (std::abs(step[axis]) < 1e-6)
                {
                    if (start[axis] < least[axis] || start[axis] > most[axis]) return false;
                    continue;
                }
                double nearSide = (least[axis] - start[axis]) / step[axis];
                double farSide = (most[axis] - start[axis]) / step[axis];
                if (nearSide > farSide) std::swap(nearSide, farSide);
                from = std::max(from, nearSide);
                to = std::min(to, farSide);
                if (from > to) return false;
            }
            return true;
        }

        // How far a road stays from a box: zero once it goes through. Two shapes with straight
        // sides that miss each other are nearest at a corner of one, so the corners of each are
        // measured against the sides of the other.
        double DistanceToBox(const std::vector<Vec>& road, const Vec& low, const Vec& high)
        {
            double best = 1e9;
            const Vec corners[4] = {Vec{low.x, low.y, 0.0}, Vec{high.x, low.y, 0.0}, Vec{high.x, high.y, 0.0}, Vec{low.x, high.y, 0.0}};
            for (size_t i = 1; i < road.size(); ++i)
            {
                const Vec& a = road[i - 1];
                const Vec& b = road[i];
                if (SegmentCrossesBox(a, b, low, high)) return 0.0;
                for (const Vec& end : {a, b})
                {
                    const double dx = std::max({low.x - end.x, 0.0, end.x - high.x});
                    const double dy = std::max({low.y - end.y, 0.0, end.y - high.y});
                    best = std::min(best, std::sqrt(dx * dx + dy * dy));
                }
                for (const Vec& corner : corners)
                    best = std::min(best, DistanceToSegment(corner, a, b));
            }
            return best;
        }

        // Whether a road, walked at the character's height, passes through a volume.
        bool RoadEnters(UObject* volume, const Vec& low, const Vec& high, const std::vector<Vec>& road, double halfHeight)
        {
            for (size_t i = 1; i < road.size(); ++i)
            {
                const Vec& a = road[i - 1];
                const Vec& b = road[i];
                const int steps = std::max(1, static_cast<int>(Distance(a, b) / 50.0));
                for (int step = 0; step <= steps; ++step)
                {
                    const double f = static_cast<double>(step) / steps;
                    Vec p{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
                    // The point measured is on the character's upright line, at the height of
                    // the volume's middle kept within the capsule, as when the character stands.
                    p.z = std::clamp((low.z + high.z) / 2.0, p.z, p.z + 2.0 * halfHeight);
                    if (p.x < low.x || p.x > high.x || p.y < low.y || p.y > high.y || p.z < low.z || p.z > high.z) continue;
                    if (PointInVolume(volume, p, low, high)) return true;
                }
            }
            return false;
        }

        // The place the scene is waiting on that the mesh's road to a target runs through
        // while the target is not beyond it. Such a place is where the scene moves on: the
        // prologue woods end the moment the character steps into the volume at the end of
        // the trail, and the mesh's only road to the chest below the trail went through it,
        // while the chest stands outside and a player who sees it walks straight there. A
        // road like that is not taken. Whether the target is beyond the place is the straight
        // line's word: when the line from the character to the target passes through the
        // volume as well, the target is reached through it whichever way, and the road
        // stands.
        UObject* WayBarring(UObject* pawn, const Vec& here, const std::vector<Vec>& road, const Target& target)
        {
            if (target.way || road.size() < 2) return nullptr;
            Vec middle;
            double halfHeight = 90.0;
            CapsuleSpan(pawn, middle, halfHeight);
            for (auto* volume : g_ways)
            {
                if (!obj::IsLive(volume) || volume == target.actor) continue;
                Vec low;
                Vec high;
                if (!TriggerBox(volume, low, high)) continue;
                if (!RoadEnters(volume, low, high, road, halfHeight)) continue;
                if (RoadEnters(volume, low, high, std::vector<Vec>{here, target.position}, halfHeight)) continue;
                return volume;
            }
            return nullptr;
        }

        // Asks the mesh again when the answer has aged or the character has walked on. A
        // maximum age of zero asks every time.
        void EnsureRoute(UObject* pawn, const Vec& here, Target& t, double now, double maxAge)
        {
            if (now - t.routeAt < maxAge && FlatDistance(here, t.routeFrom) < 200.0) return;
            std::vector<Vec> road;
            const Route route = FindRoute(pawn, here, t, &road);
            t.routeAt = now;
            t.routeFrom = here;
            t.hasRoute = route.valid;
            t.routeLength = route.length;
            t.routeNext = route.next;
            UObject* barring = route.valid ? WayBarring(pawn, here, road, t) : nullptr;
            t.routeBarred = barring != nullptr;
            if (barring) t.hasRoute = false;
            if (barring != t.barredBy)
            {
                if (barring)
                    log::Info(L"explore: the mesh's way to \"{}\", {:.0f} cm, runs through {}, which the scene is waiting on, and the target is not beyond it; "
                              L"by sight it is {:.0f} cm",
                              t.label, route.length, obj::ObjectName(barring), t.distance);
                else
                    log::Info(L"explore: the mesh's way to \"{}\" is clear of {}", t.label, obj::ObjectName(t.barredBy));
                t.barredBy = barring;
            }
        }

        // How far the walkable ground reaches around the character, which is what a player
        // sees of a path or a room. A scene can hold nothing to walk to and still have a way
        // on, and that way is the ground itself.
        std::vector<std::pair<double, double>> OpenGround(UObject* pawn, const Vec& here, double yaw)
        {
            std::vector<std::pair<double, double>> ways; // degrees to the right of the camera, metres
            for (int sector = 0; sector < 8; ++sector)
            {
                const double bearing = sector * 45.0;
                const double radians = (yaw + bearing) * std::numbers::pi / 180.0;
                for (const double reach : {3500.0, 1500.0})
                {
                    Vec point{here.x + std::cos(radians) * reach, here.y + std::sin(radians) * reach, here.z};
                    Vec ground;
                    if (!ProjectToMesh(pawn, point, ground, Vec{250.0, 250.0, 400.0}) || FlatDistance(ground, point) > 400.0) continue;
                    Target probe;
                    probe.position = ground;
                    const Route route = FindRoute(pawn, here, probe, nullptr);
                    if (!route.valid || route.partial) continue;
                    ways.push_back({bearing, FlatDistance(here, ground) / 100.0});
                    break;
                }
            }
            std::sort(ways.begin(), ways.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            if (ways.size() > 3) ways.resize(3);
            return ways;
        }

        std::wstring OpenGroundText()
        {
            UObject* pawn = Pawn();
            Vec here;
            double yaw = 0.0;
            if (!pawn || !ActorLocation(pawn, here) || !CameraYaw(yaw)) return {};
            const auto ways = OpenGround(pawn, here, yaw);
            std::vector<std::wstring> parts;
            for (const auto& [bearing, metres] : ways)
            {
                if (metres < 10.0) continue;
                parts.push_back(locale::Mod(L"explore.open.item", locale::Mod(SectorKey(bearing)), std::to_wstring(std::lround(metres))));
            }
            if (parts.empty()) return {};
            return locale::Mod(L"explore.open", str::Join(parts, L", "));
        }

        // How far a target is on foot, and which way to set off: along the way through when
        // the mesh knows one, else as the crow flies.
        double SpokenDistance(const Target& t)
        {
            return t.hasRoute ? std::max(t.distance, t.routeLength) : t.distance;
        }

        double SpokenBearing(const Target& t, const Vec& here, double yaw)
        {
            return t.hasRoute ? BearingDegrees(here, t.routeNext, yaw) : t.bearing;
        }

        // ---- what is said -------------------------------------------------------------------

        // Where a target is: how far, which way, and whether it stands above or below the
        // character, which a scene of two storeys needs and a flat one never mentions.
        std::wstring Bearings(const Target& t, const Vec& here, double yaw)
        {
            std::vector<std::wstring> where{locale::Mod(SectorKey(SpokenBearing(t, here, yaw)))};
            const double rise = t.position.z - here.z;
            // A way's box says for itself whether it lies wholly above or below the character.
            const int storey = t.way && t.hasBox ? t.storey : (rise > 150.0 ? 1 : (rise < -150.0 ? -1 : 0));
            if (storey > 0)
                where.push_back(locale::Mod(L"explore.higher"));
            else if (storey < 0)
                where.push_back(locale::Mod(L"explore.lower"));
            return str::Join(where, L", ");
        }

        std::wstring TargetLine(const wchar_t* key, const Target& t, const Vec& here, double yaw)
        {
            return locale::Mod(key, {t.label, Metres(SpokenDistance(t)), Bearings(t, here, yaw)});
        }

        std::wstring TargetText(const Target& t, const Vec& here, double yaw)
        {
            if (t.reached) return locale::Mod(L"explore.reached", t.label);
            return TargetLine(L"explore.target", t, here, yaw);
        }

        std::wstring ListText()
        {
            // With nothing to walk to, the ground is all there is to say.
            if (g_targets.empty()) return str::JoinSentences({locale::Mod(L"explore.none"), OpenGroundText()});
            UObject* pawn = Pawn();
            Vec here;
            double yaw = 0.0;
            const bool placed = pawn && ActorLocation(pawn, here) && CameraYaw(yaw);
            const double now = gamethread::NowSeconds();
            std::vector<Target*> ordered;
            for (auto& t : g_targets)
                ordered.push_back(&t);
            std::sort(ordered.begin(), ordered.end(), [](const Target* a, const Target* b) { return a->distance < b->distance; });
            // The way there is asked for the ones near enough to be worth saying, so the list
            // gives the distances on foot rather than through the walls.
            if (placed)
            {
                size_t asked = 0;
                for (auto* t : ordered)
                {
                    if (++asked > 10) break;
                    EnsureRoute(pawn, here, *t, now, 3.0);
                }
                std::sort(ordered.begin(), ordered.end(), [](const Target* a, const Target* b) { return SpokenDistance(*a) < SpokenDistance(*b); });
            }
            std::vector<std::wstring> parts{locale::Mod(L"explore.nearby")};
            size_t n = 0;
            for (const auto* t : ordered)
            {
                if (++n > 8) break;
                const double bearing = placed ? SpokenBearing(*t, here, yaw) : t->bearing;
                parts.push_back(locale::Mod(L"explore.target", {t->label, Metres(SpokenDistance(*t)), locale::Mod(SectorKey(bearing))}));
            }
            return str::JoinSentences(parts);
        }

        // What the mod's keys do here, named for the device in the player's hands.
        std::wstring HintText()
        {
            const auto& s = cfg::Get();
            if (input::CurrentScheme() == input::Scheme::Gamepad)
                return locale::Mod(L"explore.hint", {input::KeyDisplayName(s.padExploreNext), input::KeyDisplayName(s.padExplorePrevious),
                                                     input::KeyDisplayName(s.padExploreWalk), input::KeyDisplayName(s.padExploreWhere),
                                                     input::KeyDisplayName(s.padExploreBeacon)});
            return locale::Mod(L"explore.hint", {s.keyNextTarget, s.keyPreviousTarget, s.keyWalk, s.keyWhere, s.keyBeacon});
        }

        void Select(Target* t, UObject* pawn, const Vec& here, double yaw, bool say)
        {
            g_selected = t ? t->actor : nullptr;
            g_beaconAt = -10.0;
            if (!t) return;
            if (pawn) EnsureRoute(pawn, here, *t, gamethread::NowSeconds(), 0.0);
            if (say) speech::Now(TargetText(*t, here, yaw));
        }

        // ---- the beacon -------------------------------------------------------------------

        void Beacon(double bearing, double distance, double now)
        {
            if (!g_beacon || now - g_beaconAt < cfg::Get().beaconIntervalMs / 1000.0) return;
            g_beaconAt = now;
            const double radians = bearing * std::numbers::pi / 180.0;
            const double level = 1.0 - std::clamp(distance / 3000.0, 0.0, 1.0);
            sounds::Beacon(std::sin(radians), level, std::cos(radians) < 0.0);
        }

        // ---- walking there by itself ------------------------------------------------------

        bool MechanicShown(); // the game's own mechanics take the keys back while they are on screen

        // Ends the walk. A word is said only when there is something to say, and it may
        // interrupt only when the player asked for the stop himself.
        void StopWalk(const wchar_t* what, bool askedFor)
        {
            if (!g_walking) return;
            g_walking = false;
            log::Info(L"explore: walking ended{}{}", what ? L": " : L"", what ? what : L"");
            input::ReleaseWalkKeys();
            if (!what) return;
            if (askedFor)
                speech::Now(locale::Mod(what));
            else
                speech::Announce(locale::Mod(what));
        }

        // How fast the character is going, as the game itself measures it.
        double CharacterSpeed(UObject* pawn)
        {
            double speed = 0.0;
            obj::CallReturn(pawn, L"GetCharacterLinearSpeed", [&](void* params, FProperty* returnValue) { obj::ReadFloatAt(params, returnValue, speed); });
            return speed;
        }

        void PollWalk(double now)
        {
            if (!g_walking) return;
            UObject* pawn = Pawn();
            Target* target = Selected();
            if (!g_exploring || !pawn || !target || target->reached)
            {
                StopWalk(nullptr, false); // arriving speaks for itself
                return;
            }
            // The game may take the character back between two of these frames, so its own
            // word on that is asked for here rather than waited for from the slower round.
            if (watch::CurrentScreen() != nullptr || MechanicShown() || !obj::CallForBool(pawn, L"IsInLoco"))
            {
                StopWalk(nullptr, false);
                return;
            }
            Vec here;
            if (!ActorLocation(pawn, here)) return;
            const double delay = cfg::Get().walkDelayMs / 1000.0;
            // Taking the controls back ends it, as does a walk that goes on too long.
            if (now - g_walkStartedAt > delay + 0.7 && input::MovementHeld())
            {
                StopWalk(L"explore.walk.stop", true);
                return;
            }
            if (now - g_walkStartedAt > 120.0)
            {
                StopWalk(L"explore.walk.stop", false);
                return;
            }
            // A moment before the first step, so the word is out before the character moves.
            if (now - g_walkStartedAt < delay)
            {
                g_walkMovingAt = now;
                g_walkLeanedAt = now;
                return;
            }
            double yaw = 0.0;
            if (!CameraYaw(yaw)) return;
            // The way is asked again as the character advances, and again at once on reaching
            // a corner, so the walk turns with it instead of heading straight for the target.
            EnsureRoute(pawn, here, *target, now, 0.5);
            if (target->hasRoute && FlatDistance(here, target->routeNext) < 80.0) EnsureRoute(pawn, here, *target, now, 0.0);
            // The way there should keep growing shorter. When it stops, the character has come
            // as near as the ground allows and is only circling the spot.
            const double way = target->hasRoute ? target->routeLength : target->distance;
            if (way < g_walkBestWay - 30.0)
            {
                g_walkBestWay = way;
                g_walkBestWayAt = now;
            }
            else if (now - g_walkBestWayAt > 6.0)
            {
                log::Info(L"explore: the way to \"{}\" stopped growing shorter at {:.0f} cm", target->label, way);
                StopWalk(L"explore.walk.blocked", false);
                return;
            }
            // At the end of the way with the game offering a neighbour instead of the target
            // there is nowhere nearer to go: the walk ends, and the neighbour is named, since
            // that is what a press would use.
            UObject* offered = nullptr;
            obj::ReadObject(pawn, L"CurrentUseLocation", offered);
            if (way < 40.0 && !target->destination && obj::IsLive(offered) && offered != target->actor)
            {
                const auto other = CurrentUseLocationLabel();
                log::Info(L"explore: at the end of the way to \"{}\" the game offers {}", target->label, other);
                StopWalk(nullptr, false);
                speech::Announce(locale::Mod(L"explore.walk.other", target->label, other));
                return;
            }
            const Vec goal = target->hasRoute ? target->routeNext : target->position;
            // The heading the beacon gives, in the camera's frame, which is the frame the
            // character's own keys work in. When the way ahead yields nothing for a moment the
            // heading leans to one side and then the other, the way a player feels around a
            // doorway.
            const double bearing = BearingDegrees(here, goal, yaw) + g_walkLean;
            const double radians = bearing * std::numbers::pi / 180.0;
            if (!input::HoldWalkKeys(std::cos(radians), std::sin(radians)))
            {
                StopWalk(L"explore.walk.stop", false);
                return;
            }
            // A character that is going somewhere is not stuck. Turning on the spot at the
            // start of a walk takes a moment and covers no ground, so only a long stillness
            // counts.
            const double speed = CharacterSpeed(pawn);
            if (speed > 15.0)
            {
                g_walkMovingAt = now;
                g_walkLean = 0.0;
            }
            if (now - g_walkLoggedAt >= 1.0)
            {
                if (FlatDistance(here, g_walkLastPosition) > 20.0) g_walkMovingAt = now;
                log::Info(L"explore: walking to \"{}\": {:.0f} cm straight, way {} {:.0f} cm, corner {:.0f} cm at {:.0f} deg, lean {:.0f}, speed {:.0f}, moved "
                          L"{:.0f} cm",
                          target->label, target->distance, target->hasRoute ? L"through" : (target->routeBarred ? L"by sight" : L"unknown"),
                          target->routeLength, FlatDistance(here, goal), BearingDegrees(here, goal, yaw), g_walkLean, speed,
                          FlatDistance(here, g_walkLastPosition));
                g_walkLoggedAt = now;
                g_walkLastPosition = here;
            }
            // Nothing gained for a moment: try a little to one side, then the other.
            if (now - g_walkMovingAt > 1.2 && now - g_walkLeanedAt > 0.9)
            {
                static const double leans[] = {0.0, 40.0, -40.0, 75.0, -75.0};
                size_t next = 0;
                for (size_t i = 0; i < std::size(leans); ++i)
                {
                    if (std::fabs(leans[i] - g_walkLean) < 1.0) next = (i + 1) % std::size(leans);
                }
                g_walkLean = leans[next];
                g_walkLeanedAt = now;
            }
            if (now - g_walkMovingAt > 8.0) StopWalk(L"explore.walk.blocked", false);
        }

        void StartWalk()
        {
            if (!g_exploring) return;
            Target* target = Selected();
            if (!target)
            {
                speech::Now(locale::Mod(L"explore.notarget"));
                return;
            }
            if (target->reached)
            {
                speech::Now(locale::Mod(L"explore.reached", target->label));
                return;
            }
            UObject* pawn = Pawn();
            Vec here;
            if (!pawn || !ActorLocation(pawn, here)) return;
            g_walking = true;
            g_walkStartedAt = gamethread::NowSeconds();
            g_walkMovingAt = g_walkStartedAt;
            g_walkLeanedAt = g_walkStartedAt;
            g_walkLean = 0.0;
            g_walkBestWay = 1e9;
            g_walkBestWayAt = g_walkStartedAt;
            g_walkLoggedAt = g_walkStartedAt;
            g_walkLastPosition = here;
            log::Info(L"explore: walking to \"{}\"", target->label);
            const Route route = FindRoute(pawn, here, *target, nullptr);
            if (route.valid)
                log::Info(L"explore: the ground for \"{}\" is {:.0f} cm across from it and {:+.0f} cm in height; the way there is {} and ends {:.0f} cm from "
                          L"that ground",
                          target->label, FlatDistance(route.goal, target->position), route.goal.z - target->position.z,
                          route.partial ? L"partial" : L"complete", Distance(route.end, route.goal));
            if (route.valid && target->way && target->hasBox)
            {
                Vec middle;
                double halfHeight = 0.0;
                if (CapsuleSpan(pawn, middle, halfHeight))
                {
                    const Vec probe{route.goal.x, route.goal.y,
                                    std::clamp((target->boxLow.z + target->boxHigh.z) / 2.0, route.goal.z, route.goal.z + 2.0 * halfHeight)};
                    log::Info(L"explore: that ground is {} the volume of the way",
                              PointInVolume(target->actor, probe, target->boxLow, target->boxHigh) ? L"inside" : L"outside");
                }
            }
            speech::Now(locale::Mod(L"explore.walk.start"));
        }

        // ---- free roaming ----------------------------------------------------------------

        // The game's own mechanics take the keys back while they are on screen.
        bool MechanicShown()
        {
            std::erase_if(g_mechanicWidgets, [](UObject* widget) { return !obj::IsLive(widget) || !obj::IsWidgetShown(widget, true); });
            return !g_mechanicWidgets.empty();
        }

        void PollRoaming(double now)
        {
            UObject* pawn = Pawn();
            const bool exploring = pawn && watch::CurrentScreen() == nullptr && !MechanicShown() && obj::CallForBool(pawn, L"IsInLoco");
            if (exploring != g_inLoco)
            {
                g_inLoco = exploring;
                log::Info(L"explore: {} {}", pawn ? obj::ObjectName(pawn) : L"<no pawn>",
                          exploring ? L"under the player's control" : L"not under the player's control");
            }
            if (!exploring)
            {
                if (g_exploring)
                {
                    g_exploring = false;
                    g_leftAt = now;
                    g_hintDueAt = -1.0;
                    StopWalk(nullptr, false);
                    if (pawn) ActorLocation(pawn, g_leftAtPosition);
                }
                return;
            }
            Vec here;
            double yaw = 0.0;
            if (!ActorLocation(pawn, here) || !CameraYaw(yaw)) return;
            // The game cuts between fixed cameras as the character walks, and every direction
            // is given from the camera, so a cut turns them all at once.
            if (std::fabs(NormalizeDegrees(yaw - g_cameraYaw)) > 40.0)
            {
                log::Info(L"explore: the camera turned {:.0f} degrees", NormalizeDegrees(yaw - g_cameraYaw));
                g_cameraYaw = yaw;
            }
            else if (std::fabs(NormalizeDegrees(yaw - g_cameraYaw)) > 5.0)
            {
                g_cameraYaw = yaw;
            }
            if (pawn != g_pawn)
            {
                g_pawn = pawn;
                g_targets.clear();
                g_selected = nullptr;
                g_waysAnnounced.clear();
                std::erase_if(g_useLocationState, [](const auto& item) { return !obj::IsLive(item.first); });
            }
            // A short exchange in the middle of a scene leaves everything where it was, so
            // the list is not said again; only what the scene has added is.
            else if (!g_exploring && !g_targets.empty() && now - g_leftAt < 30.0 && FlatDistance(here, g_leftAtPosition) < 2000.0)
            {
                g_exploring = true;
            }
            // Which of several things standing together the game is offering: it picks one
            // and the player changes it by standing somewhere else, so every change is a line.
            UObject* offered = nullptr;
            obj::ReadObject(pawn, L"CurrentUseLocation", offered);
            auto fresh = Gather(pawn, here, yaw, now);
            const auto added = Reconcile(fresh);
            if (offered != g_currentUse)
            {
                g_currentUse = offered;
                log::Info(L"explore: the game offers {}", obj::IsLive(offered) ? CurrentUseLocationLabel() : L"nothing to use");
            }

            // Arrival: a use location once the character stands in it, a place once it is
            // close; a target left behind again can be walked to again.
            for (auto& t : g_targets)
            {
                const bool there = AtTarget(t, offered);
                if (there && !t.reached)
                {
                    t.reached = true;
                    if (t.actor == g_selected && g_exploring)
                    {
                        log::Info(L"explore: reached \"{}\"", t.label);
                        speech::Announce(locale::Mod(L"explore.reached", t.label));
                        sounds::Play(sounds::Cue::Confirm);
                    }
                }
                else if (!there && t.reached && ((t.way && t.hasBox) || t.distance > 350.0))
                {
                    t.reached = false;
                }
            }

            for (const auto& t : added)
                log::Info(L"explore: {} \"{}\" {} at {:.0f} cm, {:.0f} deg", t.destination ? L"place" : L"use location", t.label, obj::ObjectName(t.actor),
                          t.distance, t.bearing);

            // A way the road filter hid for a moment and shows again is not new: it is said the
            // first time the scene watches it, and afterwards only returns to the list.
            std::vector<UObject*> silent;
            for (const auto& t : g_targets)
            {
                if (!t.way) continue;
                if (std::find(g_waysAnnounced.begin(), g_waysAnnounced.end(), t.actor) != g_waysAnnounced.end())
                    silent.push_back(t.actor);
                else
                    g_waysAnnounced.push_back(t.actor);
            }

            if (!g_exploring)
            {
                g_exploring = true;
                if (cfg::Get().autoTarget)
                {
                    auto it = std::find_if(g_targets.begin(), g_targets.end(), [](const Target& t) { return !t.reached; });
                    if (it != g_targets.end()) Select(&*it, pawn, here, yaw, false);
                }
                // The keys are said after the game's own word about the stick or the movement
                // keys, which it shows a moment after handing the character over, and then the
                // target the beacon leads to. The rest of what is around is left to F6.
                g_hintDueAt = now + 1.0;
            }
            else
            {
                for (auto& t : g_targets)
                {
                    if (std::none_of(added.begin(), added.end(), [&](const Target& a) { return a.actor == t.actor; })) continue;
                    if (std::find(silent.begin(), silent.end(), t.actor) != silent.end()) continue;
                    EnsureRoute(pawn, here, t, now, 3.0);
                    speech::Announce(locale::Mod(L"explore.new", {t.label, Metres(SpokenDistance(t)), locale::Mod(SectorKey(SpokenBearing(t, here, yaw)))}));
                }
                if (g_selected == nullptr && cfg::Get().autoTarget && !added.empty())
                {
                    auto it = std::find_if(g_targets.begin(), g_targets.end(), [](const Target& t) { return !t.reached; });
                    if (it != g_targets.end()) Select(&*it, pawn, here, yaw, false);
                }
            }

            if (g_hintDueAt > 0.0 && now >= g_hintDueAt)
            {
                g_hintDueAt = -1.0;
                // Once a scene, not after every exchange in the middle of one.
                if (now - g_hintSaidAt > 120.0)
                {
                    g_hintSaidAt = now;
                    speech::Announce(HintText());
                }
                if (Target* chosen = Selected()) speech::Announce(TargetText(*chosen, here, yaw));
            }

            Target* target = Selected();
            if (!target)
            {
                g_selected = nullptr;
                return;
            }
            if (target->reached) return;
            EnsureRoute(pawn, here, *target, now, g_walking ? 0.5 : 1.5);
            Beacon(SpokenBearing(*target, here, yaw), SpokenDistance(*target), now);
        }

        // ---- looking around --------------------------------------------------------------

        void PollStatic(double now)
        {
            if (!obj::IsLive(g_static) || watch::CurrentScreen() != nullptr) return;
            if (now - g_glintsScannedAt > 1.0)
            {
                g_glints = obj::FindAllLive(L"StaticExplorationGlintActorSMG");
                g_glintsScannedAt = now;
            }
            UObject* controller = obj::LocalPlayerController();
            UObject* camera = nullptr;
            if (!controller || !obj::ReadObject(controller, L"PlayerCameraManager", camera) || !obj::IsLive(camera)) return;
            Vec eye;
            double yaw = 0.0;
            if (!ActorLocation(camera, eye) || !YawOf(camera, L"GetCameraRotation", yaw)) return;
            double best = 1e9;
            for (auto* glint : g_glints)
            {
                Vec at;
                if (!obj::IsLive(glint) || !ActorLocation(glint, at)) continue;
                const double bearing = BearingDegrees(eye, at, yaw);
                if (std::fabs(bearing) < std::fabs(best)) best = bearing;
            }
            if (best > 1e8) return;
            if (g_beacon && now - g_beaconAt >= cfg::Get().beaconIntervalMs / 1000.0)
            {
                g_beaconAt = now;
                const double radians = best * std::numbers::pi / 180.0;
                sounds::Beacon(std::sin(radians), 1.0 - std::clamp(std::fabs(best) / 90.0, 0.0, 1.0), std::cos(radians) < 0.0);
            }
        }

        // The scene turns a use location on when the player may walk to it, and off when it
        // is done with it.
        void OnUseLocationState(UObject* self, FFrame& stack)
        {
            if (!obj::IsLive(self)) return;
            int64_t state = 0;
            params::Int(stack, L"NewState", state);
            const double now = gamethread::NowSeconds();
            auto& entry = g_useLocationState[self];
            const bool changed = entry.state != state;
            entry.state = state;
            if (state == 1) entry.availableAt = now;
            // Only a settled change is worth a line: the game flicks these on and off dozens
            // of times a second while the character stands in one.
            if (changed && now - entry.loggedAt > 1.0)
            {
                entry.loggedAt = now;
                log::Info(L"explore: use location \"{}\" {} state {}", UseLocationLabel(self), obj::ObjectName(self), state);
            }
            if (g_useLocationState.size() > 256) std::erase_if(g_useLocationState, [](const auto& item) { return !obj::IsLive(item.first); });
        }

        void OnPoiFound(UObject*, FFrame&)
        {
            log::Info(L"explore: point of interest found");
            sounds::Play(sounds::Cue::Confirm);
            speech::Announce(locale::Mod(L"explore.found"));
        }

        // ---- the reading pane ------------------------------------------------------------

        std::wstring PageText()
        {
            std::vector<std::wstring> parts;
            const auto title = ui::PropertyText(g_readingPane, L"TitleText");
            if (!title.empty()) parts.push_back(title);
            UObject* lines = nullptr;
            if (obj::ReadObject(g_readingPane, L"LinesBox", lines) && obj::IsLive(lines))
            {
                std::vector<std::wstring> text;
                for (auto* line : obj::PanelChildren(lines))
                {
                    for (const auto& t : obj::DescendantTexts(line, 4))
                    {
                        const auto clean = str::CollapseWhitespace(str::StripMarkup(t));
                        if (!clean.empty()) text.push_back(clean);
                    }
                }
                if (!text.empty()) parts.push_back(str::Join(text, L" "));
            }
            const auto page = ui::PropertyText(g_readingPane, L"PageCount");
            if (!page.empty()) parts.push_back(page);
            return str::JoinSentences(parts);
        }

        void PollReading()
        {
            if (!obj::IsLive(g_readingPane) || !obj::IsWidgetShown(g_readingPane, true)) return;
            const auto text = PageText();
            if (text != g_pageText)
            {
                g_pageText = text;
                g_pageStable = 0;
            }
            else if (!text.empty() && text != g_pageRead && ++g_pageStable >= 3)
            {
                g_pageRead = text;
                log::Info(L"explore: reading pane \"{}\"", text.substr(0, 120));
                speech::Announce(text);
            }
        }

        void OnPageChanged(UObject* self, FFrame&)
        {
            if (self == g_readingPane) g_pageRead.clear();
        }

        void OnHud(const watch::HudEvent& ev)
        {
            static const wchar_t* const kMechanics[] = {L"Choice", L"QTE", L"ButtonMash", L"DontBreathe", L"RealtimeCombat"};
            if (std::any_of(std::begin(kMechanics), std::end(kMechanics), [&](const wchar_t* part) { return ev.hudClass.find(part) != std::wstring::npos; }))
            {
                std::erase(g_mechanicWidgets, ev.instance);
                if (ev.appeared && ev.instance)
                {
                    g_mechanicWidgets.push_back(ev.instance);
                    StopWalk(nullptr, false); // the keys belong to the game the moment it wants them
                }
            }
            if (obj::IsA(ev.hud, L"ActionHUDStaticExplorationSMG026"))
            {
                g_static = ev.appeared ? ev.instance : nullptr;
                g_glintsScannedAt = -10.0;
                log::Info(L"explore: looking around {}", ev.appeared ? L"begins" : L"ends");
            }
            else if (obj::IsA(ev.hud, L"ActionHUDReadingPaneSMG026"))
            {
                g_readingPane = ev.appeared ? ev.instance : nullptr;
                g_pageText.clear();
                g_pageRead.clear();
                g_pageStable = 0;
                log::Info(L"explore: reading pane {}", ev.appeared ? L"opened" : L"closed");
            }
        }

        void PollImpl()
        {
            const double now = gamethread::NowSeconds();
            PollWalk(now); // the character is pushed along every frame, as a held key would
            if (gamethread::FrameCount() % 10 == 0) PollRoaming(now);
            if (gamethread::FrameCount() % 5 == 0) PollStatic(now);
            if (gamethread::FrameCount() % 6 == 3) PollReading();
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"exploration.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }

        // The next or the previous target in the list's stable order.
        void Step(int direction)
        {
            if (!g_exploring || g_targets.empty()) return;
            size_t index = 0;
            bool found = false;
            for (size_t i = 0; i < g_targets.size(); ++i)
            {
                if (g_targets[i].actor == g_selected)
                {
                    index = (i + g_targets.size() + static_cast<size_t>(direction > 0 ? 1 : -1)) % g_targets.size();
                    found = true;
                }
            }
            if (!found) index = direction > 0 ? 0 : g_targets.size() - 1;
            UObject* pawn = Pawn();
            Vec here;
            double yaw = 0.0;
            if (!pawn || !ActorLocation(pawn, here) || !CameraYaw(yaw)) return;
            Select(&g_targets[index], pawn, here, yaw, true);
        }

        void DumpImpl()
        {
            UObject* pawn = Pawn();
            if (!pawn)
            {
                log::Info(L"explore dump: no pawn");
                return;
            }
            Vec here;
            double yaw = 0.0, camYaw = 0.0;
            ActorLocation(pawn, here);
            YawOf(pawn, L"K2_GetActorRotation", yaw);
            CameraYaw(camYaw);
            log::Info(L"explore dump: pawn {} {} at ({:.0f}, {:.0f}, {:.0f}) yaw {:.0f}, camera yaw {:.0f}, in loco {}, cinematic-to-loco {}, screen {}",
                      obj::ClassName(pawn), obj::ObjectName(pawn), here.x, here.y, here.z, yaw, camYaw, obj::CallForBool(pawn, L"IsInLoco"),
                      obj::CallForBool(pawn, L"IsInCinematicToLoco"), watch::CurrentScreen() ? obj::ClassName(watch::CurrentScreen()) : L"<none>");
            for (const wchar_t* list : {L"AvailableUseLocations", L"OverlappingUseLocations"})
            {
                std::vector<UObject*> actors;
                obj::ReadObjectArray(pawn, list, actors);
                log::Info(L"explore dump: {} ({})", list, actors.size());
                for (auto* actor : actors)
                {
                    if (!obj::IsLive(actor)) continue;
                    Vec at;
                    ActorLocation(actor, at);
                    std::wstring key, own;
                    UObject* data = nullptr;
                    if (obj::ReadObject(actor, L"UseLocationData", data) && data) obj::ReadLocaleKey(data, L"PromptLabel", key);
                    obj::ReadLocaleKey(actor, L"LocaleString", own);
                    UObject* user = nullptr;
                    obj::ReadObject(actor, L"User", user);
                    double radius = 0.0;
                    obj::ReadFloat(actor, L"NavigationRadius", radius);
                    log::Info(L"  {} {} label \"{}\" (data key {}, own key {}) at ({:.0f}, {:.0f}, {:.0f}) {:.0f} cm {:.0f} deg radius {:.0f} user {}",
                              obj::ClassName(actor), obj::ObjectName(actor), UseLocationLabel(actor), key, own, at.x, at.y, at.z, Distance(here, at),
                              BearingDegrees(here, at, camYaw), radius, user ? obj::ObjectName(user) : L"<none>");
                }
            }
            UObject* current = nullptr;
            obj::ReadObject(pawn, L"CurrentUseLocation", current);
            log::Info(L"explore dump: CurrentUseLocation {}", current ? obj::ObjectName(current) : L"<none>");
            const auto useLocations = obj::FindAllLive(L"UseLocation");
            log::Info(L"explore dump: use locations in the level ({})", useLocations.size());
            for (auto* actor : useLocations)
            {
                Vec at;
                ActorLocation(actor, at);
                const auto state = g_useLocationState.find(actor);
                bool hidden = false, collision = false, glint = false, mustLook = false;
                double viewAngle = 0.0;
                obj::ReadBool(actor, L"bHidden", hidden);
                obj::ReadBool(actor, L"bActorEnableCollision", collision);
                obj::ReadBool(actor, L"bShouldHaveGlint", glint);
                obj::ReadBool(actor, L"bCharacterMustBeLookingAtDiscoveryLocation", mustLook);
                obj::ReadFloat(actor, L"CharacterViewThresholdAngle", viewAngle);
                UObject* user = nullptr;
                UObject* requested = nullptr;
                UObject* data = nullptr;
                UObject* marker = nullptr;
                obj::ReadObject(actor, L"User", user);
                obj::ReadObject(actor, L"RequestedUser", requested);
                obj::ReadObject(actor, L"UseLocationData", data);
                obj::ReadObject(actor, L"UseLocationMarkerComponent", marker);
                bool markerVisible = false;
                UObject* markerCharacter = nullptr;
                if (obj::IsLive(marker))
                {
                    obj::ReadBool(marker, L"bVisible", markerVisible);
                    obj::ReadObject(marker, L"Character", markerCharacter);
                }
                std::wstring key;
                obj::ReadLocaleKey(actor, L"LocaleString", key);
                log::Info(L"  {} {} label \"{}\" (own key {}, data {}) state {} hidden {} collision {} glint {} must look {} view angle {:.0f} user {} "
                          L"requested {} marker visible {} "
                          L"character {} at "
                          L"{:.0f} cm {:.0f} deg",
                          obj::ClassName(actor), obj::ObjectName(actor), UseLocationLabel(actor), key, data ? obj::ObjectName(data) : L"<none>",
                          state == g_useLocationState.end() ? -1 : state->second.state, hidden, collision, glint, mustLook, viewAngle,
                          user ? obj::ObjectName(user) : L"<none>", requested ? obj::ObjectName(requested) : L"<none>", markerVisible,
                          markerCharacter ? obj::ObjectName(markerCharacter) : L"<none>", Distance(here, at), BearingDegrees(here, at, camYaw));
            }
            const auto destinations = obj::FindAllLive(L"ExplorationDestinationSMG026");
            log::Info(L"explore dump: places ({})", destinations.size());
            for (auto* actor : destinations)
            {
                Vec at;
                ActorLocation(actor, at);
                int64_t type = 0, useType = 0;
                bool discovered = false, used = false;
                obj::ReadInt(actor, L"DestinationType", type);
                obj::ReadInt(actor, L"UseLocationType", useType);
                obj::ReadBool(actor, L"bIsDiscovered", discovered);
                obj::ReadBool(actor, L"bIsUsed", used);
                std::wstring key;
                obj::ReadLocaleKey(actor, L"PromptLabel", key);
                log::Info(L"  {} label \"{}\" (key {}) type {} useType {} discovered {} used {} at ({:.0f}, {:.0f}, {:.0f}) {:.0f} cm {:.0f} deg",
                          obj::ObjectName(actor), DestinationLabel(actor), key, type, useType, discovered, used, at.x, at.y, at.z, Distance(here, at),
                          BearingDegrees(here, at, camYaw));
            }
            for (const auto& t : g_targets)
            {
                const Route route = FindRoute(pawn, here, t, nullptr);
                log::Info(L"explore dump: target \"{}\" {:.0f} cm straight {:.0f} deg; way {} {:.0f} cm, corner {:.0f} cm at {:.0f} deg", t.label, t.distance,
                          t.bearing, route.valid ? (route.partial ? L"partial" : L"whole") : L"none", route.length, FlatDistance(here, route.next),
                          BearingDegrees(here, route.next, camYaw));
            }
            for (const wchar_t* cls :
                 {L"ExplorationDestinationGroupSMG026", L"StaticExplorationGlintActorSMG", L"GlintActorBase", L"StaticExplorationSMG026_Replicator"})
            {
                const auto actors = obj::FindAllLive(cls);
                log::Info(L"explore dump: {} ({})", cls, actors.size());
                for (auto* actor : actors)
                {
                    Vec at;
                    ActorLocation(actor, at);
                    int64_t status = -1;
                    obj::ReadInt(actor, L"Status", status);
                    log::Info(L"  {} {} at ({:.0f}, {:.0f}, {:.0f}) {:.0f} cm status {}", obj::ClassName(actor), obj::ObjectName(actor), at.x, at.y, at.z,
                              Distance(here, at), status);
                }
            }
        }
    }

    void ExplorationFeature::Install()
    {
        g_beacon = cfg::Get().beacon;
        watch::AddHudListener(&OnHud);
        hooks::OnScript(L"ReadingPaneWidget_C", L"AddLines", &OnPageChanged);
        if (!hooks::OnNative(L"/Script/SMG026Runtime.StaticExplorationSMG026_Replicator:MulticastPOIFound", nullptr, &OnPoiFound))
            log::Error(L"explore: the point-of-interest function is not hooked");
        if (!hooks::OnNative(L"/Script/SMG026Runtime.UseLocationSMG026:ClientStateChanged", nullptr, &OnUseLocationState))
            log::Error(L"explore: the use location state function is not hooked");
        gamethread::AddPoller(L"explore", &Poll);
    }

    void ExplorationFeature::Describe(std::vector<std::wstring>& out)
    {
        if (obj::IsLive(g_readingPane) && !g_pageRead.empty())
        {
            out.push_back(g_pageRead);
            return;
        }
        if (!g_exploring) return;
        out.push_back(ListText());
    }

    void ExplorationFeature::Help(std::vector<std::wstring>& out)
    {
        if (obj::IsLive(g_readingPane))
        {
            const auto next = input::KeyForAction(ui::ActionName(g_readingPane, L"ActionMappingNext"));
            const auto close = input::KeyForAction(ui::ActionName(g_readingPane, L"ActionMappingClose"));
            if (!next.empty() && !close.empty()) out.push_back(locale::Mod(L"help.reading", next, close));
            return;
        }
        if (!g_exploring && !obj::IsLive(g_static)) return;
        const auto& s = cfg::Get();
        if (input::CurrentScheme() == input::Scheme::Gamepad)
        {
            out.push_back(locale::Mod(L"help.explore.pad", {input::KeyDisplayName(s.padExploreNext), input::KeyDisplayName(s.padExplorePrevious),
                                                            input::KeyDisplayName(s.padExploreWhere), input::KeyDisplayName(s.padExploreBeacon),
                                                            input::KeyDisplayName(s.padExploreWalk)}));
        }
        else
        {
            out.push_back(locale::Mod(L"help.explore", {s.keyNextTarget, s.keyPreviousTarget, s.keyBeacon}));
            out.push_back(locale::Mod(L"help.where", s.keyWhere));
            out.push_back(locale::Mod(L"help.walk", s.keyWalk));
        }
        if (!g_destinationPrompt) return;
        std::vector<std::wstring> keys;
        for (const wchar_t* action :
             {L"ExporationDestinationAction", L"ExporationDestinationLeft", L"ExporationDestinationRight", L"ExporationDestinationCancel"})
            keys.push_back(input::KeyForAction(action));
        if (std::none_of(keys.begin(), keys.end(), [](const std::wstring& k) { return k.empty(); }))
            out.push_back(locale::Mod(L"help.explore.destinations", keys));
    }

    void NextTarget()
    {
        Step(1);
    }

    void PreviousTarget()
    {
        Step(-1);
    }

    void WhereIsTarget()
    {
        if (!g_exploring) return;
        Target* target = Selected();
        if (!target)
        {
            speech::Now(locale::Mod(L"explore.notarget"));
            return;
        }
        UObject* pawn = Pawn();
        Vec here;
        double yaw = 0.0;
        if (!pawn || !ActorLocation(pawn, here) || !CameraYaw(yaw)) return;
        target->distance = Distance(here, target->position);
        target->bearing = BearingDegrees(here, target->position, yaw);
        EnsureRoute(pawn, here, *target, gamethread::NowSeconds(), 0.0);
        speech::Now(TargetText(*target, here, yaw));
    }

    void WalkToTarget()
    {
        if (g_walking)
            StopWalk(L"explore.walk.stop", true);
        else
            StartWalk();
    }

    bool ExplorationActive()
    {
        return g_exploring;
    }

    void ToggleBeacon()
    {
        g_beacon = !g_beacon;
        log::Info(L"explore: beacon {}", g_beacon ? L"on" : L"off");
        speech::Now(locale::Mod(g_beacon ? L"explore.beacon.on" : L"explore.beacon.off"));
        // Kept in the ini, so that the next start begins the way this one ended.
        if (!cfg::Persist(L"Exploration", L"Beacon", g_beacon ? L"1" : L"0")) log::Error(L"explore: the beacon setting could not be saved to QuarryAccess.ini");
    }

    void NoteDestinationPrompt()
    {
        if (!g_destinationPrompt) log::Info(L"explore: the game offers its own place navigation");
        g_destinationPrompt = true;
    }

    std::wstring CurrentUseLocationLabel()
    {
        UObject* pawn = Pawn();
        if (!pawn) return {};
        UObject* current = nullptr;
        obj::ReadObject(pawn, L"CurrentUseLocation", current);
        if (!obj::IsLive(current))
        {
            std::vector<UObject*> overlapping;
            obj::ReadObjectArray(pawn, L"OverlappingUseLocations", overlapping);
            for (auto* actor : overlapping)
            {
                if (obj::IsLive(actor))
                {
                    current = actor;
                    break;
                }
            }
        }
        if (!obj::IsLive(current)) return {};
        // The name it has among the things to walk to, which is the one the player was told.
        for (const auto& t : g_targets)
        {
            if (t.actor == current) return t.label;
        }
        return UseLocationLabel(current);
    }

    void DumpExploration()
    {
        obj::SafeInvoke([](void*) { DumpImpl(); }, nullptr);
    }
}
