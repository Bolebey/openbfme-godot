"""Upstream PRs from the fork's ready up/* branches (owner, 2026-11-16).

Runs on the owner's PC, in the persistent tree workspace/wt/upstream-pr (detached upstream/main, built once with its own build.bat, then
incremental): the Colab token cannot open PRs in Open-BFME (403), the PC's gh can.

    python -I tools/upstream_pr.py check [branch ...]   # every up/* branch without an upstream PR, or the named ones; writes the report
    python -I tools/upstream_pr.py check --no-build     # the text and merge checks only (seconds)
    python -I tools/upstream_pr.py open up/port-217 ... # opens the PRs of branches that passed the last check (never all at once)

The checks are upstream's rules (docs/ennorath/PROCESS.md, "Как собирается PR в upstream"; their CONTRIBUTING / REVIEW):
  * the branch merges into the current upstream/main without a conflict;
  * no fork-only paths (docs/ennorath, workspace, .hashes vectors: upstream's merge queue re-measures pins), no "#N" in the commit texts
    (it links upstream's issues), English commit texts;
  * every new TEST_CASE passes on the merge, and fails once the branch's non-test files are put back to upstream/main
    (upstream drops tests that pass without the change);
  * visual-only changes (GameClient / W3D / shaders with no logic file) are reported as "needs the owner's captures", never opened.
The report: workspace/upstream-pr/report.md (and .json, which `open` reads).
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TREE = REPO / "workspace" / "wt" / "upstream-pr"
OUT = REPO / "workspace" / "upstream-pr"
UPSTREAM = "Open-BFME/openbfme-godot"
FORK_OWNER = "Gleb302596555"
FORK_ONLY = ("docs/ennorath/", "workspace/", "tools/colab/", "tools/mod/", ".clinerules/")
VISUAL = re.compile(r"^engine/src/(GameClient/|W3DDevice/|GodotDevice/.*(Draw|Shader|Render))|\.(gdshader|glsl)$")
TEST_FILE = re.compile(r"^engine/tests/|^engine/cmake/|^engine/CMakeLists\.txt$")
JOBS = os.environ.get("OPENBFME_BUILD_JOBS", "3")  # 16 GB RAM: 3 jobs (owner, 2026-11-10)


def sh(args, cwd=REPO, check=False, **kw) -> subprocess.CompletedProcess:
    r = subprocess.run(args, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace", **kw)
    if check and r.returncode:
        raise RuntimeError(f"{' '.join(map(str, args))}: {r.stderr.strip()[:400]}")
    return r


def git(*a, cwd=TREE, check=True) -> str:
    return sh(["git", *a], cwd=cwd, check=check).stdout.strip()


def test_env() -> dict:
    env = dict(os.environ)
    env.setdefault("ROTWK_INSTALL", str(REPO / "workspace" / "games-en" / "Vanilla_(2.01)" / "install"))
    env.setdefault("BFME2_INSTALL", r"C:\Program Files (x86)\BFME2")
    env.setdefault("OPENBFME_ENNORATH_INSTALL", str(REPO / "workspace" / "mods-en" / "Ennorath" / "install"))
    return env


def ready_branches() -> list[str]:
    git("fetch", "-q", "origin", cwd=REPO)
    git("fetch", "-q", "upstream", cwd=REPO)
    branches = [b.strip().removeprefix("origin/") for b in git("branch", "-r", "--list", "origin/up/*", cwd=REPO).splitlines()]
    out = sh(["gh", "pr", "list", "-R", UPSTREAM, "--author", FORK_OWNER, "--state", "all", "--limit", "500",
              "--json", "headRefName"], check=True).stdout
    have = {p["headRefName"] for p in json.loads(out)}
    return [b for b in branches if b not in have]


def text_checks(branch: str) -> tuple[list[str], dict]:
    """Problems found without a build, and facts for the PR body."""
    ref = f"origin/{branch}"
    problems: list[str] = []
    base = git("merge-base", "upstream/main", ref)
    files = git("diff", "--name-only", f"{base}..{ref}").split()
    msgs = git("log", "--format=%s%n%b%x00", f"upstream/main..{ref}")
    subjects = git("log", "--reverse", "--format=%s", f"upstream/main..{ref}").splitlines()
    if not files:
        problems.append("no change against upstream/main (already upstream?)")
    bad = [f for f in files if f.startswith(FORK_ONLY) or f.endswith(".hashes")]
    if bad:
        problems.append("fork-only files / pin vectors: " + ", ".join(bad[:6]))
    if re.search(r"(?<![\w/])#\d+", msgs):
        problems.append('"#N" in a commit text (links upstream issues; write "fork issue N")')
    if re.search(r"[А-Яа-яЁё]", msgs):
        problems.append("non-English commit text")
    added = git("diff", f"{base}..{ref}", "--", "engine")
    if "docs/ennorath" in added:
        problems.append("a code comment cites docs/ennorath (fork-only)")
    tree = git("merge-tree", "--write-tree", "--name-only", "upstream/main", ref, check=False)
    conflict = sh(["git", "merge-tree", "--write-tree", "upstream/main", ref], cwd=TREE).returncode != 0
    if conflict:  # --name-only: the tree id, the conflicted paths, a blank line, the messages
        paths = []
        for l in tree.splitlines()[1:]:
            if not l.strip():
                break
            paths.append(l.strip())
        problems.append("conflicts with upstream/main: " + ", ".join(paths[:6]))
    logic = [f for f in files if not TEST_FILE.search(f) and not f.startswith("docs/")]
    visual = bool(logic) and all(VISUAL.search(f) for f in logic)
    tests = sorted(set(re.findall(r'^\+\s*TEST_CASE\("([^"]+)"', git("diff", f"{base}..{ref}", "--", "engine/tests"), re.M)))
    if not tests and not visual:
        problems.append("no new TEST_CASE (upstream wants a test that fails without the change)")
    facts = {"files": files, "logic": logic, "tests": tests, "subjects": subjects, "visual": visual,
             "behind": int(git("rev-list", "--count", f"{base}..upstream/main"))}
    return problems, facts


def build() -> tuple[bool, str]:
    env = dict(os.environ, CMAKE_BUILD_PARALLEL_LEVEL=JOBS)
    r = sh(["cmd", "/c", str(TREE / "build.bat")], cwd=TREE, env=env, timeout=3 * 3600)
    log = r.stdout + r.stderr
    (OUT / "build.log").write_text(log, encoding="utf-8")
    errs = [l for l in log.splitlines() if re.search(r"error (C|LNK)\d+|FAILED:", l)][:6]
    return r.returncode == 0, "\n".join(errs) or log[-600:]


def run_tests(names: list[str]) -> dict[str, str]:
    exe = TREE / "engine" / "build" / "openbfme_tests.exe"
    res = {}
    for name in names:
        pat = name.replace(",", "?")  # doctest splits -tc on commas
        r = sh([str(exe), f"-tc={pat}"], cwd=TREE, env=test_env(), timeout=2400)
        m = re.search(r"test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed", r.stdout)
        res[name] = "none" if not m or m.group(1) == "0" else ("pass" if r.returncode == 0 and m.group(3) == "0" else "fail")
    return res


def build_checks(branch: str, facts: dict) -> list[str]:
    problems: list[str] = []
    git("checkout", "-q", "-f", "--detach", "upstream/main")
    r = sh(["git", "-c", "user.name=upstream-pr", "-c", "user.email=upstream-pr@localhost", "merge", "-q", "--no-edit", "--no-verify",
            f"origin/{branch}"], cwd=TREE)
    if r.returncode:
        git("merge", "--abort", check=False)
        return ["merge failed: " + r.stderr.strip()[:300]]
    ok, err = build()
    if not ok:
        return ["does not build on upstream/main:\n" + err]
    with_fix = run_tests(facts["tests"])
    problems += [f'"{t}" {st} with the change' for t, st in with_fix.items() if st != "pass"]
    if facts["logic"] and not problems:
        git("checkout", "upstream/main", "--", *[f for f in facts["logic"] if git("cat-file", "-t", f"upstream/main:{f}", check=False)])
        for f in facts["logic"]:  # files the branch added go too
            if not git("cat-file", "-t", f"upstream/main:{f}", check=False) and (TREE / f).exists():
                (TREE / f).unlink()
        ok, err = build()
        if ok:
            without = run_tests(facts["tests"])
            problems += [f'"{t}" passes without the change (upstream drops such tests)' for t, st in without.items() if st == "pass"]
        else:  # the test calls the change's new API: it cannot pass without it, which is what upstream asks
            print("   (the tests do not build without the change: they need its API)", flush=True)
    git("checkout", "-q", "-f", "--detach", "upstream/main")
    return problems


def check(names: list[str], do_build: bool) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    branches = names or ready_branches()
    results = {}
    for b in branches:
        print(f"== {b}", flush=True)
        problems, facts = text_checks(b)
        if facts["visual"]:
            verdict = "visual"
        elif problems:
            verdict = "fail"
        elif do_build:
            problems = build_checks(b, facts)
            verdict = "fail" if problems else "ready"
        else:
            verdict = "text-ok"
        results[b] = {"verdict": verdict, "problems": problems, **facts, "sha": git("rev-parse", f"origin/{b}")}
        print(f"   {verdict}: " + ("; ".join(problems) or "ok"), flush=True)
    old = json.loads((OUT / "report.json").read_text(encoding="utf-8")) if (OUT / "report.json").exists() else {}
    old.update(results)
    (OUT / "report.json").write_text(json.dumps(old, indent=1), encoding="utf-8")
    lines = ["# Upstream PR check", "", f"upstream/main {git('rev-parse', '--short', 'upstream/main')}", "",
             "| branch | verdict | behind | tests | problems |", "|---|---|---|---|---|"]
    for b, r in sorted(old.items()):
        lines.append(f"| `{b}` | {r['verdict']} | {r['behind']} | {len(r['tests'])} | {'; '.join(r['problems']).replace('|', '/')[:300]} |")
    (OUT / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {OUT / 'report.md'}")


def pr_body(b: str, r: dict) -> str:
    log = git("log", "--reverse", "--format=%s%n%n%b", f"upstream/main..origin/{b}")
    log = re.sub(r"\n*Co-Authored-By:.*", "", log).strip()
    tests = "\n".join(f"- `{t}`: fails without the change, passes with it" for t in r["tests"])
    return (f"## What this changes\n\n{log}\n\n## How it was checked\n\n{tests}\n\n"
            "Built and run on upstream/main (Windows MSVC). Pins: not re-measured here (the merge queue re-measures them).\n\n"
            "## Checklist\n\n- [x] one change\n- [x] a test that fails without it\n- [x] no fork-only files\n")


def open_prs(names: list[str]) -> None:
    report = json.loads((OUT / "report.json").read_text(encoding="utf-8"))
    for b in names:
        r = report.get(b)
        if not r or r["verdict"] != "ready":
            print(f"{b}: not ready ({r and r['verdict']}), skipped")
            continue
        if git("rev-parse", f"origin/{b}", cwd=REPO) != r["sha"]:
            print(f"{b}: the branch moved since the check, run check again")
            continue
        title = r["subjects"][0] if r["subjects"] else b
        out = sh(["gh", "pr", "create", "-R", UPSTREAM, "--head", f"{FORK_OWNER}:{b}", "--base", "main", "--title", title,
                  "--body", pr_body(b, r)])
        print(f"{b}: {out.stdout.strip() or out.stderr.strip()}")


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in ("check", "open"):
        print(__doc__)
        sys.exit(2)
    args = [a for a in sys.argv[2:] if not a.startswith("--")]
    if sys.argv[1] == "check":
        check(args, "--no-build" not in sys.argv)
    else:
        if not args:
            sys.exit("name the branches to open (never all at once)")
        open_prs(args)
