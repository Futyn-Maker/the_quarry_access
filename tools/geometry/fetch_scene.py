"""Unpack a scene's persistent map (which holds its cooked navigation mesh) and its
gameplay sub-levels from the game's paks, and convert them to JSON, into the research
folder: `navmaps/` and `navjson/` for the persistent map, `maps/` and `mapjson/` for the
sub-levels, named `<Act>__<scene>__<map>__<folder>__<Level>.json` as scene_geometry.py
reads them.

    python fetch_scene.py Act_3/woodlandromance/woodlanddate [more scenes] [--root DIR] [--force]
    python fetch_scene.py --list woodland        (search the pak listing)

Needs repak (QA_REPAK, or `repak` on PATH) and UAssetGUI (QA_UASSETGUI, or `UAssetGUI`
on PATH); the game folder from QA_GAME_DIR, else Steam's default. The pak listing is cached
as `<research>/paklist.txt` and built once with `repak list` over every pak."""

import glob
import os
import shutil
import subprocess
import sys

from scene_geometry import research_root

AES_KEY = "0xB916405788667A528F252B78348541929BAFD27FA88933543CE6016BB2388408"
DEFAULT_GAME = r"C:\Program Files (x86)\Steam\steamapps\common\The Quarry"


def game_dir():
    return os.environ.get("QA_GAME_DIR") or DEFAULT_GAME


def tool(env, name):
    path = os.environ.get(env) or shutil.which(name) or shutil.which(name + ".exe")
    if not path:
        raise SystemExit(f"{name} not found: set {env} or put it on PATH")
    return path


def paks():
    out = sorted(
        glob.glob(
            os.path.join(
                game_dir(), "SMG026", "Content", "Paks", "pakchunk*-WindowsNoEditor.pak"
            )
        )
    )
    if not out:
        raise SystemExit(f"no paks under {game_dir()} (set QA_GAME_DIR)")
    return out


def listing(root):
    """(pak basename, entry path) for every file of every pak, cached in paklist.txt."""
    path = os.path.join(root, "paklist.txt")
    if not os.path.exists(path):
        repak = tool("QA_REPAK", "repak")
        lines = []
        for pak in paks():
            print("listing", os.path.basename(pak))
            res = subprocess.run(
                [repak, "-a", AES_KEY, "list", pak],
                capture_output=True,
                text=True,
                errors="replace",
            )
            for line in res.stdout.splitlines():
                line = line.strip()
                if line:
                    lines.append(f"{os.path.basename(pak)} {line}")
        os.makedirs(root, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split(" ", 1)
        if len(parts) == 2:
            out.append((parts[0], parts[1]))
    return out


def scene_entries(entries, scene):
    """The persistent map's two files and the sub-levels' files of a scene, as
    (kind, pak, entry) with kind "persistent" or "sub"."""
    act, sc, mp = scene.strip("/").replace("\\", "/").split("/")
    base = f"SMG026/Content/Maps/Acts/{act}/{sc}/{mp}/"
    out = []
    for pak, entry in entries:
        if not entry.lower().startswith(base.lower()):
            continue
        rest = entry[len(base) :]
        low = rest.lower()
        if not (low.endswith(".umap") or low.endswith(".uexp")) or "_builtdata" in low:
            continue
        if "/" not in rest and rest.lower() in (
            f"{mp.lower()}.umap",
            f"{mp.lower()}.uexp",
        ):
            out.append(("persistent", pak, entry))
        elif rest.count("/") == 1 and rest.split("/")[0].lower() in (
            "gameplay",
            "levels",
        ):
            out.append(("sub", pak, entry))
    return out


def unpack(pak_name, entries, out_dir, force):
    repak = tool("QA_REPAK", "repak")
    pak = os.path.join(game_dir(), "SMG026", "Content", "Paks", pak_name)
    todo = [e for e in entries if force or not os.path.exists(os.path.join(out_dir, e))]
    if not todo:
        return
    cmd = [repak, "-a", AES_KEY, "unpack", pak, "-o", out_dir]
    if force:
        cmd.append("-f")
    for e in todo:
        cmd += ["-i", e]
    res = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if res.returncode != 0:
        raise SystemExit(
            f"repak failed on {pak_name}: {res.stderr.strip() or res.stdout.strip()}"
        )


def convert(umap, out_json, force):
    if os.path.exists(out_json) and not force:
        return
    gui = tool("QA_UASSETGUI", "UAssetGUI")
    os.makedirs(os.path.dirname(out_json), exist_ok=True)
    res = subprocess.run(
        [gui, "tojson", umap, out_json, "VER_UE4_26"],
        capture_output=True,
        text=True,
        errors="replace",
    )
    if not os.path.exists(out_json):
        raise SystemExit(
            f"UAssetGUI failed on {umap}: {res.stderr.strip() or res.stdout.strip()}"
        )


def fetch(scene, root, force=False):
    entries = scene_entries(listing(root), scene)
    if not entries:
        print("nothing in the paks for", scene, "(try --list)")
        return False
    act, sc, mp = scene.strip("/").replace("\\", "/").split("/")
    prefix = f"{act}__{sc}__{mp}"
    by_pak = {}
    for kind, pak, entry in entries:
        by_pak.setdefault((kind, pak), []).append(entry)
    for (kind, pak), files in by_pak.items():
        unpack(
            pak,
            files,
            os.path.join(root, "navmaps" if kind == "persistent" else "maps"),
            force,
        )
    for kind, pak, entry in entries:
        if not entry.lower().endswith(".umap"):
            continue
        if kind == "persistent":
            umap = os.path.join(root, "navmaps", entry)
            convert(umap, os.path.join(root, "navjson", f"{mp}.json"), force)
            with open(umap[:-5] + ".uexp", "rb") as f:
                tiles = f.read().count(b"VAND")
            print(f"{prefix}: persistent map with {tiles} navmesh tiles")
        else:
            folder, level = entry.split("/")[-2], os.path.basename(entry)[:-5]
            convert(
                os.path.join(root, "maps", entry),
                os.path.join(root, "mapjson", f"{prefix}__{folder}__{level}.json"),
                force,
            )
            print(f"{prefix}: sub-level {folder}/{level}")
    return True


def main(argv):
    root = research_root()
    force = "--force" in argv
    if "--root" in argv:
        root = argv[argv.index("--root") + 1]
    if "--list" in argv:
        needle = argv[argv.index("--list") + 1].lower()
        found = [
            f"{pak} {entry}"
            for pak, entry in listing(root)
            if needle in entry.lower() and entry.lower().endswith((".umap", ".uexp"))
        ]
        try:
            print(
                "\n".join(found) if found else "nothing in the paks matches " + needle
            )
        except OSError:  # the reader closed the pipe
            pass
        return 0
    scenes = [a for a in argv if not a.startswith("--") and a != root]
    if not scenes:
        print(__doc__)
        return 1
    ok = True
    for scene in scenes:
        ok = fetch(scene, root, force) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
