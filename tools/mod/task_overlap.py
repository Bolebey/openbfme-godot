"""task_overlap.py: two open tasks must not port the same RotWK mechanism (owner 2026-11-19: PRs 589 and 662 both ported Call the Horde's
production speed bonus, 721 and 663 both the repair order; two of the four went back for rework).

A task's mechanism is the set of RotWK functions it names: every `RW 0x...` of an open issue's text, and for an open PR also the `RW 0x...`
of the lines its diff adds (what it actually ports), each mapped to the function that holds it (the whole-binary inventory,
rw_function_blocks_full.json). Two open tasks overlap when they share a function. A function that half the open tasks name is a hub (a shared
helper such as the damage path) and does not count.

    task_overlap.py scan  [--out task-overlap.json]     every overlapping pair of open issues / PRs (the dashboard shows them, sb4 loop)
    task_overlap.py check --draft <issue text file>     before an issue is written: exit 1 when its functions are an open task's

A PR and the issue it closes are one task. Only issues with a Port / Local title and PRs of arena / local branches are tasks.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import citation_check as cc  # noqa: E402

REPO = "Gleb302596555/openbfme-godot"
CITE = re.compile(r"\bRW\s+0x([0-9A-Fa-f]{5,8})\b")
TASK_TITLE = re.compile(r"^\s*(Port|Local|PORT|LOCAL)[\s-]+\d+", re.I)
CLOSES = re.compile(r"(?:closes|fixes|resolves)\s+#(\d+)", re.I)
HUB_SHARE = 0.5  # a function named by at least this share of the open tasks (and by 4 or more) is a hub


def gh(*args: str) -> str:
    done = subprocess.run(["gh", *args, "-R", REPO], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if done.returncode:
        raise SystemExit(f"gh {' '.join(args[:2])}: {done.stderr.strip()[:300]}")
    return done.stdout


def addresses(text: str) -> set[int]:
    return {int(a, 16) for a in CITE.findall(text or "")}


def added_lines(diff: str) -> str:
    return "\n".join(l[1:] for l in diff.splitlines() if l.startswith("+") and not l.startswith("+++"))


def functions_of(addrs: set[int], inv: cc.Inventory) -> set[int]:
    out = set()
    for a in addrs:
        f = inv.owner(a)
        if f is not None:
            out.add(f)
    return out


def open_tasks(inv: cc.Inventory) -> dict[str, dict]:
    """task key -> {title, url, functions}; a PR that closes an issue joins that issue's task."""
    tasks: dict[str, dict] = {}
    issues = json.loads(gh("issue", "list", "--state", "open", "--limit", "300", "--json", "number,title,body,url"))
    for i in issues:
        if not TASK_TITLE.match(i["title"]):
            continue
        tasks[f"#{i['number']}"] = {"title": i["title"], "url": i["url"], "functions": functions_of(addresses(i["body"]), inv), "prs": []}
    prs = json.loads(gh("pr", "list", "--state", "open", "--limit", "200", "--json", "number,title,body,headRefName,url"))
    for p in prs:
        if not p["headRefName"].startswith(("arena/", "agent/local/")):
            continue
        diff = gh("pr", "diff", str(p["number"]))
        funcs = functions_of(addresses(added_lines(diff)) | addresses(p.get("body") or ""), inv)
        m = CLOSES.search(p.get("body") or "")
        key = f"#{m.group(1)}" if m and f"#{m.group(1)}" in tasks else f"PR #{p['number']}"
        t = tasks.setdefault(key, {"title": p["title"], "url": p["url"], "functions": set(), "prs": []})
        t["functions"] |= funcs
        t["prs"].append(p["number"])
    return tasks


def hubs(tasks: dict[str, dict]) -> set[int]:
    count = defaultdict(int)
    for t in tasks.values():
        for f in t["functions"]:
            count[f] += 1
    n = max(1, len(tasks))
    return {f for f, c in count.items() if c >= 4 and c / n >= HUB_SHARE}


def overlaps(tasks: dict[str, dict], hub: set[int]) -> list[dict]:
    keys = sorted(tasks)
    out = []
    for i, a in enumerate(keys):
        for b in keys[i + 1:]:
            shared = (tasks[a]["functions"] & tasks[b]["functions"]) - hub
            if shared:
                out.append({"a": a, "b": b, "shared": sorted(f"RW 0x{f:06X}" for f in shared)})
    return sorted(out, key=lambda o: -len(o["shared"]))


def load_inventory(path: Path | None) -> cc.Inventory:
    full = cc.DRIVE_COPY.with_name("rw_function_blocks_full.json")
    p = path or (full if full.exists() else cc.find_inventory(Path(__file__).resolve().parents[2]))
    if not p or not Path(p).exists():
        raise SystemExit("task_overlap: no RotWK function inventory (rw_function_blocks_full.json): pass --inventory")
    return cc.Inventory(json.loads(Path(p).read_text(encoding="utf-8"))["functions"])


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("scan", "check"))
    ap.add_argument("--draft", type=Path, help="check: the issue text to be written")
    ap.add_argument("--out", type=Path, help="scan: the JSON for the dashboard")
    ap.add_argument("--inventory", type=Path)
    a = ap.parse_args(argv)
    inv = load_inventory(a.inventory)
    tasks = open_tasks(inv)
    hub = hubs(tasks)
    if a.cmd == "check":
        if not a.draft:
            raise SystemExit("check needs --draft")
        mine = functions_of(addresses(a.draft.read_text(encoding="utf-8")), inv) - hub
        hits = [(k, sorted(mine & t["functions"])) for k, t in sorted(tasks.items()) if mine & t["functions"]]
        if not mine:
            print("task_overlap: the draft names no RotWK function (RW 0x...): nothing to check")
            return 0
        if not hits:
            print(f"task_overlap: none of the draft's {len(mine)} functions is an open task's")
            return 0
        for k, fs in hits:
            print(f"OVERLAP {k} {tasks[k]['title'][:80]}: " + ", ".join(f"RW 0x{f:06X}" for f in fs[:10]))
        return 1
    pairs = overlaps(tasks, hub)
    data = {"at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "tasks": len(tasks), "hubs": sorted(f"RW 0x{f:06X}" for f in hub),
            "titles": {k: t["title"] for k, t in tasks.items()}, "prs": {k: t["prs"] for k, t in tasks.items() if t["prs"]}, "overlaps": pairs}
    if a.out:
        tmp = a.out.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8")
        os.replace(tmp, a.out)
    print(f"{len(tasks)} open tasks, {len(hub)} hub functions, {len(pairs)} overlapping pairs")
    for o in pairs[:30]:
        print(f"  {o['a']} <-> {o['b']}: {len(o['shared'])} shared ({', '.join(o['shared'][:4])}{' ...' if len(o['shared']) > 4 else ''})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
