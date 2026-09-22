"""Validation of the offline navmesh against the roads the game gave the mod. Every
hand-over in the mod's log is trilaterated from the distances the mod logged to the listed
places and use locations, the scene is recognised by the actor names among the converted
scenes, and each road the mod logged right after the hand-over (its length, its number of
corners, whether it stopped short) is set against the road navmesh.py gives from that start
to the same target.

    python roadvalidate.py [HH:MM:SS ...] [--log FILE] [--root DIR]

All hand-overs when no times are given. The log is the mod's `QuarryAccess.log` under the
game's UE4SS `Mods\\QuarryAccess` folder (QA_GAME_DIR, else Steam's default). Roads to use
locations come out a little long, since the mod aims at a spot of the use box it settles
only later; roads to places and ways should agree to within a few percent, with the same
number of corners give or take the pawn's own first point."""

import math
import os
import re
import sys

from navmesh import NavMesh
from scene_geometry import load_scene, research_root

STAMP = re.compile(r"^\[(\d\d:\d\d:\d\d)\.(\d\d\d)\] \[\w+ \] (.*)$")
PLACE = re.compile(
    r'explore: (?:use location|place) "([^"]*)" (\S+) at (\d+) cm, (-?\d+) deg'
)
ROUTE = re.compile(r'explore: route to "([^"]*)": (\d+) points, (\d+) cm(, partial)?')


def mod_log():
    game = (
        os.environ.get("QA_GAME_DIR")
        or r"C:\Program Files (x86)\Steam\steamapps\common\The Quarry"
    )
    for sub in (("ue4ss", "Mods"), ("Mods",)):
        path = os.path.join(
            game,
            "SMG026",
            "Binaries",
            "Win64",
            *sub,
            "QuarryAccess",
            "QuarryAccess.log",
        )
        if os.path.exists(path):
            return path
    return None


def seconds(h, ms):
    hh, mm, ss = h.split(":")
    return int(hh) * 3600 + int(mm) * 60 + int(ss) + int(ms) / 1000.0


def scenes(root):
    """Every scene with a persistent map unpacked: prefix -> navmesh path."""
    out = {}
    base = os.path.join(root, "navmaps", "SMG026", "Content", "Maps", "Acts")
    if not os.path.isdir(base):
        return out
    for act in os.listdir(base):
        for scene in os.listdir(os.path.join(base, act)):
            for m in os.listdir(os.path.join(base, act, scene)):
                uexp = os.path.join(base, act, scene, m, f"{m}.uexp")
                if os.path.exists(uexp):
                    out[f"{act}__{scene}__{m}"] = uexp
    return out


def handovers(lines, wanted):
    """The hand-overs of the log: (time, session index, places, routes)."""
    out = []
    session = 0
    i = 0
    while i < len(lines):
        line = lines[i]
        if "QuarryAccess log opened" in line:
            session += 1
        m = STAMP.match(line)
        if m and "the game hands the player the character" in m.group(3):
            t0 = seconds(m.group(1), m.group(2))
            if wanted and m.group(1) not in wanted:
                i += 1
                continue
            places = {}
            routes = []
            j = i + 1
            while j < len(lines):
                mm = STAMP.match(lines[j])
                if mm:
                    t = seconds(mm.group(1), mm.group(2))
                    if t - t0 > 2.0 or "hands the player" in mm.group(3):
                        break
                    p = PLACE.search(mm.group(3))
                    if p:
                        places[p.group(2)] = (
                            p.group(1),
                            float(p.group(3)),
                            float(p.group(4)),
                        )
                    r = ROUTE.search(mm.group(3))
                    if r:
                        routes.append(
                            (
                                r.group(1),
                                int(r.group(2)),
                                float(r.group(3)),
                                bool(r.group(4)),
                            )
                        )
                j += 1
            out.append((m.group(1), session, places, routes))
        i += 1
    return out


def centre(a):
    return ((a["low"][0] + a["high"][0]) / 2, (a["low"][1] + a["high"][1]) / 2, a["z"])


def trilaterate(places, actors):
    """The character's position from the distances the mod logged: a grid search over
    the actors' heights, then a refinement."""
    pts = [(centre(actors[n]), d) for n, (_, d, _) in places.items() if n in actors]
    if len(pts) < 2:
        return None
    zs = sorted(
        set(round(v["z"] / 50) * 50 for v in actors.values() if v["kind"] != "actor")
    )

    def resid(x, y, z):
        return math.sqrt(
            sum((math.dist((x, y, z), p) - dd) ** 2 for p, dd in pts) / len(pts)
        )

    cx = sum(p[0][0] for p in pts) / len(pts)
    cy = sum(p[0][1] for p in pts) / len(pts)
    best = None
    for z in zs:
        for x in range(int(cx) - 2500, int(cx) + 2500, 25):
            for y in range(int(cy) - 2500, int(cy) + 2500, 25):
                r = resid(x, y, z)
                if best is None or r < best[0]:
                    best = (r, x, y, z)
    if best is None:
        return None
    r, x, y, z = best
    for _ in range(4):
        for dx in range(-25, 26, 1):
            for dy in range(-25, 26, 1):
                rr = resid(x + dx, y + dy, z)
                if rr < r:
                    r, x, y = rr, x + dx, y + dy
    return (x, y, z), r, len(pts)


def road_length(pts):
    return sum(math.dist(pts[i - 1], pts[i]) for i in range(1, len(pts)))


def main(argv):
    root = research_root()
    log = None
    if "--root" in argv:
        root = argv[argv.index("--root") + 1]
    if "--log" in argv:
        log = argv[argv.index("--log") + 1]
    log = log or mod_log()
    if not log or not os.path.exists(log):
        print("no mod log found; give --log FILE")
        return 1
    wanted = set(a for a in argv if re.match(r"\d\d:\d\d:\d\d$", a))
    lines = open(log, encoding="utf-8", errors="ignore").read().splitlines()
    known = scenes(root)
    if not known:
        print(
            "no persistent maps under",
            os.path.join(root, "navmaps"),
            "(fetch_scene.py unpacks them)",
        )
        return 1
    loaded = {}
    meshes = {}
    for stamp, session, places, routes in handovers(lines, wanted):
        if not places:
            continue
        best = None
        for prefix, uexp in known.items():
            if prefix not in loaded:
                try:
                    loaded[prefix] = load_scene(prefix, root)
                except Exception:
                    loaded[prefix] = {}
            actors = loaded[prefix]
            if sum(1 for n in places if n in actors) < 2:
                continue
            got = trilaterate(places, actors)
            if got and (best is None or got[1] < best[1][1]):
                best = (prefix, got)
        if best is None:
            print(
                f"{stamp} (session {session}): no converted scene lists {list(places)[:4]}"
            )
            continue
        prefix, ((x, y, z), r, n) = best
        actors = loaded[prefix]
        if prefix not in meshes:
            meshes[prefix] = NavMesh(known[prefix])
        nav = meshes[prefix]
        print(
            f"{stamp} (session {session}) {prefix}: start ({x}, {y}, {z}) from {n} distances, residual {r:.0f} cm"
        )
        for label, npts, length, partial in routes:
            # the target: the listed actor with this label; a way's label is shared, so
            # the one whose road fits the logged length best
            cands = [
                (nm, actors[nm])
                for nm, (lb, d, b) in places.items()
                if lb == label and nm in actors
            ]
            if not cands:
                print(f"   route to {label}: no listed actor with that label")
                continue
            results = []
            for nm, a in cands:
                pts, part = nav.find_path((x, y, z), centre(a))
                results.append(
                    (nm, len(pts), road_length(pts) if len(pts) >= 2 else None, part)
                )
            nm, cn, cl, cp = min(results, key=lambda q: abs((q[2] or 1e9) - length))
            flag = (
                ""
                if cl is None
                else f"{cl - length:+.0f} cm ({(cl - length) / length * 100:+.1f}%)"
            )
            print(
                f"   route to {label} [{nm}]: log {npts} points {length:.0f} cm{' partial' if partial else ''}; "
                f"mesh {cn} points {cl if cl is None else round(cl)} cm{' partial' if cp else ''}  {flag}"
            )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
