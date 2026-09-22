"""The cooked Recast navigation mesh of a scene, read from its persistent map's .uexp
(`fetch_scene.py` unpacks it under `<research>/navmaps`), and the two questions the mod asks
the game's navigation system, answered the way Detour answers them: the nearest point on the
mesh to a point (K2_ProjectPointToNavigation) and the road between two points
(FindPathToLocationSynchronously).

    python navmesh.py Act_3/woodlandromance/woodlanddate road X Y Z X2 Y2 Z2   the corners of a road
    python navmesh.py Act_3/woodlandromance/woodlanddate nearest X Y Z          the nearest point on the mesh
    python navmesh.py Act_3/woodlandromance/woodlanddate info                   the tiles and polygons
    (a path to a .uexp does as well as a scene; --root DIR names the research folder)

The tiles' links are rebuilt as dtNavMesh::addTile rebuilds them: inner neighbours by
index; border and layer portals by findConnectingPolys, whose slabs must overlap along the
portal line and meet in height within the walkable climb, each link keeping the overlapping
part of the edge as its portal. The road is dtNavMeshQuery::findPath (A* over the polygons
with node positions at the portal midpoints, nodes keyed by polygon and the tile side crossed
into it, the heuristic scaled by 0.999) followed by findStraightPath (the funnel over the
clipped portals; Detour's restart `i = apexIndex; continue;` sits in a for loop, so the scan
resumes one portal later, which a while loop must spell out or it spins for ever).

Coordinates: Recast keeps (x, y, z) = (-X, Z, -Y) of the game. The links are matched in
Recast's own coordinates; roads and points are handed out in the game's (X, Y, Z).

Layout of a tile, as serialized by the game's engine: the standard 100-byte dtMeshHeader,
four more ints, then vertices (3 floats each) and polygons (firstLink u32, 6 vertex
indices u16, 6 neighbours u16, flags u16, vertCount u8, areaAndtype u8). Nothing after the
polygons is needed. A neighbour of 0 is a wall, n an inner neighbour n-1, and 0x8000|side
a portal edge whose other side lies in the tile beyond (sides 0, 2, 4, 6 = Recast +x, +z,
-x, -z) or in another layer of the same tile position. Tiles are 1000 cm; a position can
hold several layers (ground and a walkable top of something), each a tile of its own.

What this mesh is not: the game runs its navmesh with RuntimeGeneration=DynamicModifiersOnly,
so where a modifier touches a tile the mesh in play is rebuilt from the stored layers and can
differ (the ground at the base of a prop was an island here and reachable in play). Set the
roads against the mod's log with roadvalidate.py before trusting a scene: on 2026-09-22,
60-odd roads in seven scenes came within 0.1 to 4 percent, most with the same corners. The
game's paths carry the pawn's own location as their first point and one more point where the
road crosses between navigation areas."""

import heapq
import math
import os
import struct

NEI_EXT = 0x8000
H_SCALE = 0.999
VEQUAL = (1.0 / 16384.0) ** 2


class Link:
    __slots__ = ("ref", "edge", "side", "bmin", "bmax")

    def __init__(self, ref, edge, side, bmin, bmax):
        self.ref = ref
        self.edge = edge
        self.side = side  # 0xff for an inner neighbour
        self.bmin = bmin
        self.bmax = bmax


class Poly:
    __slots__ = (
        "tile",
        "index",
        "verts",
        "rverts",
        "neis",
        "flags",
        "area",
        "centre",
        "zmin",
        "zmax",
        "links",
    )

    def __init__(self, tile, index, verts, rverts, neis, flags, area):
        self.tile = tile
        self.index = index
        self.verts = verts  # (X, Y, Z) each, the game's
        self.rverts = rverts  # (x, y, z) each, Recast's
        self.neis = neis
        self.flags = flags
        self.area = area
        self.links = []
        n = len(verts)
        self.centre = (
            sum(v[0] for v in verts) / n,
            sum(v[1] for v in verts) / n,
            sum(v[2] for v in verts) / n,
        )
        self.zmin = min(v[2] for v in verts)
        self.zmax = max(v[2] for v in verts)


class Tile:
    __slots__ = (
        "x",
        "y",
        "layer",
        "bmin",
        "bmax",
        "polys",
        "climb",
        "height",
        "radius",
    )


def to_game(rx, ry, rz):
    return (-rx, -rz, ry)


def parse_tiles(data):
    tiles = []
    offs = []
    i = data.find(b"VAND")
    while i >= 0:
        offs.append(i)
        i = data.find(b"VAND", i + 4)
    for o in offs:
        (
            magic,
            version,
            x,
            y,
            layer,
            user,
            polyCount,
            vertCount,
            maxLinkCount,
            dmc,
            dvc,
            dtc,
            bvc,
            omc,
            omb,
        ) = struct.unpack_from("<15i", data, o)
        if version != 7:
            continue
        fl = struct.unpack_from("<10f", data, o + 60)
        p = o + 116
        verts = []
        rverts = []
        for k in range(vertCount):
            rx, ry, rz = struct.unpack_from("<3f", data, p)
            rverts.append((rx, ry, rz))
            verts.append(to_game(rx, ry, rz))
            p += 12
        tile = Tile()
        tile.x, tile.y, tile.layer = x, y, layer
        tile.bmin = to_game(*fl[3:6])
        tile.bmax = to_game(*fl[6:9])
        tile.height, tile.radius, tile.climb = fl[0], fl[1], fl[2]
        tile.polys = []
        ok = True
        for k in range(polyCount):
            vi = struct.unpack_from("<6H", data, p + 4)
            ne = struct.unpack_from("<6H", data, p + 16)
            flags, vc, at = struct.unpack_from("<HBB", data, p + 28)
            p += 32
            if vc > 6 or any(v >= vertCount for v in vi[:vc]):
                ok = False
                break
            tile.polys.append(
                Poly(
                    tile,
                    k,
                    [verts[v] for v in vi[:vc]],
                    [rverts[v] for v in vi[:vc]],
                    list(ne[:vc]),
                    flags,
                    at & 0x3F,
                )
            )
        if ok:
            tiles.append(tile)
    return tiles, offs


# ---- Detour's link building (dtNavMesh::addTile) ------------------------------------------


def _opposite(side):
    return (side + 4) & 0x7


def _neighbour_offset(side):
    return {
        0: (1, 0),
        1: (1, 1),
        2: (0, 1),
        3: (-1, 1),
        4: (-1, 0),
        5: (-1, -1),
        6: (0, -1),
        7: (1, -1),
    }[side]


def _slab_coord(v, side):
    # Recast coordinates: sides 0/4 lie on a line of constant x, sides 2/6 of constant z
    return v[0] if side in (0, 4) else v[2]


def _slab_ends(va, vb, side):
    # (along, height) of the two ends, the lower along-coordinate first
    if side in (0, 4):
        a, b = (va[2], va[1]), (vb[2], vb[1])
    else:
        a, b = (va[0], va[1]), (vb[0], vb[1])
    return (a, b) if a[0] < b[0] else (b, a)


def _overlap_slabs(amin, amax, bmin, bmax, px, py):
    minx = max(amin[0] + px, bmin[0] + px)
    maxx = min(amax[0] - px, bmax[0] - px)
    if minx > maxx:
        return False
    ad = (amax[1] - amin[1]) / (amax[0] - amin[0]) if amax[0] != amin[0] else 0.0
    ak = amin[1] - ad * amin[0]
    bd = (bmax[1] - bmin[1]) / (bmax[0] - bmin[0]) if bmax[0] != bmin[0] else 0.0
    bk = bmin[1] - bd * bmin[0]
    aminy = ad * minx + ak
    amaxy = ad * maxx + ak
    bminy = bd * minx + bk
    bmaxy = bd * maxx + bk
    dmin = bminy - aminy
    dmax = bmaxy - amaxy
    if dmin * dmax < 0:
        return True
    thr = (py * 2) * (py * 2)
    return dmin * dmin <= thr or dmax * dmax <= thr


def _find_connecting_polys(va, vb, target, side, maxcon=4):
    """findConnectingPolys: the target tile's polygons whose portal edges on the given side
    lie on the line of va-vb and overlap it, each with the overlapping span along the
    line."""
    amin, amax = _slab_ends(va, vb, side)
    apos = _slab_coord(va, side)
    m = NEI_EXT | side
    out = []
    for poly in target.polys:
        nv = len(poly.rverts)
        for j in range(nv):
            if poly.neis[j] != m:
                continue
            vc = poly.rverts[j]
            vd = poly.rverts[(j + 1) % nv]
            if abs(apos - _slab_coord(vc, side)) > 0.01:
                continue
            bmin, bmax = _slab_ends(vc, vd, side)
            if not _overlap_slabs(amin, amax, bmin, bmax, 0.01, target.climb):
                continue
            if len(out) < maxcon:
                out.append((poly, max(amin[0], bmin[0]), min(amax[0], bmax[0])))
            break
    return out


def _connect_ext(tile, target, side):
    """connectExtLinks: links from the tile's portal edges facing the given side (-1: any
    side, for another layer of the same tile position) into the target tile."""
    for poly in tile.polys:
        nv = len(poly.rverts)
        for j in range(nv):
            if not (poly.neis[j] & NEI_EXT):
                continue
            direction = poly.neis[j] & 0xFF
            if side != -1 and direction != side:
                continue
            va = poly.rverts[j]
            vb = poly.rverts[(j + 1) % nv]
            for other, lo, hi in _find_connecting_polys(
                va, vb, target, _opposite(direction)
            ):
                c = 2 if direction in (0, 4) else 0
                if vb[c] == va[c]:
                    tmin, tmax = 0.0, 1.0
                else:
                    tmin = (lo - va[c]) / (vb[c] - va[c])
                    tmax = (hi - va[c]) / (vb[c] - va[c])
                if tmin > tmax:
                    tmin, tmax = tmax, tmin
                bmin = int(min(max(tmin, 0.0), 1.0) * 255.0)
                bmax = int(min(max(tmax, 0.0), 1.0) * 255.0)
                poly.links.append(Link(other, j, direction, bmin, bmax))


class NavMesh:
    def __init__(self, path):
        data = open(path, "rb").read()
        self.tiles, self.offsets = parse_tiles(data)
        self.polys = [p for t in self.tiles for p in t.polys if len(p.verts) >= 3]
        self.by_pos = {}
        for t in self.tiles:
            self.by_pos.setdefault((t.x, t.y), []).append(t)
        self._connect()

    def _connect(self):
        for tile in self.tiles:
            # inner neighbours, by index (connectIntLinks)
            for poly in tile.polys:
                nv = len(poly.verts)
                for j in range(nv):
                    nei = poly.neis[j]
                    if nei == 0 or nei & NEI_EXT:
                        continue
                    other = tile.polys[nei - 1]
                    if len(other.verts) >= 3:
                        poly.links.append(Link(other, j, 0xFF, 0, 255))
            # the other layers at the same position, any side
            for other in self.by_pos[(tile.x, tile.y)]:
                if other is not tile:
                    _connect_ext(tile, other, -1)
            # the neighbouring tile positions, side by side
            for side in range(8):
                dx, dy = _neighbour_offset(side)
                for other in self.by_pos.get((tile.x + dx, tile.y + dy), ()):
                    _connect_ext(tile, other, side)

    # ---- the nearest point on the mesh -------------------------------------------------

    def nearest_poly(self, point, extent):
        """The polygon nearest across the ground within the extent, its height within the
        extent's, and the point on it: the game's ProjectPointToNavigation."""
        best = None
        for poly in self.polys:
            if poly.flags == 0:
                continue
            if poly.zmin - extent[2] > point[2] or poly.zmax + extent[2] < point[2]:
                continue
            q = _closest2d(poly.verts, point)
            dx, dy = abs(q[0] - point[0]), abs(q[1] - point[1])
            if dx > extent[0] or dy > extent[1]:
                continue
            z = _height_on(poly, q)
            if abs(z - point[2]) > extent[2]:
                continue
            d = math.hypot(dx, dy)
            key = (round(d), abs(z - point[2]))
            if best is None or key < best[0]:
                best = (key, poly, (q[0], q[1], z))
        if best is None:
            return None, None
        return best[1], best[2]

    def project(self, point, extents=((250.0, 250.0, 120.0), (250.0, 250.0, 400.0))):
        for ext in extents:
            poly, q = self.nearest_poly(point, ext)
            if poly is not None:
                return poly, q
        return None, None

    # ---- the road --------------------------------------------------------------------

    @staticmethod
    def portal(poly, other):
        """getPortalPoints: the left and right ends of the portal from poly into other."""
        for link in poly.links:
            if link.ref is other:
                nv = len(poly.verts)
                v0 = poly.verts[link.edge]
                v1 = poly.verts[(link.edge + 1) % nv]
                if link.side != 0xFF and (link.bmin != 0 or link.bmax != 255):
                    s = 1.0 / 255.0
                    return _lerp(v0, v1, link.bmin * s), _lerp(v0, v1, link.bmax * s)
                return v0, v1
        return None

    def find_path(self, start, end, end_extents=None):
        """The corners of the road from start to end, as the game hands them to the mod, and
        whether the road stops short of the end."""
        sp, s = self.project(start)
        ep, e = self.project(end, end_extents) if end_extents else self.project(end)
        if sp is None or ep is None:
            return [], True
        corridor, partial = self._astar(sp, s, ep, e)
        return _straight_path(s, e, corridor), partial

    def _astar(self, sp, s, ep, e):
        """dtNavMeshQuery::findPath."""
        sid = (id(sp), 0)
        pos = {sid: s}
        cost = {sid: 0.0}
        total = {sid: _dist(s, e) * H_SCALE}
        parent = {}
        node = {sid: sp}
        state = {sid: "open"}
        openq = [(total[sid], 0, sid)]
        counter = 1
        last_best, last_best_cost = sid, total[sid]
        while openq:
            f, _, bid = heapq.heappop(openq)
            if state.get(bid) != "open" or f > total[bid] + 1e-9:
                continue
            state[bid] = "closed"
            best = node[bid]
            if best is ep:
                last_best = bid
                break
            pid = parent.get(bid)
            for link in best.links:
                nb = link.ref
                cross = (link.side >> 1) if link.side != 0xFF else 0
                nid = (id(nb), cross)
                if (pid is not None and node[pid] is nb) or nb.flags == 0:
                    continue
                if nid not in pos:
                    left, right = self.portal(best, nb)
                    pos[nid] = _mid(left, right)
                    node[nid] = nb
                if nb is ep:
                    c = cost[bid] + _dist(pos[bid], pos[nid]) + _dist(pos[nid], e)
                    h = 0.0
                else:
                    c = cost[bid] + _dist(pos[bid], pos[nid])
                    h = _dist(pos[nid], e) * H_SCALE
                t = c + h
                st = state.get(nid)
                if st in ("open", "closed") and t >= total[nid]:
                    continue
                parent[nid] = bid
                cost[nid] = c
                total[nid] = t
                state[nid] = "open"
                counter += 1
                heapq.heappush(openq, (t, counter, nid))
                if h < last_best_cost:
                    last_best_cost, last_best = h, nid
        partial = node[last_best] is not ep
        chain = []
        cur = last_best
        while cur is not None:
            chain.append(node[cur])
            cur = parent.get(cur)
        chain.reverse()
        return chain, partial


def _lerp(a, b, t):
    return (
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
    )


def _mid(a, b):
    return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2)


def _dist(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def _inside2d(verts, p):
    inside = False
    n = len(verts)
    j = n - 1
    for i in range(n):
        xi, yi = verts[i][0], verts[i][1]
        xj, yj = verts[j][0], verts[j][1]
        if (yi > p[1]) != (yj > p[1]) and p[0] < (xj - xi) * (p[1] - yi) / (
            yj - yi + 1e-12
        ) + xi:
            inside = not inside
        j = i
    return inside


def _closest2d(verts, p):
    if _inside2d(verts, p):
        return (p[0], p[1])
    best = None
    n = len(verts)
    for i in range(n):
        a = verts[i]
        b = verts[(i + 1) % n]
        abx, aby = b[0] - a[0], b[1] - a[1]
        l2 = abx * abx + aby * aby
        t = (
            0.0
            if l2 < 1e-9
            else max(0.0, min(1.0, ((p[0] - a[0]) * abx + (p[1] - a[1]) * aby) / l2))
        )
        q = (a[0] + abx * t, a[1] + aby * t)
        d = math.hypot(q[0] - p[0], q[1] - p[1])
        if best is None or d < best[0]:
            best = (d, q)
    return best[1]


def _height_on(poly, q):
    a, b, c = poly.verts[0], poly.verts[1], poly.verts[2]
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    if abs(nz) < 1e-6:
        return poly.centre[2]
    return a[2] - (nx * (q[0] - a[0]) + ny * (q[1] - a[1])) / nz


def _closest_on_boundary(poly, p):
    """closestPointOnPolyBoundary: the point itself inside the polygon, else the nearest
    point of its edges, at the edge's height."""
    if _inside2d(poly.verts, p):
        return p
    best = None
    n = len(poly.verts)
    for i in range(n):
        a = poly.verts[i]
        b = poly.verts[(i + 1) % n]
        abx, aby = b[0] - a[0], b[1] - a[1]
        l2 = abx * abx + aby * aby
        t = (
            0.0
            if l2 < 1e-9
            else max(0.0, min(1.0, ((p[0] - a[0]) * abx + (p[1] - a[1]) * aby) / l2))
        )
        q = _lerp(a, b, t)
        d = math.hypot(q[0] - p[0], q[1] - p[1])
        if best is None or d < best[0]:
            best = (d, q)
    return best[1]


def _triarea(a, b, c):
    # dtTriArea2D over the ground plane; the game's (X, Y) keep Recast's (x, z) orientation
    abx, aby = b[0] - a[0], b[1] - a[1]
    acx, acy = c[0] - a[0], c[1] - a[1]
    return acx * aby - abx * acy


def _vequal(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2 < VEQUAL


def _dist_pt_seg_2d(p, a, b):
    abx, aby = b[0] - a[0], b[1] - a[1]
    d = abx * abx + aby * aby
    t = (p[0] - a[0]) * abx + (p[1] - a[1]) * aby
    if d > 0:
        t /= d
    t = max(0.0, min(1.0, t))
    dx = a[0] + t * abx - p[0]
    dy = a[1] + t * aby - p[1]
    return dx * dx + dy * dy


def _straight_path(start, end, corridor):
    """dtNavMeshQuery::findStraightPath over a polygon corridor."""
    if not corridor:
        return [start, end]
    closest_start = _closest_on_boundary(corridor[0], start)
    closest_end = _closest_on_boundary(corridor[-1], end)
    points = [closest_start]

    def append(p):
        if _vequal(points[-1], p):
            points[-1] = p
        else:
            points.append(p)

    n = len(corridor)
    if n == 1:
        append(closest_end)
        return points
    apex = closest_start
    pleft = apex
    pright = apex
    apex_i = left_i = right_i = 0
    i = 0
    while i < n:
        if i + 1 < n:
            portal = NavMesh.portal(corridor[i], corridor[i + 1])
            if portal is None:
                # no portal into the next polygon: end the road here
                append(_closest_on_boundary(corridor[i], end))
                return points
            left, right = portal
            if i == 0 and _dist_pt_seg_2d(apex, left, right) < 0.001 * 0.001:
                i += 1
                continue
        else:
            left = right = closest_end
        # right vertex
        if _triarea(apex, pright, right) <= 0.0:
            if _vequal(apex, pright) or _triarea(apex, pleft, right) > 0.0:
                pright = right
                right_i = i
            else:
                apex = pleft
                apex_i = left_i
                append(apex)
                pleft = apex
                pright = apex
                left_i = apex_i
                right_i = apex_i
                # Detour restarts with i = apexIndex inside a for loop, whose ++i then runs
                i = apex_i + 1
                continue
        # left vertex
        if _triarea(apex, pleft, left) >= 0.0:
            if _vequal(apex, pleft) or _triarea(apex, pright, left) < 0.0:
                pleft = left
                left_i = i
            else:
                apex = pright
                apex_i = right_i
                append(apex)
                pleft = apex
                pright = apex
                left_i = apex_i
                right_i = apex_i
                # Detour restarts with i = apexIndex inside a for loop, whose ++i then runs
                i = apex_i + 1
                continue
        i += 1
    append(closest_end)
    return points


# ---- command line ------------------------------------------------------------------------


def scene_mesh(scene, root=None):
    """The persistent map's .uexp of a scene under the research folder."""
    if scene.lower().endswith(".uexp"):
        return scene
    from scene_geometry import research_root

    parts = scene.strip("/").replace("\\", "/").replace("__", "/").split("/")
    if len(parts) != 3:
        raise SystemExit("name a scene as Act_3/woodlandromance/woodlanddate, or give a .uexp")
    act, sc, mp = parts
    return os.path.join(root or research_root(), "navmaps", "SMG026", "Content", "Maps", "Acts", act, sc, mp, f"{mp}.uexp")


def main(argv):
    root = None
    if "--root" in argv:
        root = argv[argv.index("--root") + 1]
        argv = [a for a in argv if a not in ("--root", root)]
    if len(argv) < 2:
        print(__doc__)
        return 1
    path = scene_mesh(argv[0], root)
    if not os.path.exists(path):
        print("no persistent map at", path, "(fetch_scene.py unpacks it)")
        return 1
    nav = NavMesh(path)
    what = argv[1]
    if what == "info":
        layers = {}
        for t in nav.tiles:
            layers[(t.x, t.y)] = layers.get((t.x, t.y), 0) + 1
        links = sum(len(p.links) for p in nav.polys)
        print(f"{len(nav.tiles)} tiles at {len(layers)} positions ({sum(1 for v in layers.values() if v > 1)} with layers), {len(nav.polys)} polygons, {links} links")
        xs = [v[0] for t in nav.tiles for p in t.polys for v in p.verts]
        ys = [v[1] for t in nav.tiles for p in t.polys for v in p.verts]
        zs = [v[2] for t in nav.tiles for p in t.polys for v in p.verts]
        print(f"X {min(xs):.0f}..{max(xs):.0f}, Y {min(ys):.0f}..{max(ys):.0f}, Z {min(zs):.0f}..{max(zs):.0f}")
        return 0
    nums = [float(a) for a in argv[2:]]
    if what == "nearest" and len(nums) >= 3:
        poly, q = nav.project(tuple(nums[:3]))
        if poly is None:
            print("nothing on the mesh within the search box")
            return 1
        print(f"({q[0]:.1f}, {q[1]:.1f}, {q[2]:.1f}) on tile ({poly.tile.x}, {poly.tile.y}) layer {poly.tile.layer} polygon {poly.index}")
        return 0
    if what == "road" and len(nums) >= 6:
        pts, partial = nav.find_path(tuple(nums[:3]), tuple(nums[3:6]))
        length = sum(_dist(pts[i - 1], pts[i]) for i in range(1, len(pts)))
        print(f"{len(pts)} points, {length:.0f} cm{', partial (the end is out of reach)' if partial else ''}")
        for p in pts:
            print(f"   ({p[0]:.0f}, {p[1]:.0f}, {p[2]:.0f})")
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    import sys

    sys.exit(main(sys.argv[1:]))
