#include "features/Combat.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <Unreal/NameTypes.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
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

        // What a target looks like on screen: the creature (a character wearing the werewolf),
        // a thing that is not a character, or a person. A person is never named, because the
        // game hides who it is in more than one fight.
        enum class Kind
        {
            Creature,
            Object,
            Person,
        };

        // Something the scene counts a shot against: an actor the fight's flow action names.
        // Its name is the level's and is never spoken.
        struct Target
        {
            int index = 0;
            std::wstring name;
            UObject* actor = nullptr;
            Kind kind = Kind::Person;
            bool dead = false;
            double metres = 0.0; // from the aim at the last look
        };

        // The line a shot takes, as the game fires it: from the muzzle of the weapon, along the
        // rotation of the torch on the weapon, with the pellets spread about that line. The
        // camera's line of sight is read beside it for the log.
        struct Beam
        {
            bool placed = false;
            Vec from;               // the muzzle, or the torch where the weapon names no socket
            Vec forward, right, up; // the torch's own axes
            double pitch = 0.0;
            double yaw = 0.0;
            const wchar_t* source = L"";
            bool cameraPlaced = false;
            Vec cameraFrom;
            double cameraPitch = 0.0;
            double cameraYaw = 0.0;
        };

        // Where a target stands against the shot.
        struct Sight
        {
            bool placed = false;
            double horizontal = 0.0; // degrees to the right of the line
            double vertical = 0.0;   // degrees above it
            double angle = 0.0;      // degrees off the line altogether
            double metres = 0.0;     // from the muzzle
            int pellets = 0;         // how many of the shot's pellets meet the target's body
            int of = 0;              // of how many the shot throws
            bool onTarget = false;   // a shot fired now would take the target
            bool solid = false;      // and so would one fired a moment later, whichever way the aim sways meanwhile
            bool assisted = false;   // within the aim assist's reach: its sphere along the line touches the target
            double cameraAngle = 0.0;
            bool cameraOn = false;
        };

        struct Combat
        {
            bool on = false;
            UObject* prompt = nullptr;  // the fire prompt the game shows
            UObject* reticle = nullptr; // a crosshair, when a fight shows one
            UObject* action = nullptr;  // the flow action that runs the fight
            UObject* status = nullptr;  // the game's own count of shots and of the targets' health
            UObject* replicator = nullptr;
            bool modifierSeen = false; // the fight's camera modifier was on at some point
            std::vector<Target> targets;
            int chosen = -1;  // the target the sound leads to; -1 for the one nearest the beam
            int leading = -1; // the target it led to at the last poll
            Sight sight;
            double startedAt = 0.0;
            double activeAt = 0.0; // the last moment a sign of the fight was seen
            bool hasTimeLimit = false;
            double timeLimit = 0.0;
            bool automatic = false; // the game's aiming setting fights by itself
            int64_t shots = 0;
            int64_t healthSum = -1; // the targets' health added up, to see a hit
            double shotAt = -1.0;   // when the last shot went off, until it is judged
            double lastShotAt = -1.0;
            int64_t hitShot = 0; // the shot the last hit was counted for
            bool anyHit = false;
            double blipAt = 0.0;
            double quietUntil = 0.0; // the aim sound waits while a cue plays
            double loggedAt = 0.0;
            bool fireCalled = false;     // the word to fire was said and the beam has not left the target since
            double onTargetAt = 0.0;     // the last moment the beam met the target it leads to
            double onSince = -1.0;       // when the shot line came onto the target it leads to, -1 while off
            std::vector<int64_t> health; // each target's health at the last look
            // The shot as the game fires it, read from the weapon's setup and the fight's action.
            bool single = false;       // one pellet, thrown at random within the spread
            int64_t pellets = 0;       // pellets per shot, as the setup counts them
            double spread = 0.0;       // degrees
            bool uniform = true;       // a fixed grid of pellets, else each thrown at random within the cone
            double range = 0.0;        // how far a pellet flies, cm
            double magnetism = 0.0;    // the aim assist's reach about the line, cm
            int64_t aimSetting = 0;    // the aiming setting: 0 off, 1 assist, 2 automatic
            std::wstring muzzleSocket; // the socket of the weapon's mesh the pellets leave from
        };
        Combat g_combat;
        bool g_aimSound = true;
        bool g_modifierOn = false; // the fight's camera modifier, at the last look
        double g_modifierAt = 0.0;

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

        bool ReadVecProperty(UObject* object, const wchar_t* name, Vec& out)
        {
            auto* prop = obj::FindProperty(object, name);
            return prop && ReadVec(obj::ValuePtr(object, prop), prop, out);
        }

        bool CallForVec(UObject* target, const wchar_t* function, Vec& out)
        {
            bool ok = false;
            obj::CallReturn(target, function,
                            [&](void* params, FProperty* returnValue) { ok = ReadVec(obj::ValuePtrAt(params, returnValue), returnValue, out); });
            return ok;
        }

        double NormalizeDegrees(double a)
        {
            a = std::fmod(a + 180.0, 360.0);
            if (a < 0) a += 360.0;
            return a - 180.0;
        }

        double Distance(const Vec& a, const Vec& b)
        {
            return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
        }

        Vec Forward(double pitch, double yaw)
        {
            const double p = pitch * std::numbers::pi / 180.0;
            const double y = yaw * std::numbers::pi / 180.0;
            return Vec{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
        }

        UObject* Pawn()
        {
            UObject* controller = obj::LocalPlayerController();
            UObject* pawn = nullptr;
            if (controller) obj::ReadObject(controller, L"Pawn", pawn);
            return obj::IsLive(pawn) ? pawn : nullptr;
        }

        UObject* CameraManager()
        {
            UObject* controller = obj::LocalPlayerController();
            UObject* camera = nullptr;
            if (controller) obj::ReadObject(controller, L"PlayerCameraManager", camera);
            return obj::IsLive(camera) ? camera : nullptr;
        }

        // The weapon in the character's hands.
        UObject* Weapon(UObject* pawn)
        {
            std::vector<UObject*> items;
            if (pawn) obj::ReadObjectArray(pawn, L"CurrentHeldItems", items);
            for (UObject* item : items)
                if (obj::IsLive(item) && obj::IsA(item, L"HeldWeapon")) return item;
            return nullptr;
        }

        // Where the pellets leave the weapon: the socket its setup names on its mesh.
        bool MuzzleLocation(UObject* weapon, Vec& out)
        {
            const Combat& c = g_combat;
            UObject* mesh = nullptr;
            if (c.muzzleSocket.empty() || c.muzzleSocket == L"None" || !obj::ReadObject(weapon, L"Mesh", mesh) || !obj::IsLive(mesh)) return false;
            auto* fn = obj::FindFunction(mesh, L"GetSocketLocation");
            if (!fn) return false;
            bool ok = false;
            obj::Call(
                mesh, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"InSocketName")
                            std::construct_at(reinterpret_cast<RC::Unreal::FName*>(obj::ValuePtrAt(params, prop)), c.muzzleSocket.c_str(),
                                              RC::Unreal::FNAME_Add);
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") ok = ReadVec(obj::ValuePtrAt(params, prop), prop, out);
                    }
                });
            return ok;
        }

        // The line a shot takes. The game fires from the socket its weapon setup names on the
        // weapon's mesh, along the rotation of the torch on the weapon, and spreads the
        // pellets about that line; the torch beam the game shows as the aim is that very
        // line. With no torch to read, the camera's line of sight, which the aim follows.
        Beam AimLine(UObject* pawn)
        {
            Beam b;
            UObject* weapon = Weapon(pawn);
            UObject* torch = nullptr;
            if (weapon && obj::ReadObject(weapon, L"TorchLightComponent", torch) && obj::IsLive(torch))
            {
                Vec at;
                if (CallForVec(torch, L"K2_GetComponentLocation", at) && CallForVec(torch, L"GetForwardVector", b.forward) &&
                    CallForVec(torch, L"GetRightVector", b.right) && CallForVec(torch, L"GetUpVector", b.up))
                {
                    b.from = at;
                    b.yaw = std::atan2(b.forward.y, b.forward.x) * 180.0 / std::numbers::pi;
                    b.pitch = std::asin(std::clamp(b.forward.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
                    b.source = L"torch";
                    b.placed = true;
                    Vec muzzle;
                    if (MuzzleLocation(weapon, muzzle))
                    {
                        b.from = muzzle;
                        b.source = L"muzzle";
                    }
                }
            }
            if (UObject* camera = CameraManager(); camera && CallForVec(camera, L"GetCameraLocation", b.cameraFrom))
            {
                obj::CallReturn(camera, L"GetCameraRotation",
                                [&](void* params, FProperty* returnValue)
                                {
                                    void* value = obj::ValuePtrAt(params, returnValue);
                                    b.cameraPlaced = value && obj::ReadFloatAt(value, obj::StructMember(returnValue, L"Pitch"), b.cameraPitch) &&
                                                     obj::ReadFloatAt(value, obj::StructMember(returnValue, L"Yaw"), b.cameraYaw);
                                });
            }
            if (!b.placed && b.cameraPlaced)
            {
                b.from = b.cameraFrom;
                b.pitch = b.cameraPitch;
                b.yaw = b.cameraYaw;
                b.forward = Forward(b.pitch, b.yaw);
                const double y = b.yaw * std::numbers::pi / 180.0;
                b.right = Vec{-std::sin(y), std::cos(y), 0.0};
                b.up = Vec{b.forward.y * b.right.z - b.forward.z * b.right.y, b.forward.z * b.right.x - b.forward.x * b.right.z,
                           b.forward.x * b.right.y - b.forward.y * b.right.x};
                b.source = L"camera";
                b.placed = true;
            }
            return b;
        }

        // The werewolves are the characters built on the wolf blueprints; everything that is not
        // a character is a thing.
        Kind KindOf(UObject* actor)
        {
            if (!obj::IsLive(actor)) return Kind::Person;
            if (!obj::IsA(actor, L"Pawn")) return Kind::Object;
            const auto cls = str::ToLower(obj::FullName(actor));
            const auto name = str::ToLower(obj::ObjectName(actor));
            if (cls.find(L"wolf") != std::wstring::npos || name.find(L"wolf") != std::wstring::npos) return Kind::Creature;
            return Kind::Person;
        }

        const wchar_t* KindKey(Kind kind)
        {
            switch (kind)
            {
            case Kind::Creature: return L"combat.kind.creature";
            case Kind::Object: return L"combat.kind.object";
            default: return L"combat.kind.target";
            }
        }

        // The middle of what the target's collision fills, and how far it reaches from there.
        bool Centre(UObject* actor, Vec& out, Vec* reach = nullptr)
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
                if (read && (extent.x > 1.0 || extent.y > 1.0 || extent.z > 1.0)) break;
            }
            if (!read) return false;
            out = origin;
            if (reach) *reach = extent;
            return true;
        }

        // The bodies of a target a shot can meet: every component of it that can collide,
        // asked for by class. Their names are the blueprint's own and cannot be guessed: the
        // bottles of the shooting range carry their hit body as a capsule named "Capsule",
        // five centimetres wide, under a mesh that has no collision of its own, while the
        // melons' mesh collides by itself.
        std::vector<UObject*> Bodies(UObject* actor)
        {
            // A shot's pellets ask for the same actor's bodies by the score; they are found
            // once a frame.
            static std::map<UObject*, std::pair<unsigned long long, std::vector<UObject*>>> found;
            const auto frame = gamethread::FrameCount();
            if (auto it = found.find(actor); it != found.end() && it->second.first == frame) return it->second.second;
            std::vector<UObject*> bodies;
            auto* fn = obj::IsLive(actor) ? obj::FindFunction(actor, L"K2_GetComponentsByClass") : nullptr;
            UObject* primitive = obj::FindObject(L"/Script/Engine.PrimitiveComponent");
            if (!fn || !primitive) return bodies;
            obj::Call(
                actor, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ComponentClass") *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = primitive;
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (!prop || prop->GetName() != L"ReturnValue") continue;
                        obj::ForEachArrayElement(params, prop,
                                                 [&](void* element, FProperty* inner)
                                                 {
                                                     UObject* component = nullptr;
                                                     if (obj::ReadObjectAt(element, inner, component) && obj::IsLive(component) &&
                                                         obj::CallForBool(component, L"K2_IsCollisionEnabled"))
                                                         bodies.push_back(component);
                                                 });
                    }
                });
            if (found.size() > 64) found.clear();
            found[actor] = {frame, bodies};
            return bodies;
        }

        std::wstring BodiesText(UObject* actor)
        {
            std::vector<std::wstring> names;
            for (UObject* body : Bodies(actor))
                names.push_back(std::format(L"{} ({})", obj::ObjectName(body), obj::ClassName(body)));
            return names.empty() ? std::wstring(L"none") : str::Join(names, L", ");
        }

        // Whether a line meets one of the target's bodies: the engine's own test against the
        // target's collision, against its shapes first and its triangles when it has no
        // shapes.
        bool Meets(UObject* actor, const Vec& from, const Vec& to)
        {
            for (UObject* body : Bodies(actor))
            {
                auto* fn = obj::FindFunction(body, L"K2_LineTraceComponent");
                if (!fn) continue;
                // A mesh with no shapes of its own answers only to its triangles; a shape
                // answers to the plain trace.
                const bool mesh = obj::IsA(body, L"MeshComponent");
                for (const bool complex : {false, true})
                {
                    if (complex && !mesh) break;
                    bool hit = false;
                    obj::Call(
                        body, fn,
                        [&](void* params)
                        {
                            for (auto* prop : fn->ForEachProperty())
                            {
                                if (!prop) continue;
                                const auto name = prop->GetName();
                                if (name == L"TraceStart")
                                    WriteVec(obj::ValuePtrAt(params, prop), prop, from);
                                else if (name == L"TraceEnd")
                                    WriteVec(obj::ValuePtrAt(params, prop), prop, to);
                                else if (name == L"bTraceComplex")
                                    *static_cast<bool*>(obj::ValuePtrAt(params, prop)) = complex;
                            }
                        },
                        [&](void* params)
                        {
                            for (auto* prop : fn->ForEachProperty())
                            {
                                if (prop && prop->GetName() == L"ReturnValue") obj::ReadBoolAt(params, prop, hit);
                            }
                        });
                    if (hit) return true;
                }
            }
            return false;
        }

        // Degrees between a line and the line from its start to a point.
        double AngleOff(const Vec& from, const Vec& forward, const Vec& to)
        {
            const double d = Distance(from, to);
            if (d <= 0.0) return 0.0;
            const double along = ((to.x - from.x) * forward.x + (to.y - from.y) * forward.y + (to.z - from.z) * forward.z) / d;
            return std::acos(std::clamp(along, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        }

        // How far a point stands from a line, measured square to it.
        double Beside(const Vec& from, const Vec& forward, const Vec& to)
        {
            const double along = (to.x - from.x) * forward.x + (to.y - from.y) * forward.y + (to.z - from.z) * forward.z;
            const Vec foot{from.x + forward.x * along, from.y + forward.y * along, from.z + forward.z * along};
            return Distance(foot, to);
        }

        bool MeetsAlong(UObject* actor, const Vec& from, const Vec& forward)
        {
            const Vec far{from.x + forward.x * 20000.0, from.y + forward.y * 20000.0, from.z + forward.z * 20000.0};
            return Meets(actor, from, far);
        }

        // The pellets of one shot, as the game throws them: from the muzzle, about the torch's
        // own axes, a grid of n by n pellets, n from the pellet count, a step of the spread
        // over n degrees apart, each flown over the weapon's range and stopped by the first
        // body it meets. The count of them that would meet the target's body is what a shot
        // fired now would do to it. A weapon that throws its pellets at random within the
        // spread, or a single one, has only its middle line to judge by.
        int Pellets(const Beam& beam, UObject* actor, int& of)
        {
            const Combat& c = g_combat;
            const double range = c.range > 0.0 ? c.range : 20000.0;
            const auto flies = [&](double pitchOff, double yawOff)
            {
                const double p = pitchOff * std::numbers::pi / 180.0;
                const double y = yawOff * std::numbers::pi / 180.0;
                const Vec v{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
                const Vec dir{beam.forward.x * v.x + beam.right.x * v.y + beam.up.x * v.z, beam.forward.y * v.x + beam.right.y * v.y + beam.up.y * v.z,
                              beam.forward.z * v.x + beam.right.z * v.y + beam.up.z * v.z};
                const Vec end{beam.from.x + dir.x * range, beam.from.y + dir.y * range, beam.from.z + dir.z * range};
                return Meets(actor, beam.from, end);
            };
            if (c.single || !c.uniform || c.pellets <= 0)
            {
                of = 1;
                return flies(0.0, 0.0) ? 1 : 0;
            }
            const int n = std::max(1, static_cast<int>(std::lrint(2.0 * std::sqrt(static_cast<double>(c.pellets)) - 0.5)) >> 1);
            const double step = c.spread / n;
            of = n * n;
            int hits = 0;
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j)
                    if (flies((n / 2 - i) * step, (n / 2 - j) * step)) ++hits;
            return hits;
        }

        // Whether the shot would take the target from a line swayed a little to either side
        // and up or down: the aim sways by about a tenth of a degree on its own, breathing
        // with the character, so a target at the very edge of the pellets' reach is hit and
        // missed by turns while the stick stays still. A hit that holds through that sway is
        // one a shot fired a moment later would still make.
        bool Robust(const Beam& beam, UObject* actor)
        {
            const double sway = 0.15 * std::numbers::pi / 180.0;
            for (const int way : {0, 1, 2, 3})
            {
                Beam b = beam;
                const double c = std::cos(sway), n = (way % 2 == 0 ? 1.0 : -1.0) * std::sin(sway);
                if (way < 2)
                {
                    b.forward = Vec{beam.forward.x * c + beam.right.x * n, beam.forward.y * c + beam.right.y * n, beam.forward.z * c + beam.right.z * n};
                    b.right = Vec{beam.right.x * c - beam.forward.x * n, beam.right.y * c - beam.forward.y * n, beam.right.z * c - beam.forward.z * n};
                }
                else
                {
                    b.forward = Vec{beam.forward.x * c + beam.up.x * n, beam.forward.y * c + beam.up.y * n, beam.forward.z * c + beam.up.z * n};
                    b.up = Vec{beam.up.x * c - beam.forward.x * n, beam.up.y * c - beam.forward.y * n, beam.up.z * c - beam.forward.z * n};
                }
                int of = 0;
                if (Pellets(b, actor, of) == 0) return false;
            }
            return true;
        }

        // Where a target stands against the shot; with `others`, against the camera's line of
        // sight as well, for the log; with `robust`, whether the hit survives the sway.
        Sight Look(const Beam& beam, UObject* actor, bool others = false, bool robust = false)
        {
            Sight s;
            Vec centre, reach;
            if (!beam.placed || !Centre(actor, centre, &reach)) return s;
            const Combat& c = g_combat;
            const Vec& eye = beam.from;
            const double flat = std::sqrt((centre.x - eye.x) * (centre.x - eye.x) + (centre.y - eye.y) * (centre.y - eye.y));
            const double bearing = std::atan2(centre.y - eye.y, centre.x - eye.x) * 180.0 / std::numbers::pi;
            const double elevation = std::atan2(centre.z - eye.z, flat) * 180.0 / std::numbers::pi;
            s.horizontal = NormalizeDegrees(bearing - beam.yaw);
            s.vertical = NormalizeDegrees(elevation - beam.pitch);
            const double d = Distance(eye, centre);
            s.metres = d / 100.0;
            s.angle = AngleOff(eye, beam.forward, centre);
            const double radius = std::sqrt(reach.x * reach.x + reach.y * reach.y + reach.z * reach.z);
            const double radiusDegrees = d > 0.0 ? std::atan(radius / d) * 180.0 / std::numbers::pi : 90.0;
            // The pellets are traced only where the shot could reach: within its spread of the
            // line, plus the target's own size.
            if (s.angle <= c.spread + radiusDegrees + 1.0)
                s.pellets = Pellets(beam, actor, s.of);
            else
                s.of = c.single || !c.uniform || c.pellets <= 0 ? 1 : 0;
            s.onTarget = s.pellets > 0;
            if (robust && s.onTarget) s.solid = Robust(beam, actor);
            s.assisted = c.aimSetting == 1 && c.magnetism > 0.0 && s.angle < 90.0 && Beside(eye, beam.forward, centre) - radius <= c.magnetism;
            if (others && beam.cameraPlaced)
            {
                const Vec f = Forward(beam.cameraPitch, beam.cameraYaw);
                s.cameraAngle = AngleOff(beam.cameraFrom, f, centre);
                s.cameraOn = s.cameraAngle < 45.0 && MeetsAlong(actor, beam.cameraFrom, f);
            }
            s.placed = true;
            return s;
        }

        // ---- the fight as the flow holds it ------------------------------------------------

        // The actor name inside an actor reference property of an object or a struct.
        std::wstring ActorNameOf(void* container, FProperty* reference)
        {
            std::wstring name;
            void* ptr = reference ? obj::ValuePtrAt(container, reference) : nullptr;
            if (ptr) obj::ReadStringAt(ptr, obj::StructMember(reference, L"ActorName"), name);
            return name;
        }

        std::wstring ActorNameOf(UObject* object, const wchar_t* property)
        {
            auto* prop = obj::FindProperty(object, property);
            return prop ? ActorNameOf(object, prop) : std::wstring();
        }

        std::vector<std::wstring> TargetNames(UObject* action)
        {
            std::vector<std::wstring> names;
            obj::ForEachArrayElement(action, obj::FindProperty(action, L"PossibleTargets"),
                                     [&](void* element, FProperty* inner) { names.push_back(ActorNameOf(element, obj::StructMember(inner, L"Target"))); });
            return names;
        }

        // The state of the flow the action sits in: the action lives in the state's schema.
        std::wstring StateLabel(UObject* action)
        {
            UObject* schema = obj::Outer(action);
            UObject* state = schema ? obj::Outer(schema) : nullptr;
            std::wstring label;
            if (state && obj::IsLive(state)) obj::ReadString(state, L"Label", label);
            return label;
        }

        // The name the flow knows an actor by: the entry in its actor register when it has one,
        // else its own name. An actor's own name can carry a number the flow's name does not
        // ("Ryan_2" for the flow's "Ryan").
        std::wstring RegisteredName(UObject* actor)
        {
            std::wstring name;
            auto* prop = obj::IsLive(actor) ? obj::FindProperty(actor, L"ActorRegister") : nullptr;
            if (prop && obj::PropertyTypeName(prop) == L"StructProperty")
                obj::ReadStringAt(obj::ValuePtr(actor, prop), obj::StructMember(prop, L"ActorName"), name);
            return name;
        }

        std::wstring Stem(std::wstring name)
        {
            const size_t underscore = name.find_last_of(L'_');
            if (underscore == std::wstring::npos || underscore + 1 >= name.size()) return name;
            for (size_t i = underscore + 1; i < name.size(); ++i)
                if (name[i] < L'0' || name[i] > L'9') return name;
            name.erase(underscore);
            return name;
        }

        bool ActorLocation(UObject* actor, Vec& out)
        {
            return obj::IsLive(actor) && CallForVec(actor, L"K2_GetActorLocation", out);
        }

        // The level's actors by the names the flow uses. A name can belong to more than one
        // actor (a second Silas_Wolf stood two hundred metres off in another part of the
        // level), so of several the one nearest the character is taken.
        struct Actors
        {
            std::multimap<std::wstring, UObject*> registered; // by their register entry
            std::multimap<std::wstring, UObject*> named;      // by their own name
            std::multimap<std::wstring, UObject*> stemmed;    // by their own name without its number
            Vec here;                                         // where the character stands

            UObject* Nearest(const std::multimap<std::wstring, UObject*>& table, const std::wstring& name, std::wstring& note) const
            {
                UObject* best = nullptr;
                UObject* bestHidden = nullptr;
                double bestDistance = 0.0, bestHiddenDistance = 0.0;
                std::vector<std::wstring> seen;
                const auto [begin, end] = table.equal_range(name);
                for (auto it = begin; it != end; ++it)
                {
                    Vec at;
                    if (!ActorLocation(it->second, at)) continue;
                    bool hidden = false;
                    obj::ReadBool(it->second, L"bHidden", hidden);
                    const double distance = Distance(here, at);
                    seen.push_back(std::format(L"{} at {:.0f} m{}", obj::ObjectName(it->second), distance / 100.0, hidden ? L", hidden" : L""));
                    UObject*& slot = hidden ? bestHidden : best;
                    double& slotDistance = hidden ? bestHiddenDistance : bestDistance;
                    if (!slot || distance < slotDistance)
                    {
                        slot = it->second;
                        slotDistance = distance;
                    }
                }
                if (seen.size() > 1) note = L", the nearest of " + str::Join(seen, L"; ");
                return best ? best : bestHidden;
            }

            UObject* Find(const std::wstring& name, std::wstring& how) const
            {
                std::wstring note;
                if (UObject* actor = Nearest(registered, name, note))
                {
                    how = L"by its register" + note;
                    return actor;
                }
                if (UObject* actor = Nearest(named, name, note))
                {
                    how = L"by its name" + note;
                    return actor;
                }
                if (UObject* actor = Nearest(stemmed, name, note))
                {
                    how = L"by its name without its number" + note;
                    return actor;
                }
                how.clear();
                return nullptr;
            }

            bool Has(const std::wstring& name) const
            {
                std::wstring how;
                return Find(name, how) != nullptr;
            }
        };

        Actors LevelActors(UObject* pawn)
        {
            Actors actors;
            ActorLocation(pawn, actors.here);
            for (UObject* actor : obj::FindAllLive(L"Actor"))
            {
                const auto registered = RegisteredName(actor);
                if (!registered.empty()) actors.registered.emplace(registered, actor);
                const auto name = obj::ObjectName(actor);
                actors.named.emplace(name, actor);
                actors.stemmed.emplace(Stem(name), actor);
            }
            return actors;
        }

        // The targets' health added up, and which of them have none left.
        int64_t Health(Combat& c)
        {
            if (!obj::IsLive(c.status)) return -1;
            int64_t sum = 0;
            int index = 0;
            std::vector<int64_t> health;
            obj::ForEachArrayElement(c.status, obj::FindProperty(c.status, L"TargetsHealth"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         int64_t current = 0;
                                         obj::ReadIntAt(element, obj::StructMember(inner, L"CurrentHealth"), current);
                                         sum += std::max<int64_t>(0, current);
                                         health.push_back(current);
                                         for (auto& t : c.targets)
                                             if (t.index == index) t.dead = current <= 0;
                                         ++index;
                                     });
            // Which target a shot took health from is what the shot line is judged by.
            for (size_t i = 0; i < health.size() && i < c.health.size(); ++i)
                if (health[i] < c.health[i]) log::Info(L"combat: target {} lost health, {} to {}", i, c.health[i], health[i]);
            c.health = health;
            return sum;
        }

        std::wstring HealthText(UObject* status)
        {
            std::vector<std::wstring> parts;
            obj::ForEachArrayElement(status, obj::FindProperty(status, L"TargetsHealth"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         int64_t current = 0, max = 0;
                                         obj::ReadIntAt(element, obj::StructMember(inner, L"CurrentHealth"), current);
                                         obj::ReadIntAt(element, obj::StructMember(inner, L"MaxHealth"), max);
                                         parts.push_back(std::format(L"{}/{}", current, max));
                                     });
            std::vector<std::wstring> hits;
            obj::ForEachArrayElement(status, obj::FindProperty(status, L"LastHitTargets"),
                                     [&](void* element, FProperty* inner)
                                     {
                                         bool hit = false;
                                         obj::ReadBoolAt(element, inner, hit);
                                         hits.push_back(hit ? L"hit" : L"-");
                                     });
            return std::format(L"health [{}] last hit [{}]", str::Join(parts, L" "), str::Join(hits, L" "));
        }

        int64_t ShotsFired(UObject* status)
        {
            int64_t shots = 0;
            if (obj::IsLive(status)) obj::ReadInt(status, L"ShotsFired", shots);
            return shots;
        }

        // The flow action running this fight. Every loaded action of the kind is a candidate;
        // the one for the character in the player's hands whose targets stand in the level
        // wins, and every one is written to the log with its state.
        // The flow action running this fight. The fight's replicator carries the path of the
        // state it runs in, which settles it; without one, the action for the character in the
        // player's hands whose targets stand in the level. Every candidate goes to the log.
        UObject* FindAction(UObject* pawn, const Actors& actors, const std::wstring& wantedState)
        {
            std::wstring pawnName = RegisteredName(pawn);
            if (pawnName.empty() && pawn) pawnName = Stem(obj::ObjectName(pawn));
            UObject* best = nullptr;
            int bestScore = -1;
            for (UObject* action : obj::FindAllLive(L"GFActionRealtimeCombatSMG026"))
            {
                const auto character = ActorNameOf(action, L"CharacterRef");
                const auto names = TargetNames(action);
                const auto state = StateLabel(action);
                int standing = 0;
                for (const auto& name : names)
                    if (actors.Has(name)) ++standing;
                int score = 0;
                if (!wantedState.empty() && str::EqualsNoCase(state, wantedState)) score += 8;
                if (!pawnName.empty() && str::EqualsNoCase(character, pawnName)) score += 4;
                if (!names.empty() && standing == static_cast<int>(names.size())) score += 2;
                if (standing > 0) score += 1;
                log::Info(L"combat: action {} in state \"{}\" for {} with {}: {} of {} targets standing ({}){}", obj::ObjectName(action), state, character,
                          ActorNameOf(action, L"WeaponRef"), standing, names.size(), str::Join(names, L", "), score >= 8 ? L" <- the replicator's state" : L"");
                // A later action wins a tie: the flow's sub-assets load in their order.
                if (score >= bestScore)
                {
                    best = action;
                    bestScore = score;
                }
            }
            return best;
        }

        // The game's count for this fight: the newest status object whose targets are as
        // many as the action's.
        UObject* FindStatus(size_t targetCount)
        {
            UObject* best = nullptr;
            for (UObject* status : obj::FindAllLive(L"GFActionRealtimeCombatStatusSMG026"))
            {
                size_t count = 0;
                obj::ForEachArrayElement(status, obj::FindProperty(status, L"TargetsHealth"), [&](void*, FProperty*) { ++count; });
                std::wstring name;
                obj::ReadString(status, L"Name", name);
                log::Info(L"combat: status {} \"{}\" shots {} {}", obj::ObjectName(status), name, ShotsFired(status), HealthText(status));
                if (!best || count == targetCount || targetCount == 0) best = status;
            }
            return best;
        }

        // The fight's replicator, and the path of the flow state it serves.
        UObject* FindReplicator(std::wstring& path)
        {
            UObject* last = nullptr;
            for (UObject* replicator : obj::FindAllLive(L"RealtimeCombatSMG026_Replicator"))
            {
                if (obj::CallForBool(replicator, L"IsActorBeingDestroyed")) continue;
                std::wstring here;
                Vec pos;
                obj::ReadString(replicator, L"Path", here);
                ReadVecProperty(replicator, L"TargetPos", pos);
                log::Info(L"combat: replicator {} path \"{}\" target pos ({:.0f}, {:.0f}, {:.0f})", obj::ObjectName(replicator), here, pos.x, pos.y, pos.z);
                last = replicator;
                path = here;
            }
            return last;
        }

        // The weapon in the character's hands and the shells the game gives it.
        std::wstring WeaponText(UObject* pawn)
        {
            UObject* weapon = Weapon(pawn);
            if (!weapon) return L"no weapon in hand";
            UObject* setup = nullptr;
            int64_t ammo = 0, pellets = 0;
            double spread = 0.0;
            bool light = false;
            if (obj::ReadObject(weapon, L"WeaponSetup", setup) && obj::IsLive(setup))
            {
                obj::ReadInt(setup, L"Ammo", ammo);
                obj::ReadInt(setup, L"ProjectilesPerShot", pellets);
                obj::ReadFloat(setup, L"Spread", spread);
            }
            obj::ReadBool(weapon, L"bHeldItemLightOn", light);
            return std::format(L"{} ({}: {} shells, {} pellets, spread {:.1f}, light {})", obj::ObjectName(weapon),
                               obj::IsLive(setup) ? obj::ObjectName(setup) : L"?", ammo, pellets, spread, light ? L"on" : L"off");
        }

        // Whether the fight's own camera modifier is at work on the player's camera.
        bool ModifierOn()
        {
            UObject* camera = CameraManager();
            std::vector<UObject*> modifiers;
            if (!camera || !obj::ReadObjectArray(camera, L"ModifierList", modifiers)) return false;
            for (UObject* modifier : modifiers)
            {
                if (!obj::IsLive(modifier) || !obj::IsA(modifier, L"CameraModifier_RealtimeCombatSMG026")) continue;
                if (!obj::CallForBool(modifier, L"IsDisabled")) return true;
            }
            return false;
        }

        // ---- the fight ------------------------------------------------------------------------

        std::vector<const Target*> Standing(const Combat& c)
        {
            std::vector<const Target*> out;
            for (const auto& t : c.targets)
                if (!t.dead && obj::IsLive(t.actor)) out.push_back(&t);
            return out;
        }

        std::wstring Metres(double metres)
        {
            return std::to_wstring(std::max<long>(1, std::lround(metres)));
        }

        // The side of the beam a target stands on, in words.
        std::wstring PlaceText(const Sight& s)
        {
            if (s.solid) return locale::Mod(L"combat.on");
            std::vector<std::wstring> words;
            if (std::fabs(s.horizontal) > 90.0)
                words.push_back(locale::Mod(L"combat.behind"));
            else if (std::fabs(s.horizontal) >= 2.0)
                words.push_back(locale::Mod(s.horizontal > 0 ? L"combat.right" : L"combat.left"));
            if (std::fabs(s.vertical) >= 2.0 && std::fabs(s.horizontal) <= 90.0)
                words.push_back(locale::Mod(s.vertical > 0 ? L"combat.above" : L"combat.below"));
            if (words.empty()) words.push_back(locale::Mod(L"combat.ahead"));
            return str::Join(words, L" ");
        }

        std::wstring WhereText(const Sight& s)
        {
            if (!s.placed) return locale::Mod(L"explore.notarget");
            if (s.solid) return locale::Mod(L"combat.on");
            return locale::Mod(L"combat.where", Metres(s.metres) + L" " + locale::Mod(L"combat.m") + L", " + PlaceText(s));
        }

        // A target as it is listed: what it is, how far, on which side.
        std::wstring ItemText(const Target& t, const Sight& s)
        {
            return locale::Mod(L"combat.item", std::vector<std::wstring>{locale::Mod(KindKey(t.kind)), Metres(s.metres), PlaceText(s)});
        }

        std::wstring TargetText(const Combat& c, const Target& t)
        {
            const auto standing = Standing(c);
            if (standing.size() <= 1) return {};
            int place = 0;
            for (size_t i = 0; i < standing.size(); ++i)
                if (standing[i] == &t) place = static_cast<int>(i) + 1;
            return locale::Mod(L"combat.target",
                               std::vector<std::wstring>{locale::Mod(KindKey(t.kind)), std::to_wstring(place), std::to_wstring(standing.size())});
        }

        const Target* Led(const Combat& c)
        {
            for (const auto& t : c.targets)
                if (t.index == c.leading && !t.dead && obj::IsLive(t.actor)) return &t;
            return nullptr;
        }

        void Quiet(Combat& c, double seconds)
        {
            c.quietUntil = std::max(c.quietUntil, gamethread::NowSeconds() + seconds);
        }

        void Hit(Combat& c, const wchar_t* how)
        {
            if (c.hitShot == c.shots && c.anyHit) return;
            c.hitShot = c.shots;
            c.anyHit = true;
            c.shotAt = -1.0;
            log::Info(L"combat: hit ({}) on shot {}", how, c.shots);
            sounds::Play(sounds::Cue::Confirm);
            Quiet(c, 0.35);
            if (cfg::Detailed()) speech::Announce(locale::Mod(L"combat.hit"));
        }

        void Miss(Combat& c)
        {
            c.shotAt = -1.0;
            log::Info(L"combat: miss on shot {}", c.shots);
            sounds::Play(sounds::Cue::Fail);
            Quiet(c, 0.35);
            if (cfg::Detailed()) speech::Announce(locale::Mod(L"combat.miss"));
        }

        // Where the beam points against every target, and where the game's own aim point lies
        // against the beam.
        void LogAim(Combat& c, const wchar_t* when)
        {
            const Beam beam = AimLine(Pawn());
            if (!beam.placed) return;
            std::vector<std::wstring> parts;
            for (const Target* t : Standing(c))
            {
                const Sight s = Look(beam, t->actor, true, true);
                if (!s.placed) continue;
                parts.push_back(std::format(L"target {} {:.1f} deg {}, {:.1f} deg {}, {:.1f} off, {:.1f} m, {} of {} pellets, {}{}{} (camera {:.1f} off, {})",
                                            t->index, std::fabs(s.horizontal), s.horizontal >= 0 ? L"right" : L"left", std::fabs(s.vertical),
                                            s.vertical >= 0 ? L"up" : L"down", s.angle, s.metres, s.pellets, s.of,
                                            s.onTarget ? (s.solid ? L"on target and sure" : L"on target at the edge") : L"off target",
                                            s.assisted ? L", in the assist's reach" : L"", L"", s.cameraAngle, s.cameraOn ? L"on" : L"off"));
            }
            std::wstring aim = L"no aim point";
            Vec pos;
            if (obj::IsLive(c.replicator) && ReadVecProperty(c.replicator, L"TargetPos", pos) && (pos.x != 0.0 || pos.y != 0.0 || pos.z != 0.0))
            {
                aim = std::format(L"aim point ({:.0f}, {:.0f}, {:.0f}) {:.1f} m off, {:.1f} deg off the line", pos.x, pos.y, pos.z,
                                  Distance(beam.from, pos) / 100.0, AngleOff(beam.from, beam.forward, pos));
            }
            log::Info(L"combat: {}: {} yaw {:.1f} pitch {:.1f} (camera yaw {:.1f} pitch {:.1f}); {}; {}", when, beam.source, beam.yaw, beam.pitch,
                      beam.cameraYaw, beam.cameraPitch, str::Join(parts, L"; "), aim);
        }

        void Shot(Combat& c, int64_t number, const wchar_t* how)
        {
            if (number <= c.shots) return;
            c.shots = number;
            c.shotAt = gamethread::NowSeconds();
            c.lastShotAt = c.shotAt;
            c.activeAt = c.shotAt;
            log::Info(L"combat: shot {} ({}) at {:.2f} s{}", number, how, c.shotAt - c.startedAt,
                      obj::IsLive(c.status) ? L", " + HealthText(c.status) : std::wstring());
            LogAim(c, L"at the shot");
        }

        // A shot is judged by the targets' health: a hit takes some, a miss leaves it.
        void Judge(Combat& c, bool final)
        {
            const int64_t sum = Health(c);
            if (sum >= 0 && c.healthSum >= 0 && sum < c.healthSum)
            {
                c.healthSum = sum;
                Hit(c, L"health fell");
                return;
            }
            if (sum >= 0) c.healthSum = sum;
            if (c.shotAt >= 0.0 && (final || gamethread::NowSeconds() - c.shotAt > 0.45)) Miss(c);
        }

        void End(const wchar_t* why)
        {
            Combat& c = g_combat;
            if (!c.on) return;
            const double elapsed = c.activeAt - c.startedAt;
            Judge(c, true);
            // Time ran out when the fight closed at its limit with no hit, and not on the heels
            // of a shot.
            const bool afterShot = c.lastShotAt >= 0.0 && c.activeAt - c.lastShotAt < 1.0;
            if (!c.anyHit && c.hasTimeLimit && !afterShot && elapsed >= c.timeLimit - 0.5)
            {
                log::Info(L"combat: the time ran out");
                // The tone of a loss, and briefly nothing more, as with a missed shot.
                sounds::Play(sounds::Cue::Fail);
                if (cfg::Detailed()) speech::Announce(locale::Mod(L"combat.timeout"));
            }
            log::Info(L"combat: over ({}) after {:.2f} s, {} shot(s), {}", why, elapsed, c.shots, c.anyHit ? L"a hit" : L"no hit");
            g_combat = Combat{};
        }

        void Begin(const wchar_t* sign)
        {
            Combat& c = g_combat;
            if (c.on) return;
            c.on = true;
            c.startedAt = gamethread::NowSeconds();
            c.activeAt = c.startedAt;
            const int64_t setting = ui::GameSettingValue(L"CombatAimSetting");
            c.automatic = setting == 2;
            UObject* pawn = Pawn();

            // The level's actors by the names the flow uses, to find the targets it names.
            const Actors actors = LevelActors(pawn);
            // The replicator's path ends in the state of the fight: "...|doublewerewolf_combat".
            std::wstring path;
            c.replicator = FindReplicator(path);
            const size_t bar = path.find_last_of(L'|');
            const std::wstring wantedState = bar == std::wstring::npos ? path : path.substr(bar + 1);
            c.action = FindAction(pawn, actors, wantedState);
            std::vector<std::wstring> names;
            if (c.action)
            {
                obj::ReadBool(c.action, L"bHasTimeLimit", c.hasTimeLimit);
                obj::ReadFloat(c.action, L"TimeoutTime", c.timeLimit);
                int index = 0;
                for (const auto& name : TargetNames(c.action))
                {
                    Target t;
                    t.index = index++;
                    t.name = name;
                    std::wstring how;
                    t.actor = actors.Find(name, how);
                    t.kind = KindOf(t.actor);
                    names.push_back(name + (obj::IsLive(t.actor) ? std::format(L" ({} {}, {}; bodies: {})", obj::ClassName(t.actor), obj::ObjectName(t.actor),
                                                                               how, BodiesText(t.actor))
                                                                 : L" (not found)"));
                    c.targets.push_back(t);
                }
                double strength = 0.0, magnetism = 0.0, lockNear = 0.0, lockFar = 0.0, aimDistance = 0.0;
                bool torch = false, reticle = false;
                obj::ReadFloat(c.action, L"AimAssistStrength", strength);
                obj::ReadFloat(c.action, L"AimAssistMagnetismRadius", magnetism);
                obj::ReadFloat(c.action, L"MinLockDistance", lockNear);
                obj::ReadFloat(c.action, L"MaxLockDistance", lockFar);
                obj::ReadFloat(c.action, L"AimTargetDistance", aimDistance);
                obj::ReadBool(c.action, L"bTurnOnTorch", torch);
                obj::ReadBool(c.action, L"bEnableReticle", reticle);
                log::Info(L"combat: aim assist strength {:.2f} magnetism {:.0f} lock {:.0f}-{:.0f} m aim distance {:.0f} torch {} reticle {}", strength,
                          magnetism, lockNear, lockFar, aimDistance, torch, reticle);
            }
            c.status = FindStatus(c.targets.size());
            c.shots = ShotsFired(c.status);
            c.healthSum = Health(c);
            c.modifierSeen = g_modifierOn;
            // The shot as the game fires it: the weapon's setup gives the pellets, their spread
            // and the muzzle they leave from, the action how far they fly and how far the aim
            // assist reaches.
            c.aimSetting = setting;
            UObject* setup = nullptr;
            if (UObject* weapon = Weapon(pawn); weapon && obj::ReadObject(weapon, L"WeaponSetup", setup) && obj::IsLive(setup))
            {
                obj::ReadBool(setup, L"bSingleShot", c.single);
                obj::ReadInt(setup, L"ProjectilesPerShot", c.pellets);
                obj::ReadFloat(setup, L"Spread", c.spread);
                obj::ReadBool(setup, L"bUniformSpread", c.uniform);
                obj::ReadString(setup, L"MuzzleSocket", c.muzzleSocket);
            }
            int64_t channel = -1;
            if (c.action)
            {
                obj::ReadFloat(c.action, L"WeaponEffectiveRange", c.range);
                obj::ReadFloat(c.action, L"AimAssistMagnetismRadius", c.magnetism);
                obj::ReadInt(c.action, L"TargetCollisionChannel", channel);
            }
            const Beam beam = AimLine(pawn);
            log::Info(L"combat: begins on {} for {} ({}) with {}; aiming setting {}; state \"{}\"; {} target(s): {}; time limit {}; status {} at {} shot(s); "
                      L"aim read from the {}",
                      sign, pawn ? obj::ObjectName(pawn) : L"<no pawn>", RegisteredName(pawn), WeaponText(pawn), setting, wantedState, c.targets.size(),
                      str::Join(names, L", "), c.hasTimeLimit ? std::format(L"{:.1f} s", c.timeLimit) : L"none",
                      c.status ? obj::ObjectName(c.status) : L"<none>", c.shots, beam.placed ? beam.source : L"nothing");
            const int grid = c.single || !c.uniform || c.pellets <= 0
                                 ? 1
                                 : std::max(1, static_cast<int>(std::lrint(2.0 * std::sqrt(static_cast<double>(c.pellets)) - 0.5)) >> 1);
            log::Info(
                L"combat: the shot: {} pellet(s) as set up, {}, {:.2f} deg spread, from socket \"{}\" over {:.0f} m on channel {}; the aim assist reaches "
                L"{:.0f} cm about the line",
                c.pellets,
                grid > 1 ? std::format(L"a grid of {} by {} pellets {:.2f} deg apart, the outer ones {:.2f} deg off the line", grid, grid, c.spread / grid,
                                       (grid / 2) * c.spread / grid)
                         : std::wstring(c.single ? L"one pellet at random within the spread" : L"each pellet at random within the spread"),
                c.spread, c.muzzleSocket, c.range / 100.0, channel, c.magnetism);
            // Only the word for aiming: a fight leaves seconds, and they belong to the game's
            // own prompt and the aim sound. What there is to shoot at is a key away (H, F6).
            // The aim sound says as much as the word does, so briefly only the fight the game
            // aims by itself is worth a word.
            if (c.automatic)
                speech::Announce(locale::Mod(L"combat.auto"));
            else if (cfg::Detailed())
                speech::Announce(locale::Mod(L"combat.aim"));
        }

        // The aim sound: a blip in the ear on the side of the target, higher when it is above
        // the beam and lower when below, faster as the beam nears it, and a quick double ping
        // while the beam is on the target.
        void Guide(Combat& c, double now)
        {
            if (c.automatic) return;
            const Beam beam = AimLine(Pawn());
            if (!beam.placed) return;
            // The sound leads to the target the shot line is on, whichever it was leading to,
            // since that is the target a shot would take; else to the one chosen by key; else
            // to the nearest, kept until another is nearer by a clear margin in proportion,
            // so that two targets side by side do not take the sound in turns. The margin is
            // in proportion because the bottles of the shooting range stand two or three
            // degrees apart: a margin of degrees kept the sound on a bottle the line had
            // left while the next one stood on it.
            const Target* led = nullptr;
            Sight best;
            const Target* kept = nullptr;
            Sight keptSight;
            const Target* on = nullptr;
            Sight onSight;
            for (const Target* t : Standing(c))
            {
                if (c.chosen >= 0 && t->index != c.chosen) continue;
                const Sight s = Look(beam, t->actor);
                if (!s.placed) continue;
                if (s.onTarget && (!on || s.angle < onSight.angle))
                {
                    on = t;
                    onSight = s;
                }
                if (t->index == c.leading)
                {
                    kept = t;
                    keptSight = s;
                }
                if (!led || s.angle < best.angle)
                {
                    led = t;
                    best = s;
                }
            }
            if (on)
            {
                led = on;
                best = onSight;
            }
            else if (kept && led != kept && best.angle > keptSight.angle * 0.6)
            {
                led = kept;
                best = keptSight;
            }
            if (!led)
            {
                c.leading = -1;
                c.sight = Sight{};
                c.onSince = -1.0;
                return;
            }
            if (led->index != c.leading)
            {
                log::Info(L"combat: the sound leads to target {}", led->index);
                c.fireCalled = false;
                c.onSince = -1.0;
            }
            c.leading = led->index;
            // The double ping tells the moment: a shot fired now would take the target. The
            // word to fire tells more: the hit holds through the aim's own sway, so a shot a
            // moment later takes it too, and it has held for a fifth of a second, so that a
            // line swept across a target does not call it. A target at the very edge of the
            // pellets' reach, hit and missed by turns as the aim breathes, gets the ping by
            // turns and never the word; the ping keeps the side of the target, so the stick
            // can bring it in. The word comes again only after a second off the target. It
            // comes with the aim sound on or off and with the aiming setting on or off, short
            // of the one that fights by itself.
            best.solid = best.onTarget && Robust(beam, led->actor);
            c.sight = best;
            if (best.solid)
            {
                if (c.onSince < 0.0) c.onSince = now;
                c.onTargetAt = now;
                if (!c.fireCalled && now - c.onSince >= 0.2)
                {
                    c.fireCalled = true;
                    log::Info(L"combat: a shot has been sure of target {} for a fifth of a second, the word to fire", led->index);
                    if (cfg::Detailed()) speech::Announce(locale::Mod(L"combat.fire"));
                }
            }
            else
            {
                c.onSince = -1.0;
                if (c.fireCalled && now - c.onTargetAt > 1.0) c.fireCalled = false;
            }
            if (now - c.loggedAt >= 0.5)
            {
                c.loggedAt = now;
                LogAim(c, L"aim");
            }
            if (!g_aimSound || now < c.quietUntil) return;
            const double interval = best.onTarget ? 0.12 : 0.09 + 0.4 * std::clamp(best.angle / 25.0, 0.0, 1.0);
            if (now - c.blipAt < interval) return;
            c.blipAt = now;
            const double pan = std::clamp(best.horizontal / 12.0, -1.0, 1.0);
            const double level = 0.5 + std::clamp(best.vertical / 12.0, -0.5, 0.5);
            sounds::Aim(pan, level, best.onTarget, std::fabs(best.horizontal) > 90.0);
        }

        void PollImpl()
        {
            const double now = gamethread::NowSeconds();
            const auto frame = gamethread::FrameCount();
            // The fight's camera modifier is one sign of a fight, looked for now and then.
            if (frame % 10 == 0)
            {
                const bool on = ModifierOn();
                if (on != g_modifierOn)
                {
                    g_modifierOn = on;
                    g_modifierAt = now;
                    log::Info(L"combat: the fight's camera modifier is {}", on ? L"on" : L"off");
                    if (on && !g_combat.on) Begin(L"the camera modifier");
                    if (on) g_combat.modifierSeen = true;
                }
            }
            Combat& c = g_combat;
            if (!c.on) return;
            if (g_modifierOn) c.activeAt = now;
            if (obj::IsLive(c.prompt) && obj::IsWidgetShown(c.prompt, true)) c.activeAt = now;
            if (obj::IsLive(c.status)) Shot(c, ShotsFired(c.status), L"counted by the game");
            Judge(c, false);
            // The fight is over once its signs have been gone for a while: the modifier off
            // when it was seen on, the fire prompt hidden and no shot for a few seconds, the
            // character gone, or a screen of the game over the picture.
            if (c.modifierSeen && !g_modifierOn && now - g_modifierAt > 0.5)
            {
                End(L"the camera modifier went off");
                return;
            }
            // The fight's replicator lives as long as the fight: a new one is spawned for each.
            if (c.replicator)
            {
                if (!obj::IsLive(c.replicator) || obj::CallForBool(c.replicator, L"IsActorBeingDestroyed"))
                {
                    End(L"the fight's replicator is gone");
                    return;
                }
                c.activeAt = now;
            }
            if (!Pawn() || watch::CurrentScreen())
            {
                End(L"the character or the picture went");
                return;
            }
            if (now - c.activeAt > 3.0)
            {
                End(L"the fire prompt has been gone for three seconds");
                return;
            }
            Guide(c, now);
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"combat.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }

        void OnHud(const watch::HudEvent& ev)
        {
            if (ev.appeared && obj::IsA(ev.instance, L"GFRealtimeCombatReticleWidget"))
            {
                log::Info(L"combat: a crosshair {} appeared", obj::ObjectName(ev.instance));
                if (!g_combat.on) Begin(L"a crosshair");
                g_combat.reticle = ev.instance;
                g_combat.activeAt = gamethread::NowSeconds();
            }
        }

        void OnReticleConstruct(UObject* self, FFrame&)
        {
            log::Info(L"combat: a crosshair {} is built", obj::ObjectName(self));
            if (!g_combat.on) Begin(L"a crosshair");
            g_combat.reticle = self;
        }

        void OnHitMarker(UObject* self, FFrame&)
        {
            if (g_combat.on && self == g_combat.reticle) Hit(g_combat, L"hit marker");
        }

        void OnFireWeapon(UObject*, FFrame&)
        {
            Combat& c = g_combat;
            if (!c.on) return;
            log::Info(L"combat: the weapon fires");
            // Without the game's count the shots are counted here.
            if (!obj::IsLive(c.status)) Shot(c, c.shots + 1, L"fire event");
        }

        void OnWeaponSound(UObject*, FFrame& stack)
        {
            bool fired = false;
            params::Bool(stack, L"bHasFired", fired);
            log::Info(L"combat: weapon sound, {}", fired ? L"a shot" : L"an empty click");
        }

        void Step(int direction)
        {
            Combat& c = g_combat;
            if (!CombatActive()) return;
            const auto standing = Standing(c);
            if (standing.empty())
            {
                speech::Now(locale::Mod(L"explore.notarget"));
                return;
            }
            if (standing.size() == 1)
            {
                c.chosen = standing.front()->index;
                WhereIsCombatTarget();
                return;
            }
            int at = 0;
            const int current = c.chosen >= 0 ? c.chosen : c.leading;
            for (size_t i = 0; i < standing.size(); ++i)
                if (standing[i]->index == current) at = static_cast<int>(i);
            const int next = (at + direction + static_cast<int>(standing.size())) % static_cast<int>(standing.size());
            const Target& target = *standing[static_cast<size_t>(next)];
            c.chosen = target.index;
            c.leading = c.chosen;
            c.sight = Look(AimLine(Pawn()), target.actor, false, true);
            log::Info(L"combat: target {} chosen", c.chosen);
            speech::Now(str::JoinSentences({TargetText(c, target), WhereText(c.sight)}));
        }
    }

    void CombatFeature::Install()
    {
        g_aimSound = cfg::Get().aimSound;
        watch::AddHudListener(&OnHud);
        hooks::OnScript(L"RealtimeCombat_SMG026_C", L"Construct", &OnReticleConstruct);
        hooks::OnScript(L"RealtimeCombat_SMG026_C", L"PlayHitMarkerAnimation", &OnHitMarker);
        if (!hooks::OnNative(L"/Script/SMG026Runtime.RealtimeCombatSMG026_Replicator:FireWeapon", nullptr, &OnFireWeapon))
            log::Error(L"combat: the fire function is not hooked");
        if (!hooks::OnNative(L"/Script/SMG026Runtime.RealtimeCombatSMG026_Replicator:PlayWeaponSound", nullptr, &OnWeaponSound))
            log::Error(L"combat: the weapon sound function is not hooked");
        gamethread::AddPoller(L"combat", &Poll);
    }

    void CombatFeature::Describe(std::vector<std::wstring>& out)
    {
        if (!CombatActive()) return;
        const Combat& c = g_combat;
        out.push_back(locale::Mod(c.automatic ? L"combat.auto" : L"combat.aim"));
        if (c.automatic) return;
        if (const Target* led = Led(c))
        {
            const auto place = TargetText(c, *led);
            if (!place.empty()) out.push_back(place);
        }
        out.push_back(WhereText(c.sight));
    }

    void CombatFeature::Help(std::vector<std::wstring>& out)
    {
        if (!CombatActive()) return;
        const auto& s = cfg::Get();
        const bool pad = input::CurrentScheme() == input::Scheme::Gamepad;
        out.push_back(locale::Mod(L"help.combat", std::vector<std::wstring>{input::KeyForAction(L"CombatAttack"),
                                                                            pad ? input::KeyDisplayName(s.padExploreNext) : s.keyNextTarget,
                                                                            pad ? input::KeyDisplayName(s.padExplorePrevious) : s.keyPreviousTarget,
                                                                            pad ? input::KeyDisplayName(s.padExploreBeacon) : s.keyBeacon}));
    }

    void NoteCombatPrompt(UObject* prompt)
    {
        // The setup reads the flow's actions and the level's actors by the hundred, from
        // inside the game's own prompt call; a fault there must not take the game down.
        if (!g_combat.on) obj::SafeInvokeLogged(L"combat.Begin", [](void*) { Begin(L"the fire prompt"); }, nullptr);
        g_combat.prompt = prompt;
        g_combat.activeAt = gamethread::NowSeconds();
    }

    bool CombatActive()
    {
        return g_combat.on;
    }

    void NextCombatTarget()
    {
        Step(1);
    }

    void PreviousCombatTarget()
    {
        Step(-1);
    }

    void WhereIsCombatTarget()
    {
        Combat& c = g_combat;
        if (!CombatActive()) return;
        const Target* led = Led(c);
        if (led) c.sight = Look(AimLine(Pawn()), led->actor, false, true);
        speech::Now(led ? str::JoinSentences({TargetText(c, *led), WhereText(c.sight)}) : locale::Mod(L"explore.notarget"));
    }

    void ToggleAimSound()
    {
        g_aimSound = !g_aimSound;
        log::Info(L"combat: aim sound {}", g_aimSound ? L"on" : L"off");
        speech::Now(locale::Mod(g_aimSound ? L"combat.sound.on" : L"combat.sound.off"));
        // Kept in the ini, so that the next start begins the way this one ended.
        if (!cfg::Persist(L"Combat", L"AimSound", g_aimSound ? L"1" : L"0"))
            log::Error(L"combat: the aim sound setting could not be saved to QuarryAccess.ini");
    }

    bool AimSoundOn()
    {
        return g_aimSound;
    }
}
