#!/usr/bin/env python3
"""The script map of Ennorath: every script of the script libraries the game runs for the sides (Libraries\\<name>\\<name>.map: the human
side's Multiplayer_Human, the AI sides' libraries) and of the .scb script files, as the mod's mount resolves them, with what each script
reads and what it gives (owner 2026-11-19: the Manwe science comes from Multiplayer_Human's ImladrisTechManve, not from any INI module, and
neither the agents nor the matrix could see it: the libraries are RefPack-compressed maps).

    python tools/mod/script_map.py <RotWK install> <mod install> [--out docs/ennorath]

Mount: the mod's archives first (case-insensitive name order, the first archive that holds a path wins: more leading underscores first,
as the mod names them), then the RotWK 2.01 archives in the pinned policy order (tools/fx/bigfs.py). The script tree and its records are
tools/script/script_census.py's reader (ZH Scripts.cpp layouts as BFME2 writes them); every condition / action is resolved against the
binary's registry (engine/src/GameLogic/ScriptEngine/ScriptTemplateTables.inc) with RotWK's parser rules (script_census.resolve).

Writes (names only, no retail or mod bytes) into --out:
- SCRIPT_MAP.tsv (into --full only, never the repository: it transcribes the scripts): one line per script: library, group, script, flags, conditions, actions, else actions, the condition / action types the
  port has no case for;
- SCRIPT_GRANTS.tsv: one line per thing a script gives a player (a science, an upgrade, an object, ...): kind, name, the action, library,
  script, and the objects / upgrades / sciences its conditions read (the trigger);
- SCRIPT_MAP.md: counts and the condition / action types the mod's scripts use that the port does not execute.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "fx"))
sys.path.insert(0, str(ROOT / "tools" / "maps" / "oracle"))
sys.path.insert(0, str(ROOT / "tools" / "script"))
sys.path.insert(0, str(ROOT / "tools" / "mod"))

import bigfs  # noqa: E402

census = sc = None  # imported in main(): census.py reads ROTWK_INSTALL / BFME2_INSTALL at import, this tool takes the install as an argument

ENGINE = ROOT / "engine" / "src" / "GameLogic" / "ScriptEngine"
GIVE = re.compile(r"GRANT|GIVE|PURCHASE|CREATE|SPAWN|UPGRADE|SCIENCE|MONEY|ADD_|SET_.*(UPGRADE|SCIENCE)", re.I)


def mount(base: Path, overlay: Path) -> bigfs.Mount:
    m = bigfs.Mount.__new__(bigfs.Mount)
    m.files = {}
    for p in sorted(overlay.glob("*.big"), key=lambda q: q.name.lower()):
        m._add(p)
    policy = json.loads((bigfs.POLICY_DIR / "rotwk-201-english-archives.json").read_text())
    for a in policy["archives"]:
        m._add(bigfs._find(base, a["path"]))
    return m


def ini_names(m: bigfs.Mount) -> dict[str, str]:
    """name -> kind for every Object / Science / Upgrade the mount's INI defines (the mod's files win, as above)."""
    kinds: dict[str, str] = {}
    pat = re.compile(r"^[ \t]*(Object|ChildObject|ObjectReskin|Science|Upgrade|SpecialPower|PlayerTemplate)[ \t]+(\w+)", re.M)
    for n in m.list("data\\ini\\"):
        if not n.endswith((".ini", ".inc")):
            continue
        text = m.read(n).decode("latin-1")
        for t, name in pat.findall(text):
            kind = {"ChildObject": "object", "ObjectReskin": "object", "Object": "object"}.get(t, t.lower())
            kinds.setdefault(name, kind)
    return kinds


def port_cases() -> tuple[set[str], set[str]]:
    """Condition / action names the port's script engine handles: `case NAME:` in the conditions, `"NAME"` in the actions."""
    conds, acts = set(), set()
    for p in ENGINE.glob("*.cpp"):
        t = p.read_text(encoding="utf-8", errors="replace")
        conds |= set(re.findall(r"\bcase\s+([A-Z][A-Z0-9_]+)\s*:", t))
        acts |= set(re.findall(r'"([A-Z][A-Z0-9_]{3,})"', t))
    return conds, acts


def scripts_of(body: bytes, reg) -> list[dict]:
    names, p = census.parse_toc(body)
    rec = []
    census.walk(body, p, len(body), names, "", rec)
    out: list[dict] = []
    groups: list[tuple[int, str]] = []
    cur = None
    for full, ver, size, pos in sc.script_records(body, names, rec):
        depth = full.count("/")
        leaf = full.rsplit("/", 1)[-1]
        r = sc.Reader(body, pos, pos + size, names)
        groups = [(d, g) for d, g in groups if d < depth]
        if leaf == "ScriptGroup":
            groups.append((depth, r.astr()))
        elif leaf == "Script":
            nm = r.astr()
            r.astr(); r.astr(); r.astr()
            flags = [r.u8() for _ in range(6)]
            cur = {"script": nm, "group": "/".join(g for _, g in groups), "active": flags[0], "oneShot": flags[1], "depth": depth,
                   "cond": [], "act": [], "else": [], "or": 0}
            out.append(cur)
        elif leaf == "OrCondition" and cur is not None:
            cur["or"] += 1
            if cur["cond"]:
                cur["cond"].append("OR")
        elif leaf in ("Condition", "ScriptAction", "ScriptActionFalse") and cur is not None:
            kind = {"Condition": "condition", "ScriptAction": "action", "ScriptActionFalse": "action"}[leaf]
            typ = r.i32()
            name = r.namekey() if ver >= (4 if kind == "condition" else 2) else None
            vals: list = []
            ps = sc.params(r, vals)
            inverted = enabled = None
            if kind == "condition" and ver >= 5:
                enabled, inverted = r.i32(), r.i32()
            if kind == "action" and ver >= 3:
                enabled = r.i32()
            table = reg[kind]
            resolved = sc.resolve(table, kind, typ, name, len(ps), ver)
            rname = table[resolved][0] if resolved in table else (name or "?%d" % typ)
            args = []
            for t, v in vals:
                args.append(("%.0f,%.0f,%.0f" % v) if t == 16 else (v[2] if v[2] else ("%d" % v[0] if v[1] == 0 else "%d/%g" % (v[0], v[1]))))
            item = {"name": rname, "args": args, "off": enabled == 0, "not": bool(inverted)}
            cur[{"Condition": "cond", "ScriptAction": "act", "ScriptActionFalse": "else"}[leaf]].append(item)
    return out


def fmt(item) -> str:
    if item == "OR":
        return "OR"
    s = "%s%s(%s)" % ("NOT " if item["not"] else "", item["name"], ", ".join(item["args"]))
    return s + (" [off]" if item["off"] else "")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("base", type=Path)
    ap.add_argument("overlay", type=Path)
    ap.add_argument("--out", type=Path, default=ROOT / "docs" / "ennorath")
    ap.add_argument("--full", type=Path, help="where SCRIPT_MAP.tsv (every script's conditions and actions with their parameters) goes: the audit"
                    " folder (Drive, the arena archive), never the repository: it transcribes the mod's scripts")
    a = ap.parse_args()
    import os
    os.environ.setdefault("ROTWK_INSTALL", str(a.base))
    os.environ.setdefault("BFME2_INSTALL", str(a.base))  # census.py only checks it is set; BFME2's archives are not mounted here
    global census, sc
    import census as _census
    import script_census as _sc
    census, sc = _census, _sc
    m = mount(a.base, a.overlay)
    reg = sc.load_registry()
    kinds = ini_names(m)
    pconds, pacts = port_cases()
    # the ordinals the ScriptEngine runs itself (script_census.ENGINE_OWN: never reach the conditions / actions switch)
    own = {reg[k][i][0] for k, ks in (("condition", "condition"), ("action", "action")) for i in sc.ENGINE_OWN[ks] if i in reg[k]}
    files = [n for n in m.files if (n.startswith("libraries\\") and n.endswith(".map")) or n.endswith(".scb")]
    rows, grants = [], []
    unported = collections.Counter()
    unported_where = collections.defaultdict(set)
    for path in sorted(files):
        blob = m.read(path)
        if not blob:
            continue
        body, _ = census.decode(blob)
        src = "mod" if str(m.files[path][0]).startswith(str(a.overlay)) else "rotwk"
        for s in scripts_of(body, reg):
            missing = []
            for it in s["cond"]:
                if it != "OR" and it["name"] not in pconds and it["name"] not in own:
                    missing.append(it["name"])
            for it in s["act"] + s["else"]:
                if it["name"] not in pacts and it["name"] not in own:
                    missing.append(it["name"])
            for n in missing:
                unported[n] += 1
                unported_where[n].add(path)
            trig = sorted({x for it in s["cond"] if it != "OR" for x in it["args"] if x in kinds})
            for it in s["act"] + s["else"]:
                if it["off"] or not GIVE.search(it["name"]):
                    continue
                for x in it["args"]:
                    if x in kinds and kinds[x] in ("science", "upgrade", "object", "specialpower"):
                        grants.append((kinds[x], x, it["name"], path, s["script"], "yes" if s["active"] else "no", " ".join(trig),
                                       "no" if it["name"] in missing else "yes"))
            rows.append((path, src, s["group"], s["script"], "%d%d" % (s["active"], s["oneShot"]), " AND ".join(fmt(c) for c in s["cond"]).replace("AND OR AND", "OR"),
                         " ; ".join(fmt(c) for c in s["act"]), " ; ".join(fmt(c) for c in s["else"]), " ".join(sorted(set(missing)))))
    a.out.mkdir(parents=True, exist_ok=True)
    clean = lambda v: str(v).replace("\t", " ").replace("\n", " ")  # noqa: E731
    if a.full:
        a.full.mkdir(parents=True, exist_ok=True)
        with open(a.full / "SCRIPT_MAP.tsv", "w", encoding="utf-8", newline="\n") as f:
            f.write("# The Ennorath script map (tools/mod/script_map.py): every script of the sides' script libraries and .scb files of the mod's mount.\n")
            f.write("file\tsource\tgroup\tscript\tactive,oneShot\tconditions\tactions\telse actions\tport has no case for\n")
            for r in rows:
                f.write("\t".join(clean(v) for v in r) + "\n")
    with open(a.out / "SCRIPT_GRANTS.tsv", "w", encoding="utf-8", newline="\n") as f:
        f.write("# What the scripts give a player (tools/mod/script_map.py): look here before calling a science / upgrade / object 'given by nothing'.\n")
        f.write("kind\tname\taction\tfile\tscript\tactive\ttrigger (INI names its conditions read)\tport runs the action\n")
        for g in sorted(set(grants), key=lambda g: (g[0], g[1].lower(), g[3], g[4])):
            f.write("\t".join(clean(v) for v in g) + "\n")
    lines = ["# Ennorath script map", "",
             "Generated by `tools/mod/script_map.py` (re-run with the nightly maps). Part of the mod's mechanics is not in INI modules but in the",
             "script libraries the game runs for every side (`Libraries\\<name>\\<name>.map`, RefPack-compressed: a text search of the archives does",
             "not find them). Example: `Multiplayer_Human` / `ImladrisTechManve` grants `SCIENCE_ManveWisdom` while the player owns",
             "`LoremasterThingObjectManveReal` (the Manwe choice).", "",
             "- `SCRIPT_GRANTS.tsv` — every science / upgrade / object / power a script gives, with its trigger. **Before calling something",
             "  'given by nothing' (a button that never opens, a science nobody grants), look it up here.**",
             "- `SCRIPT_MAP.tsv` — every script with its conditions and actions (parameters included) and the condition / action types the port has no",
             "  case for. It transcribes the mod's scripts, so it is not in the repository: `script-map/SCRIPT_MAP.tsv` of the field audit",
             "  (Drive `field-audit-auto`, `ennorath-audit.zip` of the arena archive).", "",
             "Files: %d, scripts: %d, grants: %d." % (len({r[0] for r in rows}), len(rows), len(set(grants))), "",
             "## Condition / action types the mod's scripts use that the port does not execute", "",
             "| type | uses | files |", "|---|---|---|"]
    for n, c in unported.most_common():
        lines.append("| `%s` | %d | %s |" % (n, c, ", ".join(sorted(unported_where[n]))[:300]))
    (a.out / "SCRIPT_MAP.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("files %d scripts %d grants %d unported types %d" % (len({r[0] for r in rows}), len(rows), len(set(grants)), len(unported)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
