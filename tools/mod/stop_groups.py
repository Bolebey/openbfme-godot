#!/usr/bin/env python3
"""Cluster open stops into reviewable mechanism candidates; never writes game data.

The map and machine-code resolver are evidence, not proof of ownership. Review every
suggestion before opening an issue. Install capstone and pefile for --game.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import csv
import json
import re
import struct
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

from citation_check import Inventory, find_inventory

ROOT = Path(__file__).resolve().parents[2]
ID = re.compile(r'\bS-\d+\b')
HEX = re.compile(r'0x[0-9a-fA-F]{5,8}\b')
TASK = re.compile(r'\b(?:PORT[- ]\d+|Local[- ]\d+|issue\s*#\d+)\b', re.I)
# These are diagnostic/provenance links, not mechanism dependencies.
IGNORE_REFS = {'S-001'}
MAX_GROUP = 40


@dataclass
class Stop:
    id: str
    kind: str
    cells: list[str]
    addresses: set[int] = field(default_factory=set)
    functions: set[int] = field(default_factory=set)
    classes: set[str] = field(default_factory=set)
    refs: set[str] = field(default_factory=set)
    tasks: set[str] = field(default_factory=set)


def read_stops(paths):
    stops = {}
    for path in paths:
        if not path.exists():
            continue
        for line in path.read_text(encoding='utf-8').splitlines():
            if not re.match(r'^\|\s*S-\d+\s*\|', line):
                continue
            cells = [x.strip() for x in line.strip().strip('|').split('|')]
            if len(cells) > 8:
                cells = cells[:3] + [' | '.join(cells[3:-4])] + cells[-4:]
            cells += [''] * (8 - len(cells))
            sid, kind = cells[:2]
            if sid in stops:
                raise ValueError(f'duplicate stop {sid} in {path}')
            if kind in ('code', 'project'):
                stops[sid] = Stop(sid, kind, cells)
    return stops


def table(path):
    with path.open(encoding='utf-8', newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def read_map(path):
    funcs = {}
    classes = set()
    for row in table(path):
        a = int(row['function'], 16)
        c = row['module']
        classes.add(c)
        item = funcs.setdefault(a, {'classes': set(), 'status': row['status'], 'names': set()})
        item['classes'].add(c)
        if row['decomp']:
            item['names'].add(row['decomp'])
    return funcs, classes


def rw_addresses(text):
    # Explicit RW citations and immediately following address/range lists; never
    # interpret arbitrary BFME2 VAs, offsets, RVAs or test data as RotWK VAs.
    result = set()
    for m in re.finditer(r'\bRW\s+(0x[0-9a-fA-F]{5,8}(?:(?:\s*(?:/|,|\.\.|–|->)\s*)(?:RW\s*)?0x[0-9a-fA-F]{5,8})*)', text):
        result.update(int(x, 16) for x in HEX.findall(m.group(1)))
    return result


class BinaryResolver:
    """Bounded reachable basic-block search from independently found entry seeds.

    PE call targets, map starts and post-padding prologues are candidates, not
    a symbol table. We require an instruction span to actually reach a citation.
    For authoritative blocks use citation_check.Inventory instead.
    """
    def __init__(self, path, map_starts):
        import pefile
        import capstone
        pe = pefile.PE(str(path), fast_load=True)
        self.data = pe.get_memory_mapped_image()
        self.base = pe.OPTIONAL_HEADER.ImageBase
        sec = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
        self.lo = self.base + sec.VirtualAddress
        self.hi = self.lo + sec.Misc_VirtualSize
        data = self.data
        t = data[sec.VirtualAddress:sec.VirtualAddress + sec.Misc_VirtualSize]
        seeds = {a for a in map_starts if self.lo <= a < self.hi}
        for i in range(len(t) - 5):
            if t[i] in (0xe8, 0xe9):
                dest = self.lo + i + 5 + struct.unpack_from('<i', t, i + 1)[0]
                if self.lo <= dest < self.hi:
                    seeds.add(dest)
            if t[i] == 0xcc and t[i + 1] in (0x55, 0x56, 0x57, 0x53, 0x8b, 0x83, 0xb8):
                seeds.add(self.lo + i + 1)
        rdata = next((s for s in pe.sections if s.Name.rstrip(b'\0') == b'.rdata'), None)
        if rdata:
            r = data[rdata.VirtualAddress:rdata.VirtualAddress + rdata.Misc_VirtualSize]
            for i in range(0, len(r) - 4, 4):
                a = struct.unpack_from('<I', r, i)[0]
                if self.lo <= a < self.hi:
                    seeds.add(a)
        self.seeds = sorted(seeds)
        self.map_starts = sorted(a for a in map_starts if self.lo <= a < self.hi)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.cache = {}

    def reaches(self, start, target):
        # Stop at a later known entry, but do not trust indirect-jump-table seeds
        # as boundaries; reachable instructions are a stronger criterion.
        todo, visited, steps = [start], set(), 0
        while todo and steps < 3500:
            pc = todo.pop()
            while self.lo <= pc < self.hi and pc not in visited and steps < 3500:
                visited.add(pc)
                off = pc - self.base
                ins = next(self.md.disasm(self.data[off:off + 16], pc, count=1), None)
                if ins is None:
                    break
                steps += 1
                if pc <= target < pc + ins.size:
                    return True
                if ins.mnemonic.startswith('ret') or ins.mnemonic in ('int3', 'hlt', 'ud2'):
                    break
                end = pc + ins.size
                if ins.mnemonic.startswith('j'):
                    try:
                        dest = int(ins.op_str, 16)
                    except ValueError:
                        dest = None
                    if dest is not None and self.lo <= dest < self.hi and abs(dest-start) < 0x10000:
                        todo.append(dest)
                    if ins.mnemonic == 'jmp':
                        break
                pc = end
        return False

    def owner(self, a):
        if a in self.cache:
            return self.cache[a]
        if not self.lo <= a < self.hi:
            return None
        # Prefer known map starts over speculative call-target / data pointers.
        # A misread immediate or switch-table entry can point into a function.
        i = bisect.bisect_right(self.map_starts, a)
        for candidate in reversed(self.map_starts[max(0, i - 4):i]):
            if a - candidate > 0x4000:
                break
            if self.reaches(candidate, a):
                self.cache[a] = candidate
                return candidate
        i = bisect.bisect_right(self.seeds, a)
        # Bound the search: do not claim an owner merely by address proximity.
        for candidate in reversed(self.seeds[max(0, i - 24):i]):
            if a - candidate > 0x4000:
                break
            if self.reaches(candidate, a):
                self.cache[a] = candidate
                return candidate
        self.cache[a] = None
        return None


def enrich(stops, funcs, classes, resolver):
    pattern = re.compile(r'(?<![\w])(?:' + '|'.join(map(re.escape, sorted(classes, key=len, reverse=True))) + r')(?![\w])') if classes else None
    for s in stops.values():
        # Pinned-by contains test-title ranges, not dependencies.
        evidence = ' '.join(s.cells[2:5] + s.cells[6:])
        s.addresses = rw_addresses(evidence)
        s.functions = {f for a in s.addresses if (f := resolver.owner(a)) is not None}
        s.classes = set(pattern.findall(' '.join(s.cells[2:4] + s.cells[6:]))) if pattern else set()
        s.refs = (set(ID.findall(evidence)) - {s.id} - IGNORE_REFS) & stops.keys()
        s.tasks = set(TASK.findall(' '.join(s.cells)))


def cluster(stops, funcs, limit=MAX_GROUP):
    parent = {x: x for x in stops}
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    def union(a, b):
        a, b = find(a), find(b)
        if a != b:
            parent[max(a, b)] = min(a, b)
    seen = {}
    for sid, s in stops.items():
        for ref in s.refs:
            union(sid, ref)
        keys = [('class', c) for c in s.classes]
        # Common library helpers create giant, meaningless components.
        keys += [('function', a) for a in s.functions if len(funcs.get(a, {}).get('classes', ())) <= 3]
        for key in keys:
            if key in seen:
                union(sid, seen[key])
            else:
                seen[key] = sid
    components = collections.defaultdict(list)
    unplaced = []
    for sid, s in stops.items():
        if not (s.functions or s.classes or s.refs) and find(sid) == sid and not any(sid in x.refs for x in stops.values()):
            unplaced.append(sid)
        else:
            components[find(sid)].append(sid)
    groups = []
    for component in components.values():
        if len(component) <= limit:
            groups.append(sorted(component))
            continue
        # Split by a shared function first, then by class or actual references.
        # Never stuff unrelated leftovers into one arbitrary 40-stop task.
        remaining = set(component)
        while remaining:
            counts = collections.Counter(a for sid in remaining for a in stops[sid].functions
                                         if len(funcs.get(a, {}).get('classes', ())) <= 3)
            common = [(count, a) for a, count in counts.items() if count >= 2]
            if common:
                _, pivot = min(common, key=lambda x: (-x[0], x[1]))
                batch = sorted(sid for sid in remaining if pivot in stops[sid].functions)[:limit]
            else:
                counts = collections.Counter(c for sid in remaining for c in stops[sid].classes)
                common = [(count, c) for c, count in counts.items() if count >= 2]
                if common:
                    _, pivot = min(common, key=lambda x: (-x[0], x[1]))
                    batch = sorted(sid for sid in remaining if pivot in stops[sid].classes)[:limit]
                else:
                    first = min(remaining)
                    linked = {sid for sid in remaining if sid in stops[first].refs or first in stops[sid].refs}
                    batch = [first] + sorted(linked - {first})[:limit - 1]
            remaining.difference_update(batch)
            groups.append(batch)
    return groups, sorted(unplaced)


def open_issues(path):
    if path:
        return json.loads(path.read_text(encoding='utf-8'))
    try:
        p = subprocess.run(['gh', 'issue', 'list', '-R', 'Gleb302596555/openbfme-godot', '--state', 'open', '--limit', '1000', '--json', 'number,title'], capture_output=True, text=True, check=True, timeout=20)
        return json.loads(p.stdout)
    except (OSError, subprocess.SubprocessError, ValueError):
        return None


def output(stops, groups, unplaced, funcs, matrix, fields, issues, out, command):
    details = []
    for ids in groups:
        fs = set().union(*(stops[x].functions for x in ids))
        cs = set().union(*(stops[x].classes for x in ids))
        cc = collections.Counter(c for x in ids for c in stops[x].classes)
        fc = collections.Counter(f for x in ids for f in stops[x].functions)
        name = (cc.most_common(1)[0][0] if cc else f'RW 0x{fc.most_common(1)[0][0]:08X}' if fc else ids[0])
        missing = sum(funcs.get(f, {}).get('status', 'none') in ('none', 'partial') for f in fs)
        buttons = sorted({r['button'] for r in matrix if r['module class'] in cs and r['button']})
        fl = sorted({f"{r['owner']}.{r['field']}" for r in fields if r['owner'] in cs and r['status'] in ('parsed-unused', 'not-read')})
        covers = None if issues is None else [f"#{i['number']} {i['title']}" for i in issues if re.search(r'\b(?:Port|Local)[ -]+\d+', i['title'], re.I) and (any(re.search(r'(?<![\w-])'+re.escape(x)+r'(?![\w-])', i['title'], re.I) for x in ids) or any(re.search(r'(?<!\w)'+re.escape(x)+r'(?!\w)', i['title'], re.I) for x in cs))]
        details.append((ids, fs, cs, name, missing, buttons, fl, covers))
    details.sort(key=lambda x: (-(len(x[0])/x[4] if x[4] else 0), -len(x[5]), -len(x[6]), x[0]))
    out.parent.mkdir(parents=True, exist_ok=True)
    assigned = {sid: f'G{n:03d}' for n, g in enumerate(details, 1) for sid in g[0]}
    with out.with_suffix('.tsv').open('w', encoding='utf-8', newline='') as f:
        w = csv.writer(f, delimiter='\t', lineterminator='\n')
        w.writerow(['id', 'kind', 'group', 'why', 'functions', 'classes', 'references', 'reported tasks'])
        for sid, s in sorted(stops.items()):
            w.writerow([sid, s.kind, assigned.get(sid, 'unplaced'), 'function/class/reference' if sid in assigned else 'no resolvable mechanism', ', '.join(f'0x{x:08X}' for x in sorted(s.functions)), ', '.join(sorted(s.classes)), ', '.join(sorted(s.refs)), ', '.join(sorted(s.tasks))])
    lines = ['# Open stop mechanism candidates', '', f'Regenerate: `{command}`', '', 'Candidates are heuristic, not proof. Check ownership, dependencies and issue overlap before implementation. Large transitive components are split by function; links can cross groups. Unknown function status is conservatively counted as none. Unavailable issue lookup is not evidence of no coverage.', '', f'{len(stops)} open stops; {len(details)} groups; {len(unplaced)} unplaced.', '', '## Ranked groups', '', '| Rank | Mechanism | Stops | none/partial functions | Red buttons | Unused fields | Open task |', '|---:|---|---:|---:|---:|---:|---|']
    for n, (ids, fs, cs, name, missing, buttons, fl, covers) in enumerate(details, 1):
        coverage = 'lookup unavailable' if covers is None else ', '.join(covers) if covers else 'none matched'
        lines.append(f'| {n} | {name} | {len(ids)} | {missing} | {len(buttons)} | {len(fl)} | {coverage.replace("|", "/")} |')
    lines += ['', '## Unplaced', '', ', '.join(unplaced) or 'None', '', '## Top 30 task drafts', '']
    for n, (ids, fs, cs, name, missing, buttons, fl, covers) in enumerate(details[:30], 1):
        functions = ', '.join(f"RW 0x{a:08X} ({funcs.get(a, {}).get('status', 'unknown')})" for a in sorted(fs)) or 'none resolved'
        known = '; '.join(f"{sid}: " + ', '.join(f'RW 0x{a:08X}' for a in sorted(stops[sid].addresses)) for sid in ids)
        lines += [f'### Port N: close mechanism {name}: stops {", ".join(ids)}', '', '#### Problem', f'{len(ids)} open stops may share a mechanism. Verify each link; do not assume the grouping proves ownership.', '', '#### Known', f'Kinds: {", ".join(sorted({stops[x].kind for x in ids}))}. Classes: {", ".join(sorted(cs)) or "none"}.', f'Functions: {functions}.', f'Addresses: {known}.', f'Red buttons: {", ".join(buttons) or "none linked"}.', f'Unused fields: {", ".join(fl) or "none linked"}.', f'Existing task matches: {"lookup unavailable" if covers is None else "; ".join(covers) or "none"}.', '', '#### Task', 'Trace the RotWK mechanism and callers, implement only verified missing behavior, and close or narrow the relevant stops.', '', '#### Tests', 'Add synthetic regression tests for the verified behavior and adjacent branches; run the full test target.', '', '#### In game', 'Choose a verified affected Ennorath object and document reproduction and expected retail behavior.', '', '#### Do not touch', 'Unrelated stops, other open task areas, game files and generated source maps.', '', '#### Done', 'RW instructions and tests justify each change; report any remaining stops and inference.', '']
    out.with_suffix('.md').write_text('\n'.join(lines), encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', type=Path)
    ap.add_argument('--inventory', type=Path)
    ap.add_argument('--issues', type=Path, help='JSON from gh issue list; otherwise query gh')
    ap.add_argument('--out', type=Path, default=ROOT/'docs/ennorath/STOP_GROUPS')
    args = ap.parse_args()
    funcs, classes = read_map(ROOT/'docs/ennorath/RW_FUNCTION_MAP.tsv')
    stops = read_stops([ROOT/'docs/STOPS.md', *sorted((ROOT/'docs/stops.d').glob('*.md'))])
    inv = args.inventory or find_inventory(ROOT)
    if inv:
        resolver = Inventory.load(inv)
        mode = 'inventory'
    elif args.game:
        resolver = BinaryResolver(args.game, funcs)
        mode = 'game heuristic'
    else:
        class Exact:
            def owner(self, a):
                return a if a in funcs else None
        resolver = Exact()
        mode = 'exact map starts only'
    enrich(stops, funcs, classes, resolver)
    groups, unplaced = cluster(stops, funcs)
    issues = open_issues(args.issues)
    command = 'python tools/mod/stop_groups.py --game workspace/RotWK/game.dat --out docs/ennorath/STOP_GROUPS'
    output(stops, groups, unplaced, funcs, table(ROOT/'docs/ennorath/MATRIX_GAPS.tsv'), table(ROOT/'docs/ennorath/FIELD_GAPS.tsv'), issues, args.out, command)
    print(f'{len(stops)} stops, {len(groups)} groups, {len(unplaced)} unplaced; resolver: {mode}; issues: {"available" if issues is not None else "unavailable"}')


if __name__ == '__main__':
    main()
