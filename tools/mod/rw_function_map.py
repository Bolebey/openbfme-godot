#!/usr/bin/env python3
"""Building blocks for the RotWK module call-graph audit.

This is a work in progress: these routines operate on a bounded x86 function;
there is not yet a retail inventory, module resolver, report generator or CLI.
No bytes from retail images are emitted.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from pathlib import Path
import re

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM


@dataclass(frozen=True)
class Function:
    calls: frozenset[int]
    vcalls: frozenset[int]
    instructions: int


def decode_function(data: bytes, base: int, start: int, end: int, entries: set[int]) -> Function:
    """Walk control flow in [start, end); only known entries become tail calls.

    Branches into the current function are followed, not classified as calls.
    Indirect memory calls record their displacement only: they are not resolved.
    The caller supplies independently determined function boundaries and entries.
    """
    if not (base <= start < end <= base + len(data)):
        raise ValueError("function bounds outside image")
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    pending = [start]
    visited: set[int] = set()
    calls: set[int] = set()
    vcalls: set[int] = set()
    while pending:
        address = pending.pop()
        while start <= address < end and address not in visited:
            offset = address - base
            ins = next(decoder.disasm(data[offset: min(end - base, offset + 15)], address, count=1), None)
            if ins is None:
                raise ValueError(f"undecodable instruction at 0x{address:X}")
            visited.add(address)
            nxt = address + ins.size
            if ins.mnemonic.startswith("ret") or ins.mnemonic in ("int3", "ud2", "hlt"):
                break
            if ins.mnemonic == "call":
                op = ins.operands[0]
                if op.type == X86_OP_IMM:
                    calls.add(op.imm)
                elif op.type == X86_OP_MEM and op.mem.base:
                    vcalls.add(op.mem.disp)
            elif ins.mnemonic == "jmp":
                op = ins.operands[0]
                if op.type == X86_OP_IMM:
                    if op.imm in entries and op.imm != start:
                        calls.add(op.imm)
                    elif start <= op.imm < end:
                        pending.append(op.imm)
                break
            elif ins.mnemonic.startswith("j") and ins.operands[0].type == X86_OP_IMM:
                target = ins.operands[0].imm
                if start <= target < end:
                    pending.append(target)
            address = nxt
    return Function(frozenset(calls), frozenset(vcalls), len(visited))


def walk_graph(roots: set[int], functions: dict[int, Function], stops: set[int]):
    """Return minimum depths and all incoming edges; stopped nodes are included."""
    depth: dict[int, int] = {}
    incoming: dict[int, set[int]] = {}
    queue = deque((root, 0) for root in sorted(roots))
    while queue:
        address, d = queue.popleft()
        if address in depth:
            continue
        depth[address] = d
        if address in stops or address not in functions:
            continue
        for child in sorted(functions[address].calls):
            incoming.setdefault(child, set()).add(address)
            queue.append((child, d + 1))
    return depth, incoming


CITE = re.compile(r"\bRW\s+0x([0-9a-fA-F]{6,8})\b")
PARTIAL = re.compile(r"\bS-\d+\b|\bNOT PORTED\b|\bINFERENCE\b", re.I)


def scan_citations(root: Path) -> dict[int, str]:
    """Classify explicit cites; any unmarked citation takes precedence."""
    found: dict[int, str] = {}
    for path in sorted(root.rglob('*')):
        if not path.is_file() or path.suffix.lower() not in {'.c', '.cc', '.cpp', '.h', '.hpp', '.inl', '.inc'}:
            continue
        lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
        for i, line in enumerate(lines):
            # A marker on the citation line or the next line qualifies the citation.
            context = line + '\n' + (lines[i + 1] if i + 1 < len(lines) else '')
            status = 'partial' if PARTIAL.search(context) else 'ported'
            for match in CITE.finditer(line):
                address = int(match.group(1), 16)
                if address not in found or status == 'ported':
                    found[address] = status
    return found


def importance(objects: int, depth: int, instructions: int, module: str, address: int):
    return (-objects, depth, -instructions, module, address)


def cited_neighbours(address: int, functions: dict[int, Function], citations: dict[int, str]):
    """Cited immediate callers and callees, sorted by address, with direction.

    This is evidence for manual investigation, not proof that the uncited
    function itself was ported. Only citations supplied by the caller count.
    """
    result: set[tuple[str, int, str]] = set()
    for callee in functions.get(address, Function(frozenset(), frozenset(), 0)).calls:
        if callee in citations:
            result.add(('callee', callee, citations[callee]))
    for caller, function in functions.items():
        if address in function.calls and caller in citations:
            result.add(('caller', caller, citations[caller]))
    return sorted(result)


class DecompLookup:
    """Optional counterpart map using the same bounds and fields as rw2decomp."""

    def __init__(self, path: Path):
        import sys
        from bisect import bisect_right

        self._bisect_right = bisect_right
        self.rows = []
        self.starts = []
        if not path.exists():
            return
        # rw2decomp owns CSV parsing and RotWK RVA/size interpretation.
        re_dir = str(Path(__file__).resolve().parents[1] / 're')
        if re_dir not in sys.path:
            sys.path.insert(0, re_dir)
        import rw2decomp
        self.rows = rw2decomp.load(path)
        self.starts = [r['_rva'] for r in self.rows]

    def get(self, address: int) -> str:
        rva = address - 0x400000
        i = self._bisect_right(self.starts, rva) - 1
        if i < 0 or rva >= self.starts[i] + self.rows[i]['_size']:
            return ''
        row = self.rows[i]
        # No BFME2 counterpart: do not present the RotWK name as decomp.
        if row['tier'] == '-':
            return ''
        name = row.get('decomp_qualname') or row.get('decomp_name') or ''
        return f"{name} (tier {row['tier']})" if name else ''


@dataclass(frozen=True)
class MapRow:
    module: str
    depth: int
    function: int
    status: str
    called_from: tuple[int, ...] = ()
    vcalls: tuple[int, ...] = ()
    neighbours: tuple[tuple[str, int, str], ...] = ()
    decomp: str = ''
    objects: int = 0
    instructions: int = 0


TSV_COLUMNS = ('module', 'depth', 'function', 'status', 'called from', 'vcalls', 'neighbour cited', 'decomp')


def write_reports(rows: list[MapRow], tsv: Path, markdown: Path, command: str,
                  hand_stops: dict[int, str], frequency_stops: dict[int, int], threshold: int):
    """Write reports from already validated graph rows, printing improvements.

    Counts are per (module, function) row, not distinct binary functions.
    No module coverage, function boundaries or stop frequencies are inferred here.
    The inventory/graph producer is responsible for supplying those facts.
    """
    import csv
    from collections import Counter

    keys = [(r.module, r.function) for r in rows]
    if len(keys) != len(set(keys)):
        raise ValueError('duplicate module/function row')
    if any(r.status not in {'none', 'partial', 'ported'} or r.depth < 0 or r.objects < 0 or r.instructions < 0 for r in rows):
        raise ValueError('invalid map row')
    previous = {}
    if tsv.exists():
        with tsv.open(newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f, delimiter='\t')
            if tuple(reader.fieldnames or ()) != TSV_COLUMNS:
                raise ValueError(f'invalid existing report columns: {reader.fieldnames}')
            for row in reader:
                key = (row['module'], int(row['function'], 16))
                if key in previous:
                    raise ValueError(f'duplicate existing report row: {key}')
                previous[key] = row['status']
    ordered = sorted(rows, key=lambda r: (r.module.casefold(), r.module, r.depth, r.function))
    changed = [(r.module, r.function, previous[(r.module, r.function)]) for r in ordered
               if r.status == 'ported' and previous.get((r.module, r.function)) in {'none', 'partial'}]
    tsv.parent.mkdir(parents=True, exist_ok=True)
    markdown.parent.mkdir(parents=True, exist_ok=True)
    with tsv.open('w', newline='', encoding='utf-8') as f:
        out = csv.writer(f, delimiter='\t', lineterminator='\n')
        out.writerow(TSV_COLUMNS)
        for r in ordered:
            out.writerow((r.module, r.depth, f'0x{r.function:08X}', r.status,
                          ', '.join(f'0x{x:08X}' for x in sorted(r.called_from)),
                          ', '.join(f'vcall {x:+#x}' for x in sorted(r.vcalls)),
                          ', '.join(f'{direction} 0x{address:08X} ({status})'
                                    for direction, address, status in sorted(r.neighbours)), r.decomp))
    counts = Counter(r.status for r in rows)
    lines = ['# RotWK module function map', '', f'Command: `{command}`', '',
             f"Rows: {len(rows)}; ported {counts['ported']}, partial {counts['partial']}, none {counts['none']}.",
             'Counts are per module/function pair, not unique functions.', '',
             '## Stop list', '', f'Frequency threshold: more than {threshold} call sites.', '',
             'Hand list:']
    lines.extend(f'- 0x{a:08X}: {name}' for a, name in sorted(hand_stops.items()))
    if not hand_stops:
        lines.append('- (empty)')
    lines.extend(['', 'Frequency list:'])
    lines.extend(f'- 0x{a:08X}: {count} call sites' for a, count in sorted(frequency_stops.items()))
    if not frequency_stops:
        lines.append('- (empty)')
    lines.extend(['', '## How to read', '',
                  'A citation is evidence, not a proof of complete equivalence. An uncited address can be',
                  'implemented under another address or inlined. Cited neighbours are leads, not proof.',
                  'Indirect memory calls are candidates for virtual calls; their targets are not followed.', '',
                  '## None / partial, by importance', '',
                  '| Module | Objects | Depth | Function | Instructions | Status | Decomp |',
                  '|---|---:|---:|---|---:|---|---|'])
    pending = sorted((r for r in rows if r.status != 'ported'),
                     key=lambda r: importance(r.objects, r.depth, r.instructions, r.module, r.function))
    for r in pending:
        clean = lambda s: s.replace('|', '\\|').replace('\n', ' ')
        lines.append(f'| {clean(r.module)} | {r.objects} | {r.depth} | 0x{r.function:08X} | {r.instructions} | {r.status} | {clean(r.decomp)} |')
    markdown.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    for module, address, old in changed:
        print(f'{module} 0x{address:08X}: {old} -> ported')
    print(f'{len(changed)} rows moved from none/partial to ported')
    return changed


@dataclass(frozen=True)
class ModuleTables:
    name: str
    create: int
    constructor: int
    data_vtable: int
    tables: tuple[tuple[int, int], ...]


def discover_module_tables(image, registry: list[dict], constructor_finder, table_finder) -> list[ModuleTables]:
    """Resolve constructor-installed tables, never treating ModuleData as Module.

    The finders operate on the PE image. Each result needs a primary table at
    offset zero; an unresolved factory is an error, not an empty module graph.
    The offset table can contain secondary interfaces and still requires an
    independently determined base class and slot extent before use as roots.
    """
    result = []
    seen = set()
    for record in registry:
        name = record['name']
        if name in seen:
            raise ValueError(f'duplicate module: {name}')
        seen.add(name)
        create = int(record['create'], 0)
        ctor = constructor_finder(image, create)
        if ctor is None:
            raise ValueError(f'{name}: constructor not found for 0x{create:X}')
        stores = table_finder(image, ctor)
        if 0 not in stores:
            raise ValueError(f'{name}: primary vtable not found for 0x{ctor:X}')
        result.append(ModuleTables(name, create, ctor, int(record['vtable'], 0), tuple(sorted(stores.items()))))
    return result


@dataclass(frozen=True)
class ModuleEntry:
    name: str
    tag: str
    category: str
    mask: int
    ai: bool
    slot7: bool
    inheritable: bool = False
    copied: bool = False


def copy_module_state(entries: tuple[ModuleEntry, ...]) -> tuple[ModuleEntry, ...]:
    """Mark all copied modules, as ThingTemplate::setCopiedFromDefault does."""
    from dataclasses import replace
    return tuple(replace(entry, copied=True) for entry in entries)


def remove_module(entries: tuple[ModuleEntry, ...], tag: str) -> tuple[tuple[ModuleEntry, ...], ModuleEntry]:
    """Remove the tag from all categories (RW 0x73E782 / 0x73E2B3)."""
    removed = [entry for entry in entries if entry.tag == tag]
    if len(removed) != 1:
        raise ValueError(f'module tag {tag!r}: expected one entry, found {len(removed)}')
    return tuple(entry for entry in entries if entry.tag != tag), removed[0]


def add_module(entries: tuple[ModuleEntry, ...], entry: ModuleEntry, *, child: bool,
               replacing: ModuleEntry | None = None) -> tuple[ModuleEntry, ...]:
    """Replay the module-list effects, given actual registry mask/predicate facts.

    This does not parse INI, classify unknown classes, or relax invalid tags.
    Non-child declarations clear copied, non-inheritable entries with matching
    interface masks across all lists (RW 0x73F33D..0x73F375). Data predicates
    clear AI or slot-7 entries within the same list (RW 0x73F496..0x73F4B9).
    A child can replace an existing tag, except a different Draw class.
    """
    from dataclasses import replace
    if not entry.name or not entry.tag or entry.category not in {'behavior', 'draw', 'clientupdate', 'clientbehavior'}:
        raise ValueError('invalid module name, tag or category')
    if replacing and (replacing.name != entry.name or replacing.tag == entry.tag):
        raise ValueError('ReplaceModule requires the same class and a new tag')
    current = list(entries)
    if not child:
        current = [old for old in current if not (old.copied and not old.inheritable and old.mask & entry.mask)]
    if entry.ai:
        current = [old for old in current if not (old.category == entry.category and old.ai)]
    if child and entry.slot7:
        current = [old for old in current if not (old.category == entry.category and old.slot7)]
    matching = [old for old in current if old.tag == entry.tag]
    if matching:
        if not child or any(old.category == 'draw' and old.name != entry.name for old in matching):
            raise ValueError(f'duplicate tag {entry.tag!r}')
        current = [old for old in current if old.tag != entry.tag]
    current.append(replace(entry, copied=False))
    return tuple(current)


@dataclass(frozen=True)
class ModuleEvent:
    action: str
    tag: str = ''
    name: str = ''
    category: str = ''
    inheritable: bool = False
    source: str = ''


def parse_module_events(lines: list[tuple[str, str]], start: int, grammar):
    """Extract object-level module edits using the retail field-table grammar.

    `start` is the first line inside an Object/ChildObject/ObjectReskin. Other
    object sub-blocks and module data are skipped using field_audit's grammar;
    arbitrary tags are supported. Return (events, index after the closing End).
    Unknown module classes or unclosed blocks are errors, not empty modules.
    """
    import sys
    local = str(Path(__file__).resolve().parent)
    if local not in sys.path:
        sys.path.insert(0, local)
    import field_audit as fa

    classes = {name.lower(): (name, node) for name, node in grammar.classes.items()}

    def module_end(i, node, source):
        while i < len(lines):
            key, _ = fa.field_key(lines[i][0])
            if key.lower() == 'end':
                return i
            kind = fa.row_kind(node, key) if node is not None else None
            if kind and kind[0] == 'script':
                i += 1
                while i < len(lines) and fa.field_key(lines[i][0])[0].lower() != 'endscript':
                    i += 1
                if i == len(lines):
                    raise ValueError(f'{source}: unclosed module script')
            elif (kind and kind[0] == 'block' and i + 1 < len(lines)
                  and fa.field_key(lines[i + 1][0])[0].lower() not in {'end', key.lower()}
                  and fa.opens_block(lines, i + 1, kind[1], node, grammar.classes)):
                end = fa.block_end(lines, i + 1, kind[1], grammar.classes)
                if end < 0:
                    raise ValueError(f'{source}: unclosed nested module block')
                i = end
            i += 1
        raise ValueError(f'{source}: unclosed module')

    def walk(i, mode):
        events = []
        while i < len(lines):
            text, source = lines[i]
            key, value = fa.field_key(text)
            low = key.lower()
            if low == 'end':
                return events, i + 1
            if low in {'addmodule', 'inheritablemodule', 'replacemodule'}:
                args = value.replace('=', ' ').split()
                if mode != 'normal':
                    raise ValueError(f'{source}: nested module editing mode')
                if low == 'replacemodule':
                    if len(args) != 1:
                        raise ValueError(f'{source}: ReplaceModule requires a tag')
                    events.append(ModuleEvent('replace_begin', tag=args[0], source=source))
                child, i = walk(i + 1, low)
                events.extend(child)
                if low == 'replacemodule':
                    events.append(ModuleEvent('replace_end', tag=args[0], source=source))
                continue
            if low == 'removemodule':
                args = value.replace('=', ' ').split()
                if mode != 'normal' or len(args) != 1:
                    raise ValueError(f'{source}: invalid RemoveModule')
                events.append(ModuleEvent('remove', tag=args[0], source=source))
            elif low in fa.MODULE_KEYS:
                args = value.replace('=', ' ').split()
                if len(args) < 2:
                    raise ValueError(f'{source}: {key} requires class and tag')
                found = classes.get(args[0].lower())
                if not found:
                    raise ValueError(f'{source}: unknown module class {args[0]}')
                end = module_end(i + 1, found[1], source)
                category = 'behavior' if low == 'body' else low
                events.append(ModuleEvent('add', tag=args[1], name=found[0], category=category,
                                          inheritable=mode == 'inheritablemodule', source=source))
                i = end + 1
                continue
            else:
                kind = fa.row_kind(grammar.object, low)
                if kind and kind[0] == 'script':
                    i += 1
                    while i < len(lines) and fa.field_key(lines[i][0])[0].lower() != 'endscript':
                        i += 1
                    if i == len(lines):
                        raise ValueError(f'{source}: unclosed script')
                elif kind and kind[0] == 'block' and fa.opens_block(lines, i + 1, kind[1], grammar.object, grammar.classes):
                    end = fa.block_end(lines, i + 1, kind[1], grammar.classes)
                    if end < 0:
                        raise ValueError(f'{source}: unclosed object block {key}')
                    i = end
            i += 1
        raise ValueError('object or module edit block is not closed')

    return walk(start, 'normal')
