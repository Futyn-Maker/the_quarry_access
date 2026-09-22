"""Where things stand in a scene, read from its converted sub-levels: every actor with a
root component (its position), every brush volume (its collision hull or element box,
scaled, turned and placed: the two-dimensional bounds, the height range, and the turned
frame the box really occupies), every use location (its collision box), every locator.

The sub-levels come from fetch_scene.py as `<research>/mapjson/<Act>__<scene>__<map>__<folder>__<Level>.json`;
a scene is named by that prefix, `Act_3__woodlandromance__woodlanddate`, or with slashes.

    python scene_geometry.py Act_3/woodlandromance/woodlanddate [substring] [--root DIR]

prints the actors, filtered by a substring of their name. As a module, `load_scene(prefix)`
returns them by registered name (`ActorRegister.ActorName`, else the export name):
`kind` ("volume", "use", "locator", "actor"), `cls`, `low`/`high` (X, Y bounds), `z`,
`zlow`/`zhigh`, and for a volume `frame` = {cx, cy, yaw, hx, hy} (the element box under the
component's scale and yaw) with `corners`, and `hull` (the collision hull's world points,
empty where the cooked asset keeps none). `inside(actor, x, y)` tests the turned frame.

Lessons: the geometry is three-dimensional and turned. A volume's axis-aligned bounds are
what `GetActorBounds` gives the mod, and for a turned box they reach where the box does
not (up to half again as large); heights matter wherever a building has storeys; the
element box of a brush can be stale (the Sun's card volume: 202 by 67 in the asset, 239
by 220 in play), so the hull is used when the asset keeps one, and what the game logs
(sizes, distances) is set against the asset before anything is trusted."""

import base64
import glob
import json
import math
import os
import sys


def research_root():
    """The git-ignored research folder: QA_RESEARCH_DIR, else <repo>/research."""
    env = os.environ.get("QA_RESEARCH_DIR")
    if env:
        return env
    return os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "research",
    )


def scene_prefix(scene):
    """`Act_3/woodlandromance/woodlanddate` or the prefix itself -> `Act_3__woodlandromance__woodlanddate`."""
    return scene.strip("/").replace("\\", "/").replace("/", "__")


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def decode_ref(b64):
    """An FActorReference, serialized raw: int32, FString, 2 bytes; base64 in the JSON."""
    try:
        raw = base64.b64decode(b64)
        n = int.from_bytes(raw[4:8], "little", signed=True)
        if n > 0:
            return raw[8 : 8 + n - 1].decode("latin-1")
        if n < 0:
            return raw[8 : 8 + (-n - 1) * 2].decode("utf-16-le")
    except Exception:
        pass
    return b64


def prop(data, name):
    for p in data or []:
        if isinstance(p, dict) and p.get("Name") == name:
            return p.get("Value")
    return None


def num(x):
    try:
        return float(x)
    except Exception:
        return None


def unwrap(v):
    # UAssetGUI wraps a struct value as a one-element list of property data.
    while (
        isinstance(v, list)
        and len(v) == 1
        and isinstance(v[0], dict)
        and "Value" in v[0]
    ):
        v = v[0].get("Value")
    return v


def vec(v):
    v = unwrap(v)
    if isinstance(v, dict) and "X" in v:
        return (num(v.get("X")), num(v.get("Y")), num(v.get("Z")))
    if isinstance(v, list):
        d = {q.get("Name"): q.get("Value") for q in v if isinstance(q, dict)}
        if "X" in d:
            return (num(d["X"]), num(d["Y"]), num(d["Z"]))
    return None


def find_named(node, name, depth=0):
    if depth > 12:
        return None
    if isinstance(node, dict):
        if node.get("Name") == name:
            return node.get("Value")
        for v in node.values():
            r = find_named(v, name, depth + 1)
            if r is not None:
                return r
    elif isinstance(node, list):
        for v in node:
            r = find_named(v, name, depth + 1)
            if r is not None:
                return r
    return None


def box_of(eb):
    eb = unwrap(eb)
    if isinstance(eb, dict) and "Min" in eb and "Max" in eb:
        return vec(eb["Min"]), vec(eb["Max"])
    return vec(find_named(eb, "Min")), vec(find_named(eb, "Max"))


def rotate_rotator(p, rot):
    """A point turned by an engine rotator (pitch, yaw, roll in degrees): the engine's own
    rotation matrix, so that positive yaw takes local X towards world Y."""
    if not isinstance(rot, dict):
        return p
    pitch, yaw, roll = (num(rot.get(k)) or 0.0 for k in ("Pitch", "Yaw", "Roll"))
    sp, cp = math.sin(math.radians(pitch)), math.cos(math.radians(pitch))
    sy, cy = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    sr, cr = math.sin(math.radians(roll)), math.cos(math.radians(roll))
    ax = (cp * cy, cp * sy, sp)
    ay = (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp)
    az = (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)
    return tuple(p[0] * ax[k] + p[1] * ay[k] + p[2] * az[k] for k in range(3))


def rotate_quat(p, q):
    """A point turned by an engine quaternion (X, Y, Z, W)."""
    if not isinstance(q, dict) or "W" not in q:
        return p
    x, y, z, w = (num(q.get(k)) or 0.0 for k in ("X", "Y", "Z", "W"))

    def cross(a, b):
        return (
            a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0],
        )

    t = cross((x, y, z), p)
    t = (2 * t[0], 2 * t[1], 2 * t[2])
    u = cross((x, y, z), t)
    return (p[0] + w * t[0] + u[0], p[1] + w * t[1] + u[1], p[2] + w * t[2] + u[2])


def hull_points(exps, setup):
    """The vertices of every convex element of a brush's body setup, in the brush's own
    space (each element's own transform applied). Cooked assets often keep none."""
    pts = []
    data = exps[setup - 1].get("Data") or []
    agg = unwrap(prop(data, "AggGeom"))
    if not isinstance(agg, list):
        return pts
    for q in agg:
        if not isinstance(q, dict) or q.get("Name") != "ConvexElems":
            continue
        for c in q.get("Value") or []:
            cd = c.get("Value") if isinstance(c, dict) else None
            if not isinstance(cd, list):
                continue
            verts = None
            tr = None
            for r in cd:
                if isinstance(r, dict) and r.get("Name") == "VertexData":
                    verts = r.get("Value")
                if isinstance(r, dict) and r.get("Name") == "Transform":
                    tr = unwrap(r.get("Value"))
            scale = (1.0, 1.0, 1.0)
            trans = (0.0, 0.0, 0.0)
            rq = None
            if isinstance(tr, list):
                td = {x.get("Name"): x.get("Value") for x in tr if isinstance(x, dict)}
                if "Scale3D" in td:
                    scale = vec(td["Scale3D"]) or scale
                if "Translation" in td:
                    trans = vec(td["Translation"]) or trans
                rq = unwrap(td.get("Rotation"))
            elif isinstance(tr, dict):
                scale = vec(tr.get("Scale3D")) or scale
                trans = vec(tr.get("Translation")) or trans
                rq = unwrap(tr.get("Rotation"))
            for v in verts or []:
                vv = v.get("Value") if isinstance(v, dict) else v
                if isinstance(vv, dict) and "X" in vv:
                    p = (
                        num(vv["X"]) * scale[0],
                        num(vv["Y"]) * scale[1],
                        num(vv["Z"]) * scale[2],
                    )
                    p = rotate_quat(p, rq)
                    pts.append((p[0] + trans[0], p[1] + trans[1], p[2] + trans[2]))
    return pts


def _corners(frame):
    c, s = math.cos(math.radians(frame["yaw"])), math.sin(math.radians(frame["yaw"]))
    out = []
    for sx, sy in ((1, 1), (1, -1), (-1, -1), (-1, 1)):
        lx, ly = sx * frame["hx"], sy * frame["hy"]
        out.append((frame["cx"] + lx * c - ly * s, frame["cy"] + lx * s + ly * c))
    return out


def local_of(frame, x, y):
    """A world point in the frame's own axes."""
    c, s = math.cos(math.radians(-frame["yaw"])), math.sin(math.radians(-frame["yaw"]))
    dx, dy = x - frame["cx"], y - frame["cy"]
    return (dx * c - dy * s, dx * s + dy * c)


def inside(actor, x, y, margin=0.0):
    """Whether a point lies inside a volume's turned frame, at least `margin` from its
    sides; the bounds when the volume has no frame."""
    fr = actor.get("frame")
    if fr:
        lx, ly = local_of(fr, x, y)
        return abs(lx) <= fr["hx"] - margin and abs(ly) <= fr["hy"] - margin
    lo, hi = actor["low"], actor["high"]
    return (
        lo[0] + margin <= x <= hi[0] - margin and lo[1] + margin <= y <= hi[1] - margin
    )


def load_scene(prefix, root=None):
    root = root or research_root()
    prefix = scene_prefix(prefix)
    actors = {}
    for path in sorted(glob.glob(os.path.join(root, "mapjson", f"{prefix}__*.json"))):
        d = load_json(path)
        imps = d.get("Imports", [])
        exps = d.get("Exports", [])

        def cls(e):
            ci = e.get("ClassIndex")
            return (
                imps[-ci - 1]["ObjectName"]
                if isinstance(ci, int) and ci < 0
                else str(ci)
            )

        def root_location(pr):
            r = prop(pr, "RootComponent")
            if isinstance(r, int) and r > 0:
                return vec(prop(exps[r - 1].get("Data") or [], "RelativeLocation"))
            return None

        for i, e in enumerate(exps, 1):
            c = cls(e)
            pr = e.get("Data") or []
            name = e.get("ObjectName", "")
            reg = None
            r = prop(pr, "ActorRegister")
            if isinstance(r, list):
                for q in r:
                    if isinstance(q, dict) and q.get("Name") == "ActorName":
                        reg = q.get("Value")
            key = reg or name
            comp = prop(pr, "BrushComponent")
            if isinstance(comp, int) and comp > 0:
                cp = exps[comp - 1].get("Data") or []
                loc = vec(prop(cp, "RelativeLocation")) or (0, 0, 0)
                rot = unwrap(prop(cp, "RelativeRotation"))
                scale = vec(prop(cp, "RelativeScale3D")) or (1, 1, 1)
                setup = prop(cp, "BrushBodySetup")
                pts = (
                    hull_points(exps, setup)
                    if isinstance(setup, int) and setup > 0
                    else []
                )
                hull = []
                for p in pts:
                    w = rotate_rotator(
                        (p[0] * scale[0], p[1] * scale[1], p[2] * scale[2]), rot
                    )
                    hull.append((loc[0] + w[0], loc[1] + w[1], loc[2] + w[2]))
                eb = (
                    find_named(exps[setup - 1].get("Data"), "ElemBox")
                    if isinstance(setup, int) and setup > 0
                    else None
                )
                bmn, bmx = box_of(eb) if eb is not None else (None, None)
                frame = None
                if bmn and bmx:
                    cl = (
                        (bmn[0] + bmx[0]) / 2 * scale[0],
                        (bmn[1] + bmx[1]) / 2 * scale[1],
                        (bmn[2] + bmx[2]) / 2 * scale[2],
                    )
                    cw = rotate_rotator(cl, rot)
                    frame = dict(
                        cx=loc[0] + cw[0],
                        cy=loc[1] + cw[1],
                        yaw=(num(rot.get("Yaw")) or 0.0)
                        if isinstance(rot, dict)
                        else 0.0,
                        hx=abs(bmx[0] - bmn[0]) / 2 * scale[0],
                        hy=abs(bmx[1] - bmn[1]) / 2 * scale[1],
                    )
                pts3 = (
                    hull
                    if len(hull) >= 4
                    and max(p[2] for p in hull) - min(p[2] for p in hull) >= 1.0
                    else []
                )
                if not pts3 and bmn and bmx:
                    pts3 = []
                    for x in (bmn[0], bmx[0]):
                        for y in (bmn[1], bmx[1]):
                            for z in (bmn[2], bmx[2]):
                                w = rotate_rotator(
                                    (x * scale[0], y * scale[1], z * scale[2]), rot
                                )
                                pts3.append(
                                    (loc[0] + w[0], loc[1] + w[1], loc[2] + w[2])
                                )
                if not pts3:
                    continue
                xs, ys, zs = (
                    [p[0] for p in pts3],
                    [p[1] for p in pts3],
                    [p[2] for p in pts3],
                )
                actors[key] = dict(
                    kind="volume",
                    cls=c,
                    name=name,
                    low=(min(xs), min(ys)),
                    high=(max(xs), max(ys)),
                    z=(min(zs) + max(zs)) / 2,
                    zlow=min(zs),
                    zhigh=max(zs),
                    frame=frame,
                    corners=_corners(frame) if frame else None,
                    hull=hull,
                    loc=loc,
                )
            elif "UseLocation" in c or name.lower().startswith(("ul_", "ui_")):
                loc = root_location(pr)
                if not loc:
                    continue
                half = (85.0, 85.0, 75.0)  # UseLocationDefaultExtent of DefaultGame.ini
                off = (0.0, 0.0, 0.0)
                for j, x in enumerate(exps, 1):
                    if x.get("OuterIndex") == i and cls(x) == "BoxComponent":
                        xp = x.get("Data") or []
                        ext = vec(prop(xp, "BoxExtent"))
                        rl = vec(prop(xp, "RelativeLocation"))
                        if ext:
                            half = ext
                        if rl:
                            off = rl
                actors[key] = dict(
                    kind="use",
                    cls=c,
                    name=name,
                    low=(loc[0] + off[0] - half[0], loc[1] + off[1] - half[1]),
                    high=(loc[0] + off[0] + half[0], loc[1] + off[1] + half[1]),
                    z=loc[2],
                    zlow=loc[2] + off[2] - half[2],
                    zhigh=loc[2] + off[2] + half[2],
                    loc=loc,
                )
            elif "Locator" in c or name.lower().startswith("l_"):
                loc = root_location(pr)
                if loc:
                    actors[key] = dict(
                        kind="locator",
                        cls=c,
                        name=name,
                        low=(loc[0], loc[1]),
                        high=(loc[0], loc[1]),
                        z=loc[2],
                        zlow=loc[2] - 75.0,
                        zhigh=loc[2] + 75.0,
                        loc=loc,
                    )
            else:
                loc = root_location(pr)
                if loc and key not in actors:
                    actors[key] = dict(
                        kind="actor",
                        cls=c,
                        name=name,
                        low=(loc[0], loc[1]),
                        high=(loc[0], loc[1]),
                        z=loc[2],
                        zlow=loc[2],
                        zhigh=loc[2],
                        loc=loc,
                    )
    return actors


def describe(a):
    cx, cy = (a["low"][0] + a["high"][0]) / 2, (a["low"][1] + a["high"][1]) / 2
    w, h = a["high"][0] - a["low"][0], a["high"][1] - a["low"][1]
    s = f"{a['kind']:8} {a['cls']:28} at ({cx:.0f}, {cy:.0f}, {a['z']:.0f})"
    if a["kind"] in ("volume", "use"):
        s += f" bounds {w:.0f} by {h:.0f}, heights {a['zlow']:.0f} to {a['zhigh']:.0f}"
    fr = a.get("frame")
    if fr and abs(fr["yaw"]) > 0.5:
        s += f", box {2 * fr['hx']:.0f} by {2 * fr['hy']:.0f} turned {fr['yaw']:.1f} degrees"
    if a.get("hull"):
        s += f", hull of {len(a['hull'])} points"
    return s


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    root = None
    if "--root" in argv:
        root = argv[argv.index("--root") + 1]
        args = [a for a in args if a != root]
    if not args:
        print(__doc__)
        return 1
    actors = load_scene(args[0], root)
    if not actors:
        print(
            "no converted sub-levels for",
            scene_prefix(args[0]),
            "under",
            os.path.join(root or research_root(), "mapjson"),
        )
        return 1
    wanted = args[1].lower() if len(args) > 1 else ""
    for key in sorted(actors, key=str.lower):
        if (
            wanted
            and wanted not in key.lower()
            and wanted not in actors[key]["name"].lower()
        ):
            continue
        print(f"{key:36} {describe(actors[key])}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
