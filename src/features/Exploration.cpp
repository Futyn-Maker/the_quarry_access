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
            std::wstring verb; // what the game says can be done here, when it says anything
            // The walkable way there, from the navigation mesh: how far it is on foot and
            // the corner to head for now. Everything said about a target uses these when
            // they are known, so the list, the beacon and the walking agree.
            bool hasRoute = false;
            double routeAt = -1000.0; // when the mesh was last asked
            double routeLength = 0.0; // centimetres along the way
            Vec routeNext;            // the corner to walk to now
            Vec routeFrom;            // where the character was when it was asked
        };

        // What the navigation mesh answers for one question.
        struct Route
        {
            bool valid = false;   // the mesh gave a way through
            bool partial = false; // it stops short of the target
            double length = 0.0;
            Vec next;
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
        bool g_beacon = true;
        double g_beaconAt = -10.0;
        double g_destinationsScannedAt = -10.0;
        std::vector<UObject*> g_destinations;
        std::vector<UObject*> g_ways;
        double g_waysScannedAt = -10.0;
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
        double g_walkMovingAt = 0.0; // when the character was last seen actually moving
        double g_walkLean = 0.0;     // degrees the heading leans while feeling for a way past
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

        // The ways on that the scene is watching for right now. A scene often moves only when
        // the character walks into a place its own logic waits on, and those places carry no
        // marker, no glint and no name on screen for anyone. What a player does see is the
        // ground leading to them, so they are offered by where they are and never by what they
        // are called. They are found by asking the flow: the action that turned on a use
        // location the character can see belongs to a state, and that state's transitions say
        // which volumes it is waiting on.
        std::vector<UObject*> WayVolumes(const std::vector<UObject*>& availableUses)
        {
            std::vector<std::wstring> wanted;
            for (auto* action : obj::FindAllLive(L"GFActionMakeUseLocationAvailable"))
            {
                std::wstring named;
                if (!ReadActorReference(action, L"UseLocationName", named)) continue;
                if (std::none_of(availableUses.begin(), availableUses.end(), [&](UObject* use) { return obj::IsLive(use) && obj::ObjectName(use) == named; }))
                    continue;
                UObject* schema = obj::Outer(action);
                UObject* state = schema ? obj::Outer(schema) : nullptr;
                if (!obj::IsLive(state)) continue;
                std::vector<UObject*> transitions;
                obj::ReadObjectArray(state, L"Transitions", transitions);
                for (auto* transition : transitions)
                {
                    if (!obj::IsLive(transition)) continue;
                    std::vector<UObject*> conditions;
                    obj::ReadObjectArray(transition, L"Conditions", conditions);
                    for (auto* condition : conditions)
                    {
                        std::wstring volume;
                        if (obj::IsLive(condition) && obj::IsA(condition, L"GFConditionInVolume") &&
                            ReadActorReference(condition, L"TriggerVolumeName", volume))
                            wanted.push_back(volume);
                    }
                }
            }
            std::vector<UObject*> out;
            if (wanted.empty()) return out;
            for (auto* volume : obj::FindAllLive(L"TriggerVolumeSMG"))
            {
                if (std::find(wanted.begin(), wanted.end(), obj::ObjectName(volume)) != wanted.end()) out.push_back(volume);
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

        std::vector<Target> Gather(UObject* pawn, const Vec& here, double yaw, double now)
        {
            std::vector<Target> uses;
            std::vector<Target> places;
            const double range = cfg::Get().exploreRange * 100.0;
            std::vector<UObject*> available;
            std::vector<UObject*> overlapping;
            obj::ReadObjectArray(pawn, L"AvailableUseLocations", available);
            obj::ReadObjectArray(pawn, L"OverlappingUseLocations", overlapping);
            // The character's own list holds only what is a few steps away; the rest of what
            // the scene has turned on comes from what it told the client.
            for (const auto& [actor, state] : g_useLocationState)
            {
                const bool on = state.state == 1 || now - state.availableAt < 2.0;
                if (on && obj::IsLive(actor) && std::find(available.begin(), available.end(), actor) == available.end()) available.push_back(actor);
            }
            for (auto* actor : available)
            {
                if (!obj::IsLive(actor)) continue;
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
                places.push_back(std::move(t));
            }

            // The ways on the scene is watching for, which are what a player walks toward
            // when there is nothing left to use.
            if (now - g_waysScannedAt > 3.0)
            {
                g_ways = WayVolumes(available);
                g_waysScannedAt = now;
            }
            std::vector<Target> ways;
            for (auto* volume : g_ways)
            {
                if (!obj::IsLive(volume)) continue;
                Target t;
                t.actor = volume;
                t.destination = true;
                t.way = true;
                if (!ActorLocation(volume, t.position)) continue;
                if (Distance(here, t.position) > range) continue;
                t.label = locale::Mod(L"explore.wayon");
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
            // A way that the scene already names, with a place marker or something to use
            // standing on it, is that thing: it is not offered a second time without a name.
            for (auto& way : ways)
            {
                if (std::any_of(out.begin(), out.end(), [&](const Target& o) { return FlatDistance(o.position, way.position) < 600.0; })) continue;
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
        // trigger. Several of them can sit within arm's length of one another, so no distance
        // of our own choosing could say whether a press would reach this one or its neighbour.
        // A place of the scene is only a point on the ground with nothing to press, so being
        // near it is arriving.
        bool AtTarget(const Target& t, UObject* offered)
        {
            if (t.destination) return t.distance < 150.0;
            return t.inRange || t.actor == offered;
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

        // The point on the navigation mesh nearest to a point, when there is one close by.
        bool ProjectToMesh(UObject* pawn, const Vec& point, Vec& out)
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
                            WriteVec(obj::ValuePtrAt(params, prop), prop, Vec{250.0, 250.0, 400.0});
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

        Route FindRoute(UObject* pawn, const Vec& here, const Target& target)
        {
            Route route;
            UObject* nav = NavigationSystem();
            auto* fn = nav ? obj::FindFunction(nav, L"FindPathToLocationSynchronously") : nullptr;
            if (!fn) return route;
            Vec end = target.position;
            Vec projected;
            if (ProjectToMesh(pawn, end, projected)) end = projected;
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
            route.valid = true;
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

        // Asks the mesh again when the answer has aged or the character has walked on. A
        // maximum age of zero asks every time.
        void EnsureRoute(UObject* pawn, const Vec& here, Target& t, double now, double maxAge)
        {
            if (t.hasRoute && now - t.routeAt < maxAge && FlatDistance(here, t.routeFrom) < 200.0) return;
            const Route route = FindRoute(pawn, here, t);
            t.routeAt = now;
            t.routeFrom = here;
            t.hasRoute = route.valid;
            t.routeLength = route.length;
            t.routeNext = route.next;
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
                    if (!ProjectToMesh(pawn, point, ground) || FlatDistance(ground, point) > 400.0) continue;
                    Target probe;
                    probe.position = ground;
                    const Route route = FindRoute(pawn, here, probe);
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

        std::wstring TargetText(const Target& t, const Vec& here, double yaw)
        {
            if (t.reached) return locale::Mod(L"explore.reached", t.label);
            return locale::Mod(L"explore.target", {t.label, Metres(SpokenDistance(t)), locale::Mod(SectorKey(SpokenBearing(t, here, yaw)))});
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
                          target->label, target->distance, target->hasRoute ? L"through" : L"unknown", target->routeLength, FlatDistance(here, goal),
                          BearingDegrees(here, goal, yaw), g_walkLean, speed, FlatDistance(here, g_walkLastPosition));
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
            g_walkLoggedAt = g_walkStartedAt;
            g_walkLastPosition = here;
            log::Info(L"explore: walking to \"{}\"", target->label);
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
                else if (!there && t.reached && t.distance > 350.0)
                {
                    t.reached = false;
                }
            }

            for (const auto& t : added)
                log::Info(L"explore: {} \"{}\" {} at {:.0f} cm, {:.0f} deg", t.destination ? L"place" : L"use location", t.label, obj::ObjectName(t.actor),
                          t.distance, t.bearing);

            if (!g_exploring)
            {
                g_exploring = true;
                if (!g_targets.empty())
                {
                    speech::Announce(ListText());
                    if (cfg::Get().autoTarget)
                    {
                        auto it = std::find_if(g_targets.begin(), g_targets.end(), [](const Target& t) { return !t.reached; });
                        if (it != g_targets.end()) Select(&*it, pawn, here, yaw, false);
                    }
                }
            }
            else
            {
                for (auto& t : g_targets)
                {
                    if (std::none_of(added.begin(), added.end(), [&](const Target& a) { return a.actor == t.actor; })) continue;
                    EnsureRoute(pawn, here, t, now, 3.0);
                    speech::Announce(locale::Mod(L"explore.new", {t.label, Metres(SpokenDistance(t)), locale::Mod(SectorKey(SpokenBearing(t, here, yaw)))}));
                }
                if (g_selected == nullptr && cfg::Get().autoTarget && !added.empty())
                {
                    auto it = std::find_if(g_targets.begin(), g_targets.end(), [](const Target& t) { return !t.reached; });
                    if (it != g_targets.end()) Select(&*it, pawn, here, yaw, false);
                }
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
                const Route route = FindRoute(pawn, here, t);
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
