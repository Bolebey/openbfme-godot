#!/usr/bin/env python3
"""Before an issue goes out: is its work already in main, or in an open issue / PR? (owner, 2026-11-14)

    issue_check.py <issue-body.md> [--skip <issue>]   a draft body
    issue_check.py --issue <N>                         an existing issue (compared with the others)

Reads the RW addresses (`RW 0x...`), stop ids (`S-NNNN`) and port paths (`engine/...`) the text names, then:
1. each RW address cited in engine/ code: the port already reads it (a ported part, or the work is done: Port 428
   repeated Port 148 this way);
2. open fork issues that name the same stop ids or RW addresses;
3. open fork PRs whose changed files are the paths the text names, or that cite its RW addresses in their diff.
Nothing is decided here: the output is what to read before sending the task. Needs `gh` (repo Gleb302596555/openbfme-godot).
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GH = "Gleb302596555/openbfme-godot"
ADDR = re.compile(r"RW (0x[0-9A-Fa-f]{6})")
STOP = re.compile(r"\bS-\d{3,5}\b")
GAP = re.compile(r"not ported|NOT PORTED|not identified|unported|\[S-\d+\]|INFERENCE|stop", re.I)
PATH = re.compile(r"\b(engine/[\w./-]+\.(?:cpp|h))")


def sh(*cmd: str) -> str:
    return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout


def gh_json(*args: str):
    out = sh("gh", *args, "-R", GH)
    return json.loads(out) if out.strip() else []


def norm(a: str) -> str:
    return "0x" + a[2:].upper()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("body", nargs="?")
    ap.add_argument("--issue", type=int)
    ap.add_argument("--skip", type=int, action="append", default=[])
    a = ap.parse_args()
    if a.issue:
        text = gh_json("issue", "view", str(a.issue), "--json", "title,body")
        text = text["title"] + "\n" + text["body"]
        a.skip.append(a.issue)
    elif a.body:
        text = Path(a.body).read_text(encoding="utf-8")
    else:
        ap.error("a body file or --issue")
    addrs = sorted({norm(m) for m in ADDR.findall(text)})
    stops = sorted(set(STOP.findall(text)))
    paths = sorted(set(PATH.findall(text)))
    print(f"names {len(addrs)} RW addresses, stops {', '.join(stops) or '-'}, paths {', '.join(paths) or '-'}\n")

    print("1. RW addresses already cited in engine/ (the port reads them):")
    hits = 0
    for ad in addrs:
        found = sh("git", "grep", "-n", "-i", "-I", ad, "--", "engine/src")
        lines = [l for l in found.splitlines() if l]
        # a citation inside a stop / "not ported" text names the gap, it does not port it
        real = [l for l in lines if not GAP.search(l)]
        if lines:
            hits += 1 if real else 0
            print(f"   {ad}: {len(real)} code citation(s), {len(lines) - len(real)} in stop / not-ported text")
            for l in (real or lines)[:3]:
                print("     ", ("" if real else "[gap] ") + l[:180])
    print(f"   {hits} of {len(addrs)} cited outside stop text" + ("" if hits else " (none: the work is not in main)"))

    print("\n2. open issues naming the same stops / addresses:")
    issues = gh_json("issue", "list", "--state", "open", "--limit", "300", "--json", "number,title,body")
    any2 = False
    for i in issues:
        if i["number"] in a.skip:
            continue
        t = i["title"] + "\n" + (i["body"] or "")
        common = sorted(set(stops) & set(STOP.findall(t))) + sorted(set(addrs) & {norm(m) for m in ADDR.findall(t)})
        if common:
            any2 = True
            print(f"   #{i['number']} {i['title'][:90]}\n      shares {', '.join(common[:8])}")
    if not any2:
        print("   none")

    print("\n3. open PRs touching the same files or addresses:")
    prs = gh_json("pr", "list", "--state", "open", "--limit", "100", "--json", "number,title,files")
    any3 = False
    for p in prs:
        files = {f["path"] for f in p.get("files", [])}
        same = sorted(files & set(paths))
        diff = sh("gh", "pr", "diff", str(p["number"]), "-R", GH) if addrs else ""
        shared = sorted({norm(m) for m in re.findall(r"^\+.*?RW (0x[0-9A-Fa-f]{6})", diff, re.M)} & set(addrs))
        if same or shared:
            any3 = True
            print(f"   PR #{p['number']} {p['title'][:90]}")
            if same:
                print(f"      files: {', '.join(same)}")
            if shared:
                print(f"      addresses: {', '.join(shared[:8])}")
    if not any3:
        print("   none")
    return 0


if __name__ == "__main__":
    sys.exit(main())
